#!/usr/bin/env python3
"""The bundled engines' pins and packaging (tools/bundled_engines.py): the committed pin file holds; a pinned asset is
downloaded once, held to its hash and a mismatch refused with nothing left behind; the stage lays the engines out with
their manifest; the handshake reads what an engine says and the smoke test refuses one that does not name its pinned
version; and the update moves a pin to a newer release, refusing an asset whose hash is not GitHub's digest.

Run by CTest (packaging), the fake engine named by AMPHITHEATER_FAKE_ENGINE; every download is from a folder of the test's
own through a file: URL.
"""
import hashlib
import io
import json
import os
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import bundled_engines as be  # noqa: E402


def sha(data):
    return hashlib.sha256(data).hexdigest()


class Releases:
    """Releases laid out as GitHub serves their assets: <root>/<repository>/releases/download/<tag>/<asset>."""

    def __init__(self, root):
        self.root = Path(root)
        os.environ["RECKLESS_ENGINES_BASE_URL"] = self.root.as_uri()

    def add(self, repository, tag, name, data):
        path = self.root / repository / "releases" / "download" / tag / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return sha(data)


def zip_of(member, data):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as z:
        z.writestr(member, data)
        z.writestr("._" + member, b"AppleDouble")
    return buffer.getvalue()


def pins_for(version, bare_sha, zip_sha):
    return {"format": 1, "engines": [{
        "id": "toy", "name": "Toy", "repository": "example/toy", "tag": f"v{version}", "version": version,
        "copyright": "Copyright (C) 2026 Someone", "licence": "GPL-3.0", "source_asset_pattern": None, "source_asset": None,
        "options": [{"name": "Hash", "value": "64"}],
        "builds": {
            "windows": [{"asset_pattern": "toy-{version}.exe", "asset": f"toy-{version}.exe", "sha256": bare_sha,
                         "file": "toy.exe"}],
            "macos": [{"asset_pattern": "toy-{version}-macos.zip", "asset": f"toy-{version}-macos.zip",
                       "sha256": zip_sha, "member_pattern": "toy-{version}", "member": f"toy-{version}",
                       "file": "toy", "requires": ""}],
        }}]}


class PinFile(unittest.TestCase):
    def test_the_committed_pins_hold_every_engine_on_every_platform(self):
        pins = be.load_pins()
        ids = [engine["id"] for engine in pins["engines"]]
        self.assertEqual(ids, ["pawnstar", "zenith", "pawnstar-go", "pawnstar-rs", "stoopid-chess"])
        for engine in pins["engines"]:
            self.assertEqual(sorted(engine["builds"]), ["linux", "macos", "windows"], engine["id"])
            for option in engine["options"]:
                if option["name"] == "Threads":
                    self.assertIn(option["value"], ("1", "2"), "a small number of threads, not the engine's own")
        pawnstar = next(e for e in pins["engines"] if e["id"] == "pawnstar")
        self.assertIn({"name": "Move Overhead", "value": "100"}, pawnstar["options"])
        for engine_id in ("pawnstar", "pawnstar-go"):
            engine = next(e for e in pins["engines"] if e["id"] == engine_id)
            self.assertEqual({b.get("requires") for p in ("windows", "linux") for b in engine["builds"][p]},
                             {"x86-64-v3"}, f"{engine_id}'s x86-64 build is AVX2 throughout")
        zenith = next(e for e in pins["engines"] if e["id"] == "zenith")
        for platform_name in ("windows", "linux"):
            self.assertEqual([b.get("requires") for b in zenith["builds"][platform_name]],
                             ["x86-64-v3-fast-pext", "x86-64-v2"], "the PEXT build first, the portable one after")
        # The file the update writes is the file committed.
        self.assertEqual(be.PINS.read_text(encoding="utf-8").replace("\r\n", "\n"),
                         json.dumps(pins, indent=2, ensure_ascii=False) + "\n")

    def test_a_malformed_pin_is_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "engines.json"
            for broken in ({"format": 2, "engines": []},
                           pins_for("1.0.0", "0" * 63, "1" * 64),
                           dict(pins_for("1.0.0", "0" * 64, "1" * 64), engines=[
                               dict(pins_for("1.0.0", "0" * 64, "1" * 64)["engines"][0], tag="v9.9.9")])):
                path.write_text(json.dumps(broken))
                with self.assertRaises(be.PinError):
                    be.load_pins(path)
            path.write_text(json.dumps(pins_for("1.0.0", "0" * 64, "1" * 64)))
            self.assertEqual(be.load_pins(path)["engines"][0]["id"], "toy")


class Fetch(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.root = Path(self.folder.name)
        self.releases = Releases(self.root / "github")
        self.cache = self.root / "cache"

    def tearDown(self):
        os.environ.pop("RECKLESS_ENGINES_BASE_URL", None)
        self.folder.cleanup()

    def test_a_pinned_asset_is_fetched_once_and_held_to_its_hash(self):
        digest = self.releases.add("example/toy", "v1.0.0", "toy-1.0.0.exe", b"engine")
        path = be.fetch_pinned("example/toy", "v1.0.0", "toy-1.0.0.exe", digest, self.cache)
        self.assertEqual(path.read_bytes(), b"engine")
        (self.root / "github" / "example/toy/releases/download/v1.0.0/toy-1.0.0.exe").unlink()
        self.assertEqual(be.fetch_pinned("example/toy", "v1.0.0", "toy-1.0.0.exe", digest, self.cache), path,
                         "the cache serves it, with no download")
        path.write_bytes(b"tampered")
        with self.assertRaises(Exception):
            be.fetch_pinned("example/toy", "v1.0.0", "toy-1.0.0.exe", digest, self.cache)

    def test_a_hash_that_differs_is_refused_and_nothing_kept(self):
        self.releases.add("example/toy", "v1.0.0", "toy-1.0.0.exe", b"not what was pinned")
        with self.assertRaisesRegex(be.PinError, "not the pinned"):
            be.fetch_pinned("example/toy", "v1.0.0", "toy-1.0.0.exe", sha(b"engine"), self.cache)
        self.assertEqual([p for p in self.cache.rglob("*") if p.is_file()], [], "the refused file is deleted")

    def test_the_stage_lays_out_every_build_with_its_manifest(self):
        bare = self.releases.add("example/toy", "v1.0.0", "toy-1.0.0.exe", b"windows engine")
        zipped = self.releases.add("example/toy", "v1.0.0", "toy-1.0.0-macos.zip", zip_of("toy-1.0.0", b"mac engine"))
        pins = pins_for("1.0.0", bare, zipped)
        for platform_name, file, data in (("windows", "toy.exe", b"windows engine"), ("macos", "toy", b"mac engine")):
            out = self.root / platform_name / "engines"
            manifest = be.stage(pins, platform_name, out, self.cache, do_handshake=False)
            self.assertEqual((out / "toy" / file).read_bytes(), data)
            self.assertEqual(sorted(p.name for p in (out / "toy").iterdir()), [file], "the AppleDouble left out")
            if os.name != "nt":
                self.assertTrue(os.access(out / "toy" / file, os.X_OK))
            on_disk = json.loads((out / "bundled.json").read_text())
            self.assertEqual(on_disk, manifest)
            self.assertEqual(on_disk["engines"][0]["builds"], [{"executable": f"toy/{file}", "requires": ""}])
            self.assertEqual(on_disk["engines"][0]["options"], [{"name": "Hash", "value": "64"}])
            self.assertEqual(on_disk["engines"][0]["licence"], "GPL-3.0", "the About window names it")
        self.assertEqual(be.stage(pins, "linux", self.root / "linux", self.cache, False)["engines"], [],
                         "a platform the engine has no build for carries none of it")

    def test_a_companion_is_put_beside_every_build_held_to_its_hash_and_its_licence(self):
        licence = "Permission is hereby granted to use this runtime.\n"
        (self.root / "COPYING.txt").write_text(licence, encoding="utf-8")
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w") as z:
            z.writestr("kit/bin/runtime.dll", b"a runtime")
            z.writestr("kit/COPYING.txt", licence)
        archive = self.root / "kit.zip"
        archive.write_bytes(buffer.getvalue())
        companion = {"file": "runtime.dll", "asset": "kit.zip", "member": "kit/bin/runtime.dll",
                     "member_sha256": sha(b"a runtime"), "licence_member": "kit/COPYING.txt",
                     "licence": str(self.root / "COPYING.txt")}
        folders = [self.root / "a", self.root / "b"]
        for folder in folders:
            folder.mkdir()
        be.place_companion({"id": "toy"}, companion, archive, folders)
        for folder in folders:
            self.assertEqual((folder / "runtime.dll").read_bytes(), b"a runtime")
        with self.assertRaisesRegex(be.PinError, "not the pinned file"):
            be.place_companion({"id": "toy"}, dict(companion, member_sha256="0" * 64), archive, folders)
        with self.assertRaisesRegex(be.PinError, "read it"):
            be.place_companion({"id": "toy"}, dict(companion, licence_member="kit/bin/runtime.dll"), archive, folders)

    def test_the_committed_pins_need_no_companion(self):
        for engine in be.load_pins()["engines"]:
            self.assertNotIn("companions", engine, f"{engine['id']}'s release carries everything it needs")

    def test_an_archive_without_its_member_is_refused(self):
        bare = self.releases.add("example/toy", "v1.0.0", "toy-1.0.0.exe", b"x")
        zipped = self.releases.add("example/toy", "v1.0.0", "toy-1.0.0-macos.zip", zip_of("other", b"y"))
        with self.assertRaisesRegex(be.PinError, "holds no toy-1.0.0"):
            be.stage(pins_for("1.0.0", bare, zipped), "macos", self.root / "out", self.cache, False)

    def test_the_update_moves_a_pin_and_refuses_a_digest_that_differs(self):
        old_bare = self.releases.add("example/toy", "v1.0.0", "toy-1.0.0.exe", b"one")
        old_zip = self.releases.add("example/toy", "v1.0.0", "toy-1.0.0-macos.zip", zip_of("toy-1.0.0", b"one"))
        new_bare = self.releases.add("example/toy", "v1.1.0", "toy-1.1.0.exe", b"two")
        new_zip = self.releases.add("example/toy", "v1.1.0", "toy-1.1.0-macos.zip", zip_of("toy-1.1.0", b"two"))
        pins = pins_for("1.0.0", old_bare, old_zip)
        release = {"tag_name": "v1.1.0", "assets": [
            {"name": "toy-1.1.0.exe", "digest": f"sha256:{new_bare}"},
            {"name": "toy-1.1.0-macos.zip", "digest": f"sha256:{new_zip}"}]}
        changes = be.update(pins, self.cache, latest_release=lambda repository: release)
        self.assertEqual(len(changes), 2)
        engine = pins["engines"][0]
        self.assertEqual((engine["tag"], engine["version"]), ("v1.1.0", "1.1.0"))
        self.assertEqual(engine["builds"]["windows"][0]["sha256"], new_bare)
        self.assertEqual(engine["builds"]["macos"][0]["member"], "toy-1.1.0")
        self.assertEqual(be.update(pins, self.cache, latest_release=lambda repository: release), [],
                         "a pin at the latest release stays as it is")
        release["assets"][0]["digest"] = "sha256:" + "f" * 64
        with self.assertRaisesRegex(be.PinError, "GitHub's digest"):
            be.update(pins_for("1.0.0", old_bare, old_zip), self.cache, latest_release=lambda repository: release)


class Handshake(unittest.TestCase):
    def test_an_option_line_reads_as_the_engine_list_keeps_it(self):
        self.assertEqual(be.parse_option("option name Move Overhead type spin default 30 min 0 max 5000"),
                         {"name": "Move Overhead", "type": "spin", "default": "30", "min": 0, "max": 5000})
        self.assertEqual(be.parse_option("option name Style type combo default Solid Play var Solid Play var Wild"),
                         {"name": "Style", "type": "combo", "default": "Solid Play", "choices": ["Solid Play", "Wild"]})
        self.assertEqual(be.parse_option("option name Clear Hash type button"),
                         {"name": "Clear Hash", "type": "button", "default": ""})

    @unittest.skipUnless(os.environ.get("AMPHITHEATER_FAKE_ENGINE"), "AMPHITHEATER_FAKE_ENGINE names no fake engine")
    def test_the_handshake_reads_an_engine_and_the_smoke_test_holds_it_to_its_version(self):
        fake = Path(os.environ["AMPHITHEATER_FAKE_ENGINE"])
        said = be.handshake(fake)
        self.assertEqual(said["name"], "Fake Engine")
        self.assertTrue(any(option["name"] == "Hash" for option in said["options"]))
        with self.assertRaisesRegex(be.PinError, "uciok"):
            be.handshake(fake, ["--exit-on-uci"])
        with tempfile.TemporaryDirectory() as folder:
            engines = Path(folder)
            (engines / "toy").mkdir()
            target = engines / "toy" / fake.name
            target.write_bytes(fake.read_bytes())
            target.chmod(0o755)
            manifest = {"format": 1, "engines": [{"id": "toy", "version": "1.0.0", "builds": [
                {"executable": f"toy/{fake.name}", "requires": ""}]}]}
            (engines / "bundled.json").write_text(json.dumps(manifest))
            with self.assertRaisesRegex(be.PinError, "not version 1.0.0"):
                be.smoke(engines)
            be.check_version({"id": "toy", "version": "1.0.0"}, {"name": "Toy 1.0.0"})
            be.check_version({"id": "toy", "version": "2.0.0"}, {"name": "Toy v2.0.0"})
            with self.assertRaises(be.PinError):
                be.check_version({"id": "toy", "version": "1.0.0"}, {"name": "Toy 1.0.01"})
            target.unlink()
            with self.assertRaisesRegex(be.PinError, "is not there"):
                be.smoke(engines)

    @unittest.skipUnless(os.name == "nt" and os.environ.get("AMPHITHEATER_FAKE_ENGINE"), "a Windows image to read")
    def test_a_windows_engine_may_import_the_system_and_what_lies_beside_it_alone(self):
        fake = Path(os.environ["AMPHITHEATER_FAKE_ENGINE"])
        imports = be.pe_imports(fake)
        self.assertIn("kernel32.dll", imports)
        be.check_dependencies(fake, "windows")  # the static runtime: the system's DLLs alone
        with tempfile.TemporaryDirectory() as folder:
            not_an_image = Path(folder) / "engine.exe"
            not_an_image.write_bytes(b"#!/bin/sh\n")
            self.assertEqual(be.pe_imports(not_an_image), [])
        self.assertNotIn("vcruntime140.dll", be.WINDOWS_SYSTEM_DLLS, "a compiler's runtime is no system DLL")
        self.assertNotIn("libwinpthread-1.dll", be.WINDOWS_SYSTEM_DLLS)

    def test_this_machine_meets_no_requirement_it_does_not_know(self):
        self.assertTrue(be.cpu_meets(""))
        self.assertFalse(be.cpu_meets("x86-64-v9"))


if __name__ == "__main__":
    unittest.main(verbosity=2)

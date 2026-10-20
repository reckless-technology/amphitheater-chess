#!/usr/bin/env python3
"""The engines the installers carry: Jonny Reckless's five UCI engines, each the binary of a public release, pinned in
packaging/engines.json by its tag, each platform's asset and the asset's SHA-256.

    python3 tools/bundled_engines.py update [--check] [--cache DIR]
        Move every pin to its repository's latest release: the tag, the version, each asset's name and its SHA-256
        computed from the download, held to GitHub's own digest of the asset and, for Zenith, to the release's
        SHA256SUMS. --check writes nothing and exits 1 when a pin is behind. Opens nothing by itself: the pull request
        is a person's to make.

    python3 tools/bundled_engines.py fetch --platform windows|linux|macos [--cache DIR]
        Download each of the platform's pinned assets into the cache (once: a cached file is kept while its hash is the
        pin's), refusing one whose SHA-256 is not the pin's.

    python3 tools/bundled_engines.py stage --platform P --out ENGINES_DIR [--cache DIR] [--no-handshake]
                                           [--leave-out-unfit]
        The engines laid out for an installer, ENGINES_DIR/<id>/<file> (executable), and ENGINES_DIR/bundled.json, the
        manifest the host reads to seed a person's engine list: each engine's name, version, licence, the defaults it
        is seeded with, the options its handshake declared and its builds, the first whose requirement the CPU meets
        the one run. The handshake (uci, isready, quit) runs every build this machine can run, so a staged engine
        that does not answer stops the stage; --no-handshake leaves it out, for a platform other than this machine's.
        A library an engine's release needs and does not carry - a companion, pinned in the engine's entry with its
        archive, its own hash and its licence - is put beside every build of it (Zenith's libwinpthread-1.dll on
        Windows, until its release links it statically); an engine that still imports what a fresh machine lacks
        stops the stage, or with --leave-out-unfit is left out of it with a warning.

    python3 tools/bundled_engines.py smoke ENGINES_DIR
        Every engine of a manifest, from where it lies, answers uci with uciok and isready with readyok, and names its
        pinned version: each build this machine can run (both of Zenith's x86-64 builds where the CPU takes both).

The cache is --cache, else $RUNNER_TOOL_CACHE/reckless-uci-host-engines, else build/engines-cache; each file is kept
under its own SHA-256, so two pins never meet. RECKLESS_ENGINES_BASE_URL replaces https://github.com for the
downloads (the suite points it at a folder of its own through a file: URL). Only the standard library is used.
"""
import hashlib
import json
import os
import platform
import re
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PINS = ROOT / "packaging" / "engines.json"
PLATFORMS = ("windows", "linux", "macos")
MANIFEST = "bundled.json"
FORMAT = 1
# What a build may ask of the CPU: x86-64-v2 (SSE4.2, POPCNT and the rest of the level); x86-64-v3 (AVX2, BMI1, BMI2,
# FMA and the rest of the level: Pawnstar's and Pawnstar-Go's x86-64 builds, whose evaluation is AVX2 throughout); and
# x86-64-v3 with a PEXT that is not microcoded - on any CPU but AMD's family 17h (Zen 1 and 2), whose PEXT takes hundreds
# of cycles. src/app/BundledEngines.cpp holds the host's own reading of the same three.
REQUIREMENTS = ("", "x86-64-v2", "x86-64-v3", "x86-64-v3-fast-pext")
HANDSHAKE_SECONDS = 60


class PinError(Exception):
    """A pin that does not hold: a hash that differs, an asset or a member that is not there, a malformed pin file."""


# ---- The pin file ---------------------------------------------------------------------------------------------------

def load_pins(path=PINS):
    """The pin file, its every field checked: a malformed one is refused rather than half used."""
    pins = json.loads(Path(path).read_text(encoding="utf-8"))
    if pins.get("format") != FORMAT:
        raise PinError(f"{path}: not a pin file of format {FORMAT}")
    ids = set()
    for engine in pins.get("engines", []):
        for key in ("id", "name", "repository", "tag", "version", "copyright", "licence", "options", "builds"):
            if key not in engine:
                raise PinError(f"{path}: an engine has no {key}")
        if engine["id"] in ids or not re.fullmatch(r"[a-z0-9-]+", engine["id"]):
            raise PinError(f"{path}: the id {engine['id']!r} is repeated or not lower-case and hyphens")
        ids.add(engine["id"])
        if engine["tag"] != "v" + engine["version"]:
            raise PinError(f"{engine['id']}: the tag {engine['tag']} is not v{engine['version']}")
        for name in ("source_asset", "checksums_asset"):
            pattern = engine.get(name + "_pattern")
            if pattern is not None and engine.get(name) != pattern.format(version=engine["version"]):
                raise PinError(f"{engine['id']}: {name} is not its pattern at {engine['version']}")
        for platform_name, companions in engine.get("companions", {}).items():
            for companion in companions:
                keys = ("file", "what", "repository", "tag", "asset", "sha256", "member", "member_sha256",
                        "licence_member", "licence")
                if platform_name not in PLATFORMS or any(key not in companion for key in keys) or not all(
                        re.fullmatch(r"[0-9a-f]{64}", companion[key]) for key in ("sha256", "member_sha256")):
                    raise PinError(f"{engine['id']}: a companion on {platform_name!r} lacks a field or a hash")
        for platform_name, builds in engine["builds"].items():
            if platform_name not in PLATFORMS or not builds:
                raise PinError(f"{engine['id']}: builds for {platform_name!r}")
            for build in builds:
                if build.get("asset") != build["asset_pattern"].format(version=engine["version"]):
                    raise PinError(f"{engine['id']}: {build.get('asset')} is not its pattern at {engine['version']}")
                if "member_pattern" in build and build.get("member") != build["member_pattern"].format(
                        version=engine["version"]):
                    raise PinError(f"{engine['id']}: {build.get('member')} is not its pattern at {engine['version']}")
                if not re.fullmatch(r"[0-9a-f]{64}", build.get("sha256", "")):
                    raise PinError(f"{engine['id']}: {build['asset']} has no SHA-256")
                if build.get("requires", "") not in REQUIREMENTS:
                    raise PinError(f"{engine['id']}: {build['asset']} requires {build['requires']!r}")
                if not re.fullmatch(r"[A-Za-z0-9._-]+(/[A-Za-z0-9._-]+)*", build["file"]):
                    raise PinError(f"{engine['id']}: the file {build['file']!r}")
    return pins


def save_pins(pins, path=PINS):
    """The pin file written as the update writes it: two-space JSON, LF, one newline at the end."""
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(pins, indent=2, ensure_ascii=False) + "\n")


# ---- Downloads ------------------------------------------------------------------------------------------------------

def base_url():
    return os.environ.get("RECKLESS_ENGINES_BASE_URL", "https://github.com").rstrip("/")


def asset_url(repository, tag, asset):
    return f"{base_url()}/{repository}/releases/download/{tag}/{asset}"


def default_cache():
    if os.environ.get("RUNNER_TOOL_CACHE"):
        return Path(os.environ["RUNNER_TOOL_CACHE"]) / "reckless-uci-host-engines"
    return ROOT / "build" / "engines-cache"


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def download(url, into):
    """A URL's bytes into a file, whole or not at all."""
    into.parent.mkdir(parents=True, exist_ok=True)
    partial = into.with_name(into.name + ".part")
    request = urllib.request.Request(url, headers={"User-Agent": "reckless-uci-host-packaging"})
    with urllib.request.urlopen(request, timeout=300) as response, open(partial, "wb") as f:
        shutil.copyfileobj(response, f, 1 << 20)
    os.replace(partial, into)


def fetch_pinned(repository, tag, asset, sha256, cache):
    """A pinned asset from the cache, downloaded first where the cache does not hold it at its hash; a download whose
    hash is not the pin's is deleted and refused."""
    path = Path(cache) / sha256 / asset
    if path.is_file() and sha256_of(path) == sha256:
        return path
    download(asset_url(repository, tag, asset), path)
    got = sha256_of(path)
    if got != sha256:
        path.unlink()
        raise PinError(f"{asset} from {repository} {tag} has SHA-256 {got}, not the pinned {sha256}: refused")
    return path


def fetch(pins, platform_name, cache):
    """Every pinned asset of a platform, each held to its pin, the engines' companions' archives among them.
    @return {(engine id, file): path in the cache}."""
    out = {}
    for engine in pins["engines"]:
        for build in engine["builds"].get(platform_name, []):
            out[(engine["id"], build["file"])] = fetch_pinned(engine["repository"], engine["tag"], build["asset"],
                                                              build["sha256"], cache)
        for companion in engine.get("companions", {}).get(platform_name, []):
            out[(engine["id"], "companion:" + companion["file"])] = fetch_pinned(
                companion["repository"], companion["tag"], companion["asset"], companion["sha256"], cache)
    return out


def place_companion(engine, companion, archive, folders):
    """A library an engine's release needs and does not carry - a companion, pinned in its engine's entry by the
    archive it comes from, the member's own SHA-256 and its licence - put beside every build of the engine, the member
    held to its hash and the licence in the archive to the committed copy the notices reproduce."""
    with zipfile.ZipFile(archive) as z:
        data = z.read(companion["member"])
        licence = z.read(companion["licence_member"]).decode("utf-8").replace("\r\n", "\n")
    if hashlib.sha256(data).hexdigest() != companion["member_sha256"]:
        raise PinError(f"{engine['id']}'s {companion['file']} in {companion['asset']} is not the pinned file")
    committed = (ROOT / companion["licence"]).read_text(encoding="utf-8").replace("\r\n", "\n")
    if licence != committed:
        raise PinError(f"{companion['asset']}'s licence for {companion['file']} is not {companion['licence']}: read it")
    for folder in folders:
        (folder / companion["file"]).write_bytes(data)


# ---- The layout ---------------------------------------------------------------------------------------------------

def extract(archive, member, into):
    """One member of a zip or a gzipped tar, or a bare binary as it is, written to `into`, executable."""
    into.parent.mkdir(parents=True, exist_ok=True)
    name = archive.name
    if name.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            if member not in z.namelist():
                raise PinError(f"{name} holds no {member} (it holds {z.namelist()})")
            with z.open(member) as source, open(into, "wb") as target:
                shutil.copyfileobj(source, target)
    elif name.endswith(".tar.gz"):
        with tarfile.open(archive, "r:gz") as t:
            try:
                info = t.getmember(member)
            except KeyError:
                raise PinError(f"{name} holds no {member} (it holds {t.getnames()})") from None
            if not info.isfile():
                raise PinError(f"{name}'s {member} is not a file")
            with t.extractfile(info) as source, open(into, "wb") as target:
                shutil.copyfileobj(source, target)
    else:
        if member is not None:
            raise PinError(f"{name} is a bare file, and its pin names a member {member}")
        shutil.copyfile(archive, into)
    into.chmod(0o755)


def stage(pins, platform_name, out, cache, do_handshake, leave_out_unfit=False):
    """The platform's engines laid out under `out`, and the manifest beside them. An engine that needs of the machine
    what a fresh one lacks (check_dependencies) stops the stage, or with leave_out_unfit - a pull request's run, while
    an engine's fixed release is awaited - is left out of it, named in a warning; a release ships every engine or
    none. @return The manifest."""
    out = Path(out)
    files = fetch(pins, platform_name, cache)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    manifest = {"format": FORMAT, "engines": []}
    for engine in pins["engines"]:
        builds = engine["builds"].get(platform_name)
        if not builds:
            continue
        entry = {"id": engine["id"], "name": engine["name"], "version": engine["version"],
                 "licence": engine["licence"], "repository": engine["repository"], "tag": engine["tag"],
                 "engine_name": "", "author": "",
                 "options": engine["options"], "declared": [], "builds": []}
        for build in builds:
            target = out / engine["id"] / build["file"]
            extract(files[(engine["id"], build["file"])], build.get("member"), target)
            entry["builds"].append({"executable": f"{engine['id']}/{build['file']}",
                                    "requires": build.get("requires", "")})
        for companion in engine.get("companions", {}).get(platform_name, []):
            place_companion(engine, companion, files[(engine["id"], "companion:" + companion["file"])],
                            {(out / build["executable"]).parent for build in entry["builds"]})
        try:
            for build in entry["builds"]:
                check_dependencies(out / build["executable"], platform_name)
        except PinError as error:
            if not leave_out_unfit:
                raise
            print(f"::warning::{engine['id']} is left out of this stage: {error}")
            shutil.rmtree(out / engine["id"])
            continue
        if do_handshake:
            for build in entry["builds"]:
                if not cpu_meets(build["requires"]):
                    print(f"  {build['executable']}: this CPU is not {build['requires']}; not started")
                    continue
                said = handshake(out / build["executable"])
                check_version(entry, said)
                if not entry["declared"]:
                    entry["engine_name"], entry["author"], entry["declared"] = said["name"], said["author"], \
                        said["options"]
                print(f"  {build['executable']}: {said['name']} by {said['author']}, "
                      f"{len(said['options'])} options")
            if not entry["engine_name"]:
                raise PinError(f"{engine['id']}: no build of it runs on this machine")
        manifest["engines"].append(entry)
    with open(out / MANIFEST, "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
    return manifest


# ---- The handshake ------------------------------------------------------------------------------------------------

def parse_option(line):
    """An "option" line as UCI writes it, in the shape the host's engine list keeps a declared option."""
    words = line.split()[1:]
    keys = {"name", "type", "default", "min", "max", "var"}
    fields, choices, key, value = {}, [], None, []

    def close():
        if key == "var":
            choices.append(" ".join(value))
        elif key is not None:
            fields[key] = " ".join(value)

    for word in words:
        # A name runs up to "type"; any other field to the next keyword.
        if word in keys and (key != "name" or word == "type"):
            close()
            key, value = word, []
        else:
            value.append(word)
    close()
    option = {"name": fields.get("name", ""), "type": fields.get("type", "string"),
              "default": fields.get("default", "")}
    for bound in ("min", "max"):
        if re.fullmatch(r"-?\d+", fields.get(bound, "")):
            option[bound] = int(fields[bound])
    if choices:
        option["choices"] = choices
    return option


def handshake(executable, arguments=()):
    """uci, isready and quit, with a deadline, the engine started in its own folder as the host starts it. @return What
    it said: its name, its author and its options."""
    executable = Path(executable).resolve()
    environment = None
    if os.name == "nt":
        # The system's folders alone on the PATH, as on a machine with no toolchain: a DLL that only a developer's PATH
        # provides (Git's or MSYS2's libwinpthread-1.dll) must not let an engine start here that would not start there.
        windows = os.environ.get("SystemRoot", r"C:\Windows")
        environment = dict(os.environ, PATH=f"{windows}\\System32;{windows}")
    try:
        done = subprocess.run([str(executable), *arguments], input="uci\nisready\nquit\n", capture_output=True,
                              text=True, timeout=HANDSHAKE_SECONDS, cwd=executable.parent, env=environment)
    except subprocess.TimeoutExpired:
        raise PinError(f"{executable} did not finish its handshake within {HANDSHAKE_SECONDS} s") from None
    except OSError as error:
        raise PinError(f"{executable} did not start: {error}") from None
    lines = [line.strip() for line in done.stdout.splitlines()]
    if "uciok" not in lines or "readyok" not in lines:
        raise PinError(f"{executable} answered no uciok and readyok (exit {done.returncode}):\n{done.stdout}\n"
                       f"{done.stderr}")
    said = {"name": "", "author": "", "options": []}
    for line in lines:
        if line.startswith("id name "):
            said["name"] = line[len("id name "):]
        elif line.startswith("id author "):
            said["author"] = line[len("id author "):]
        elif line.startswith("option "):
            said["options"].append(parse_option(line))
    return said


def check_version(entry, said):
    if not re.search(rf"(^|\s|v){re.escape(entry['version'])}($|\s)", said["name"]):
        raise PinError(f"{entry['id']} names itself {said['name']!r}, not version {entry['version']}")


# ---- What an engine needs of the machine ----------------------------------------------------------------------------

# The DLLs every Windows the host runs on (10 and later) carries: an engine may import these and nothing else that is
# not beside it. The C and C++ runtimes of a compiler - MSVCP140.dll, VCRUNTIME140.dll, libwinpthread-1.dll,
# libc++.dll and the rest - are not among them: a machine without the redistributable that installs them, or without
# a toolchain on its PATH, would not start the engine at all (STATUS_DLL_NOT_FOUND).
WINDOWS_SYSTEM_DLLS = {
    "advapi32.dll", "bcrypt.dll", "bcryptprimitives.dll", "comctl32.dll", "comdlg32.dll", "crypt32.dll",
    "dbghelp.dll", "gdi32.dll", "iphlpapi.dll", "kernel32.dll", "kernelbase.dll", "msvcrt.dll", "mswsock.dll",
    "normaliz.dll", "ntdll.dll", "ole32.dll", "oleaut32.dll", "powrprof.dll", "psapi.dll", "rpcrt4.dll", "secur32.dll",
    "setupapi.dll", "shell32.dll", "shlwapi.dll", "ucrtbase.dll", "user32.dll", "userenv.dll", "version.dll",
    "winmm.dll", "ws2_32.dll",
}


def pe_imports(path):
    """The DLLs a PE file imports by its import table, lower-cased. A file that is not a PE32+ image has none."""
    data = Path(path).read_bytes()
    if data[:2] != b"MZ":
        return []
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        return []
    sections, optional_size = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    if struct.unpack_from("<H", data, optional)[0] != 0x20B:
        raise PinError(f"{path} is not a 64-bit Windows image")
    import_rva = struct.unpack_from("<I", data, optional + 112 + 8)[0]
    table = optional + optional_size

    def offset(rva):
        for i in range(sections):
            size, address, _, raw = struct.unpack_from("<IIII", data, table + 40 * i + 8)
            if address <= rva < address + max(size, 1):
                return raw + rva - address
        raise PinError(f"{path}: an import's address lies in no section")

    names = []
    if import_rva:
        descriptor = offset(import_rva)
        while True:
            name_rva = struct.unpack_from("<I", data, descriptor + 12)[0]
            if name_rva == 0:
                break
            start = offset(name_rva)
            names.append(data[start:data.index(b"\0", start)].decode("ascii").lower())
            descriptor += 20
    return names


def check_dependencies(executable, platform_name):
    """An engine as the installer lays it out needs nothing of the machine but its system: on Windows every DLL it
    imports a system DLL (WINDOWS_SYSTEM_DLLS, or an API set) or a file beside it; on macOS every library it loads the
    system's (otool -L, where this machine has it). A dependency a fresh machine lacks stops the stage, naming it."""
    executable = Path(executable)
    if platform_name == "windows":
        missing = [name for name in pe_imports(executable)
                   if name not in WINDOWS_SYSTEM_DLLS and not name.startswith(("api-ms-win-", "ext-ms-"))
                   and not (executable.parent / name).is_file()]
        if missing:
            raise PinError(f"{executable.name} imports {', '.join(missing)}, which Windows does not carry and nothing "
                           "beside it provides: its release would not start on a machine without them")
    elif platform_name == "macos" and shutil.which("otool"):
        listed = subprocess.run(["otool", "-L", str(executable)], capture_output=True, text=True, check=True).stdout
        libraries = [line.split()[0] for line in listed.splitlines()[1:] if line.strip()]
        foreign = [lib for lib in libraries if not lib.startswith(("/usr/lib/", "/System/Library/"))]
        if foreign:
            raise PinError(f"{executable.name} loads {', '.join(foreign)}, which macOS does not carry")


# ---- The CPU ------------------------------------------------------------------------------------------------------

def cpu_flags():
    """The CPU's feature flags and whether its PEXT is fast, as far as the standard library can tell: Linux's
    /proc/cpuinfo whole; on Windows the processor's AVX2 bit (PF_AVX2_INSTRUCTIONS_AVAILABLE) standing for the
    x86-64-v3 level, and its vendor and family from the platform's description; nothing for anything else."""
    machine = platform.machine().lower()
    if machine not in ("x86_64", "amd64"):
        return None
    if sys.platform.startswith("linux"):
        text = Path("/proc/cpuinfo").read_text()
        flags = set(re.search(r"^flags\s*:\s*(.*)$", text, re.M).group(1).split())
        vendor = re.search(r"^vendor_id\s*:\s*(\S+)", text, re.M).group(1)
        family = int(re.search(r"^cpu family\s*:\s*(\d+)", text, re.M).group(1))
        v2 = {"sse4_2", "sse4_1", "ssse3", "popcnt", "cx16"} <= flags
        v3 = v2 and {"avx2", "bmi1", "bmi2", "fma", "f16c", "movbe", "abm"} <= flags
        return {"x86-64-v2": v2, "x86-64-v3": v3,
                "x86-64-v3-fast-pext": v3 and not (vendor == "AuthenticAMD" and family == 0x17)}
    if sys.platform == "win32":
        import ctypes
        feature = ctypes.windll.kernel32.IsProcessorFeaturePresent
        v2 = bool(feature(38))  # PF_SSE4_2_INSTRUCTIONS_AVAILABLE
        v3 = v2 and bool(feature(40))  # PF_AVX2_INSTRUCTIONS_AVAILABLE
        described = platform.processor()
        zen12 = "AuthenticAMD" in described and re.search(r"Family 23\b", described) is not None
        return {"x86-64-v2": v2, "x86-64-v3": v3, "x86-64-v3-fast-pext": v3 and not zen12}
    return {"x86-64-v2": True, "x86-64-v3": False, "x86-64-v3-fast-pext": False}


def cpu_meets(requirement):
    if not requirement:
        return True
    flags = cpu_flags()
    return flags is not None and flags.get(requirement, False)


def smoke(engines_dir):
    """Every engine of a staged or installed manifest answers from where it lies. @return The number of builds run."""
    engines_dir = Path(engines_dir)
    manifest = json.loads((engines_dir / MANIFEST).read_text(encoding="utf-8"))
    if manifest.get("format") != FORMAT or not manifest.get("engines"):
        raise PinError(f"{engines_dir / MANIFEST} is no manifest of engines")
    ran = 0
    for entry in manifest["engines"]:
        answered = 0
        for build in entry["builds"]:
            executable = engines_dir / build["executable"]
            if not executable.is_file():
                raise PinError(f"{executable} is not there")
            if os.name != "nt" and not os.access(executable, os.X_OK):
                raise PinError(f"{executable} is not executable")
            if not cpu_meets(build["requires"]):
                print(f"{build['executable']}: this CPU is not {build['requires']}; not started")
                continue
            said = handshake(executable)
            check_version(entry, said)
            print(f"{build['executable']}: uciok, {said['name']} by {said['author']}")
            answered += 1
        if answered == 0:
            raise PinError(f"no build of {entry['id']} runs on this machine")
        ran += answered
    return ran


# ---- The update ---------------------------------------------------------------------------------------------------

def github_json(path):
    request = urllib.request.Request(f"https://api.github.com/{path}",
                                     headers={"Accept": "application/vnd.github+json",
                                              "User-Agent": "reckless-uci-host-packaging"})
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if token:
        request.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def update(pins, cache, latest_release=None):
    """Every pin moved to its repository's latest release, each asset's hash computed from the download and held to
    GitHub's digest and, where the release carries one, its checksums file. @return The lines that changed."""
    latest_release = latest_release or (lambda repository: github_json(f"repos/{repository}/releases/latest"))
    changes = []
    for engine in pins["engines"]:
        release = latest_release(engine["repository"])
        tag = release["tag_name"]
        if not re.fullmatch(r"v\d+\.\d+\.\d+", tag):
            raise PinError(f"{engine['repository']}'s latest release is {tag}, not vMAJOR.MINOR.BUILD")
        version = tag[1:]
        assets = {asset["name"]: asset for asset in release.get("assets", [])}
        moved = dict(engine, tag=tag, version=version)
        for name in ("source_asset", "checksums_asset"):
            pattern = engine.get(name + "_pattern")
            if pattern is not None:
                moved[name] = pattern.format(version=version)
                if moved[name] not in assets:
                    raise PinError(f"{engine['repository']} {tag} has no {moved[name]}")
        sums = {}
        if moved.get("checksums_asset"):
            sums_path = Path(tempfile.mkdtemp()) / moved["checksums_asset"]
            download(asset_url(engine["repository"], tag, moved["checksums_asset"]), sums_path)
            for line in sums_path.read_text().splitlines():
                parts = line.split()
                if len(parts) == 2:
                    sums[parts[1].lstrip("*")] = parts[0].lower()
            shutil.rmtree(sums_path.parent)
        moved["builds"] = {}
        for platform_name, builds in engine["builds"].items():
            moved["builds"][platform_name] = []
            for build in builds:
                asset = build["asset_pattern"].format(version=version)
                if asset not in assets:
                    raise PinError(f"{engine['repository']} {tag} has no {asset}")
                github_digest = (assets[asset].get("digest") or "").removeprefix("sha256:")
                if tag == engine["tag"] and github_digest == build["sha256"] and (
                        not sums or sums.get(asset) == build["sha256"]):
                    # The release already pinned, its asset as it was: GitHub's digest of it (and the release's
                    # checksums file, where it has one) is the pin.
                    moved["builds"][platform_name].append(dict(build))
                    continue
                temporary = Path(tempfile.mkdtemp()) / asset
                download(asset_url(engine["repository"], tag, asset), temporary)
                digest = sha256_of(temporary)
                if github_digest and github_digest != digest:
                    raise PinError(f"{asset}: downloaded as {digest}, GitHub's digest is {github_digest}")
                if sums and sums.get(asset) != digest:
                    raise PinError(f"{asset}: downloaded as {digest}, {moved['checksums_asset']} says {sums.get(asset)}")
                moved_build = dict(build, asset=asset, sha256=digest)
                if "member_pattern" in build:
                    moved_build["member"] = build["member_pattern"].format(version=version)
                    extract(temporary, moved_build["member"], temporary.parent / "check" / "engine")
                cached = Path(cache) / digest / asset
                cached.parent.mkdir(parents=True, exist_ok=True)
                shutil.move(str(temporary), cached)
                shutil.rmtree(temporary.parent, ignore_errors=True)
                moved["builds"][platform_name].append(moved_build)
                if build["sha256"] != digest:
                    changes.append(f"{engine['id']} {engine['version']} -> {version}: {asset} {digest}")
        engine.clear()
        engine.update(moved)
    return changes


def option_value(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        sys.exit(__doc__)
    command = sys.argv[1]
    cache = Path(option_value("--cache", default_cache()))
    try:
        if command == "update":
            pins = load_pins()
            changes = update(pins, cache)
            for line in changes:
                print(line)
            if "--check" in sys.argv:
                if changes:
                    sys.exit("the pins are behind the engines' latest releases: run tools/update-engines.sh")
                print("every pin is at its engine's latest release")
                return
            load_pins_text = json.dumps(pins, indent=2, ensure_ascii=False) + "\n"
            if PINS.read_text(encoding="utf-8") != load_pins_text:
                save_pins(pins)
                print(f"wrote {PINS}: run tools/third_party_notices.py on a build tree next, the notices name each "
                      "engine's version")
            else:
                print("every pin is at its engine's latest release")
        elif command == "fetch":
            platform_name = option_value("--platform")
            for (engine, file), path in fetch(load_pins(), platform_name, cache).items():
                print(f"{engine}/{file}: {path}")
        elif command == "stage":
            manifest = stage(load_pins(), option_value("--platform"), Path(option_value("--out")), cache,
                             "--no-handshake" not in sys.argv, "--leave-out-unfit" in sys.argv)
            print(f"staged {len(manifest['engines'])} engines into {option_value('--out')}")
        elif command == "smoke":
            print(f"{smoke(sys.argv[2])} engine builds answered uci with uciok")
        else:
            sys.exit(__doc__)
    except PinError as error:
        sys.exit(f"bundled_engines.py: {error}")


if __name__ == "__main__":
    main()

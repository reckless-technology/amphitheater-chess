#!/usr/bin/env python3
"""Write THIRD-PARTY-NOTICES.txt: what Reckless UCI Host's binaries contain from elsewhere, each part's licence as it
ships, and the chess engines its installers carry beside it, each a separate program under the GNU General Public
License version 3, with where its corresponding source is.

Every licence text of a linked part is read from the source it governs, in the trees the build fetched
(FETCHCONTENT_BASE_DIR, read from the build tree's CMakeCache.txt, or the tree a FETCHCONTENT_SOURCE_DIR_<NAME> entry
there names in its place), and every version from the toolkit's pins (external/reckless-toolkit/cmake/ToolkitPins.cmake),
the fetched headers or the fonts themselves, so the notices can only say what the build compiles. The parts are the ones
the window's binary holds: Skia and the libraries it bundles, which the window draws through - Skia's own code with its
colour management (skcms), its PNG codec and encoder, libpng and zlib, whose SIMD routines are the Chromium authors',
and on Linux FreeType - and the two typefaces it carries, Noto Serif and Monaspace Xenon. uci-host-cli holds none of
them. The skia-pack carries the licences of libpng, zlib and FreeType, read from it here; Skia's own and the Chromium
source tree's, which the pack lacks, are fetched by the toolkit beside it and read from there. A licence file the pack
gains stops the script until it is read.

The engines are read from the pins (packaging/engines.json): each one's name, version, copyright and the public
repository at its release's tag, with the release's source archive where it has one; the GPLv3's text is
packaging/GPL-3.0.txt, the Free Software Foundation's own, once for all five.

    python3 tools/third_party_notices.py <build-dir>           writes THIRD-PARTY-NOTICES.txt at the root
    python3 tools/third_party_notices.py <build-dir> --check   exits 1 if the committed file is not what it writes
                                                               (CTest notices)

Any build tree with the window on can write the notices or check them. Run it when the submodule moves or a pin of the
engines does (tools/update-engines.sh), and read the diff.
"""
import json
import re
import struct
import sys
import textwrap
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLKIT = ROOT / "external" / "reckless-toolkit"
WIDTH = 100

# Every licence-like file of the skia-pack release: the three the notices reproduce (libpng's, zlib's, FreeType's), the
# GPL FreeType also offers, those of libwebp and HarfBuzz, which nothing links, and those of ANGLE, SwiftShader and the
# Vulkan headers, whose headers alone the pack carries. A file outside the list is a licence the pack has gained, to be
# read before the notices are written again.
SKIA_PACK_LICENCE_FILES = {
    "third_party/externals/libpng/LICENSE", "third_party/externals/zlib/LICENSE",
    "third_party/externals/libwebp/COPYING", "third_party/externals/libwebp/PATENTS",
    "third_party/externals/harfbuzz/COPYING", "third_party/externals/freetype/docs/FTL.TXT",
    "third_party/externals/freetype/docs/GPLv2.TXT", "third_party/externals/angle2/LICENSE",
    "third_party/externals/swiftshader/LICENSE.txt", "include/third_party/vulkan/LICENSE",
}

APP = "Reckless UCI Host (the window)"
INSTALLERS = "the installers, as a separate program beside Reckless UCI Host"


def read(path):
    return Path(path).read_text(encoding="utf-8", errors="strict").replace("\r\n", "\n")


def uncomment(block):
    """A licence quoted in a C or C++ comment, with the comment's markers taken off and its own layout kept."""
    lines = [line.rstrip() for line in block.split("\n")]
    if all(line.lstrip().startswith("//") or not line.strip() for line in lines):
        lines = [re.sub(r"^\s*// ?", "", line) for line in lines]
    else:
        lines[0] = re.sub(r"^\s*/\*+ ?", "", lines[0])
        lines[-1] = re.sub(r"\s*\*+/$", "", lines[-1])
    return textwrap.dedent("\n".join(lines)).strip("\n")


def pins():
    """The toolkit's pins the notices name, from its pins file, which its CMakeLists.txt reads."""
    found = dict(re.findall(r'set\((RECKLESS_[A-Z0-9_]+)\s+"([^"]*)"', read(TOOLKIT / "cmake" / "ToolkitPins.cmake")))
    names = ("RECKLESS_SKIA_TAG", "RECKLESS_SKIA_COMMIT", "RECKLESS_CHROMIUM_COMMIT")
    missing = [n for n in names if n not in found]
    if missing:
        sys.exit(f"the toolkit's pins do not set {missing}")
    return {n: found[n] for n in names}


def fetched_trees(build):
    """A lookup of a fetch's source by its name: the tree a FETCHCONTENT_SOURCE_DIR_<NAME> entry of the cache names in
    the fetch's place, else <FETCHCONTENT_BASE_DIR>/<name>-src."""
    cache = read(Path(build) / "CMakeCache.txt")
    m = re.search(r"^FETCHCONTENT_BASE_DIR:PATH=(.*)$", cache, re.M)
    base = Path(m.group(1).strip()) if m else Path(build) / "_deps"

    def src(name):
        given = re.search(rf"^FETCHCONTENT_SOURCE_DIR_{name.upper()}:PATH=(.*)$", cache, re.M)
        if given and given.group(1).strip():
            return Path(given.group(1).strip())
        return base / f"{name}-src"

    return src


def skia_pack_dir(src):
    """The skia-pack release's root: whichever platform's package the build fetched (each carries the same headers and
    licence files)."""
    for name in ("skia_macos_arm64", "skia_macos_x64", "skia_windows_x64", "skia_linux_x64"):
        if (src(name) / "third_party" / "externals").is_dir():
            return src(name)
    sys.exit("the notices carry the licences of what the window links of the skia-pack, read from the pack, which this "
             "build tree did not fetch: a tree with the window on")


def font_revision(path):
    """A font's revision as its head table records it (a 16.16 fixed-point number), to three decimals."""
    data = Path(path).read_bytes()
    count = struct.unpack(">H", data[4:6])[0]
    for i in range(count):
        tag, _, offset, _ = struct.unpack(">4sIII", data[12 + 16 * i: 28 + 16 * i])
        if tag == b"head":
            return f"{struct.unpack('>i', data[offset + 4: offset + 8])[0] / 65536:.3f}"
    sys.exit(f"{path} has no head table")


def wrap(paragraph):
    words, lines, line = paragraph.split(), [], ""
    for word in words:
        if len(line) + len(word) + (line != "") > WIDTH:
            lines.append(line)
            line = ""
        line += ("" if line == "" else " ") + word
    lines.append(line)
    return "\n".join(lines)


def prose(*paragraphs):
    return "\n\n".join(wrap(p) for p in paragraphs)


def section(number, title, where, *parts):
    rule = "=" * WIDTH
    body = "\n\n".join(p.strip("\n") for p in parts if p)
    return f"{rule}\n{number}. {title}\nUsed in: {where}\n{rule}\n\n{body}\n"


def engine_section(engine, gpl_number):
    repository = f"https://github.com/{engine['repository']}"
    source = [f"The corresponding source of this version is the public repository {repository} at the tag "
              f"{engine['tag']}: {repository}/tree/{engine['tag']}"]
    if engine.get("source_asset"):
        source.append(f"The release also carries it as one archive: "
                      f"{repository}/releases/download/{engine['tag']}/{engine['source_asset']}")
    else:
        source.append(f"GitHub serves it as one archive: {repository}/archive/refs/tags/{engine['tag']}.tar.gz")
    versions = {"GPL-3.0": "version 3 of the License",
                "GPL-3.0-or-later": "either version 3 of the License, or (at your option) any later version"}
    if engine.get("licence") not in versions:
        sys.exit(f"{engine['id']}'s licence is {engine.get('licence')!r}, not one of {sorted(versions)}")
    return prose(
        f"{engine['name']} {engine['version']}, {engine['copyright']}, is free software: you can redistribute it "
        "and/or modify it under the terms of the GNU General Public License as published by the Free Software "
        f"Foundation, {versions[engine['licence']]}. It is distributed in the hope that it will be useful, but WITHOUT "
        "ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See "
        f"the GNU General Public License, section {gpl_number}, for more details.",
        f"The installers carry the executable of its release {engine['tag']} unmodified, as published at "
        f"{repository}/releases/tag/{engine['tag']}. It is a separate program: Reckless UCI Host starts it and "
        "talks to it over the UCI protocol on its standard input and output, and contains none of its code.",
        *source,
        "That source stays available there, without charge, for as long as this version of Reckless UCI Host is "
        "offered for download. You may also ask for it at hello@recklesstechnology.com.")


def build(build_dir):
    p = pins()
    src = fetched_trees(build_dir)
    ofl_noto = read(TOOLKIT / "assets" / "fonts" / "OFL.txt").strip("\n")
    ofl_monaspace = read(TOOLKIT / "assets" / "fonts" / "monaspace" / "OFL.txt").strip("\n")
    noto_version = font_revision(TOOLKIT / "assets" / "fonts" / "NotoSerif-Regular.ttf")
    monaspace_version = font_revision(TOOLKIT / "assets" / "fonts" / "monaspace" / "MonaspaceXenon-Regular.otf")

    pack = skia_pack_dir(src)
    externals = pack / "third_party" / "externals"
    licence_files = {f.relative_to(pack).as_posix() for f in pack.rglob("*")
                     if f.is_file() and re.fullmatch(r"(licen[cs]e|copying|patents|ftl|gplv?\d*)(\.txt|\.md)?",
                                                     f.name, re.I)}
    if licence_files != SKIA_PACK_LICENCE_FILES:
        sys.exit(f"the skia-pack release's licence files are not the ones the notices read - new: "
                 f"{sorted(licence_files - SKIA_PACK_LICENCE_FILES)}, gone: "
                 f"{sorted(SKIA_PACK_LICENCE_FILES - licence_files)}: read them")
    for header in (pack / "include" / "core" / "SkCanvas.h", pack / "modules" / "skcms" / "skcms.h"):
        if "Use of this source code is governed by a BSD-style license" not in read(header):
            sys.exit(f"{header.name} no longer gives its licence as Skia's BSD-style licence: read it")
    for header in ("adler32_simd.h", "crc32_simd.h", "cpu_features.h", "slide_hash_simd.h"):
        if "found in the Chromium source repository LICENSE file" not in read(externals / "zlib" / header):
            sys.exit(f"zlib's {header} no longer points at the Chromium source repository's LICENSE: read it")
    for fetched in ("skia_licence", "chromium_licence"):
        if not (src(fetched) / "LICENSE").is_file():
            sys.exit(f"the toolkit fetches {fetched} beside the skia-pack, and this build tree has the pack without "
                     "it: configure it again")
    skia_licence = read(src("skia_licence") / "LICENSE").strip("\n")
    chromium_licence = uncomment(read(src("chromium_licence") / "LICENSE"))
    skia_milestone = re.search(r"#define SK_MILESTONE (\d+)",
                               read(pack / "include" / "core" / "SkMilestone.h")).group(1)
    skia_png_version = re.search(r'#define PNG_LIBPNG_VER_STRING "([^"]+)"',
                                 read(externals / "libpng" / "png.h")).group(1)
    skia_zlib_version = re.search(r'#define ZLIB_VERSION "([^"]+)"', read(externals / "zlib" / "zlib.h")).group(1)
    freetype_header = read(externals / "freetype" / "include" / "freetype" / "freetype.h")
    freetype_version = ".".join(re.search(rf"#define FREETYPE_{part}\s+(\d+)", freetype_header).group(1)
                                for part in ("MAJOR", "MINOR", "PATCH"))
    freetype_year = re.search(r"Copyright \(C\) \d{4}-(\d{4}) by", freetype_header).group(1)
    ftl = read(externals / "freetype" / "docs" / "FTL.TXT")
    freetype_credit = " ".join(re.search(r'"""\s*(Portions of this software.*?)\s*"""', ftl, re.S).group(1).split())
    freetype_credit = freetype_credit.replace("<year>", freetype_year)

    engines = json.loads(read(ROOT / "packaging" / "engines.json"))["engines"]
    gpl = read(ROOT / "packaging" / "GPL-3.0.txt").strip("\n")
    if "GNU GENERAL PUBLIC LICENSE\n                       Version 3, 29 June 2007" not in gpl:
        sys.exit("packaging/GPL-3.0.txt is not the GNU General Public License version 3")

    # The sections in their order, each numbered by its place.
    companions = [(engine, platform_name, companion) for engine in engines
                  for platform_name, listed in engine.get("companions", {}).items() for companion in listed]
    keys = ["noto", "monaspace", "skia", "png_skia", "zlib_skia", "freetype"] + \
        [f"engine:{e['id']}" for e in engines] + [f"companion:{c['file']}" for _, _, c in companions] + ["gpl"]
    n = {key: i + 1 for i, key in enumerate(keys)}
    sections = {
        "noto": (f"Noto Serif {noto_version}", APP, ofl_noto),
        "monaspace": (f"Monaspace Xenon {monaspace_version}", APP, ofl_monaspace),
        "skia": (f"Skia m{skia_milestone}, skia-pack {p['RECKLESS_SKIA_TAG']}", APP, prose(
            f"Reckless UCI Host draws its window through Skia m{skia_milestone}, linked from the prebuilt static "
            f"libraries of the skia-pack release {p['RECKLESS_SKIA_TAG']}, built from Skia's commit "
            f"{p['RECKLESS_SKIA_COMMIT']}. It holds Skia's own code - its raster renderer, its colour management "
            "(skcms), its PNG codec and its PNG encoder - and, of the libraries the release bundles, libpng and zlib "
            f"(sections {n['png_skia']} and {n['zlib_skia']}), and on Linux FreeType (section {n['freetype']}), "
            "which draws the text there; it declines every other image decoder, so no other bundled library is "
            "linked.",
            "Skia's licence, the LICENSE file of its source tree at that commit, which skcms, a part of the same tree, "
            "shares:"), skia_licence),
        "png_skia": (f"libpng {skia_png_version}, in Skia", APP, read(externals / "libpng" / "LICENSE")),
        "zlib_skia": (f"zlib {skia_zlib_version}, in Skia", APP, read(externals / "zlib" / "LICENSE"), prose(
            "Its SIMD routines (adler32_simd, crc32_simd and crc_folding, inffast_chunk, cpu_features and "
            "slide_hash_simd) are the Chromium authors', under the LICENSE file of the Chromium source tree at "
            f"revision {p['RECKLESS_CHROMIUM_COMMIT']}, the revision the zlib commit Skia's DEPS names was copied "
            "from:"), chromium_licence),
        "freetype": (f"FreeType {freetype_version}, in Skia", f"{APP} on Linux", prose(
            "This software is based in part on the work of the FreeType Team. FreeType is used under the FreeType "
            "License, the first of the two licences it offers.", freetype_credit), ftl),
        "gpl": ("The GNU General Public License, version 3", "every engine above", gpl),
    }
    for engine in engines:
        sections[f"engine:{engine['id']}"] = (f"{engine['name']} {engine['version']}, a chess engine", INSTALLERS,
                                              engine_section(engine, n["gpl"]))
    platform_names = {"windows": "Windows", "linux": "Linux", "macos": "macOS"}
    for engine, platform_name, companion in companions:
        sections[f"companion:{companion['file']}"] = (
            f"{companion['file']}, beside {engine['name']}",
            f"the installers on {platform_names[platform_name]}, beside {engine['name']}",
            prose(f"{companion['what'][0].upper()}{companion['what'][1:]}, taken unmodified from the release "
                  f"{companion['tag']} of https://github.com/{companion['repository']} "
                  f"({companion['asset']}), and installed beside {engine['name']}'s executable, which loads it. Its "
                  "licence, as that release carries it:"),
            read(ROOT / companion["licence"]).strip("\n"))

    rows = []
    for key in keys:
        title, used = sections[key][0], sections[key][1]
        rows.append(f"  {n[key]:>2}. {title.split(',')[0]:<44} {used.split(',')[0]}")
    engine_names = ", ".join(f"{e['name']} {e['version']}" for e in engines)
    head = ("THIRD-PARTY NOTICES\n\n" + prose(
        "Reckless UCI Host and uci-host-cli are copyright (C) 2026 Jonny Reckless and licensed under EULA.txt. The "
        "window contains the third-party software below, each part used under its own licence, which this file "
        "reproduces wherever the licence asks for it. Nothing in EULA.txt limits any right these licences give you.",
        f"The installers also carry five chess engines - {engine_names} - each a separate program, licensed under the "
        f"GNU General Public License version 3 (section {n['gpl']}). Reckless UCI Host only starts them and "
        "talks to them over the UCI protocol; it contains none of their code, and EULA.txt does not cover them. Each "
        "engine's section says where its corresponding source is.") +
        "\n\nWhere each part is used\n\n" + "\n".join(rows) + "\n")
    parts = [head] + [section(n[key], *sections[key]) for key in keys]
    return "\n\n".join(s.rstrip("\n") for s in parts) + "\n"


def main():
    if len(sys.argv) < 2 or sys.argv[1].startswith("--"):
        sys.exit(__doc__)
    text = build(sys.argv[1])
    out = ROOT / "THIRD-PARTY-NOTICES.txt"
    if "--check" in sys.argv:
        current = out.read_text(encoding="utf-8").replace("\r\n", "\n") if out.exists() else ""
        if current != text:
            sys.exit(f"{out.name} is not what {Path(__file__).name} writes from {sys.argv[1]}: run it again")
        print(f"{out.name} is current")
        return
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"wrote {out} ({len(text.splitlines())} lines)")


if __name__ == "__main__":
    main()

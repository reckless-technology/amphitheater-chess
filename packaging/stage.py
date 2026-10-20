#!/usr/bin/env python3
"""What a release ships, staged from a release build tree into a folder of its own: the window and the command-line
tool as the build made them, the bundled engines (tools/bundled_engines.py stage: each pinned release's binary, held to
its hash, and the manifest the host seeds a person's engine list from), the end-user licence and the third-party
notices. Everything after it - the archive, the installers, the signing - reads the stage, never the build tree.

    python3 -I packaging/stage.py <build-dir> <dist-dir> linux|macos|windows [--cache DIR] [--leave-out-unfit]

    windows  Amphitheater.exe, amphitheater-cli.exe, engines/<engine>/..., EULA.txt, THIRD-PARTY-NOTICES.txt
    linux    amphitheater, amphitheater-cli, engines/<engine>/..., EULA.txt, THIRD-PARTY-NOTICES.txt - the portable
             archive as it is, and what the Debian package lays out (the engines under /usr/lib/reckless-amphitheater)
    macos    Amphitheater.app with the engines in Contents/Resources/engines/<engine>/ (the bundle copied with
             ditto and signed again ad hoc, since files added to it break the build's signature; a release signs it
             with the Developer ID after, packaging/macos/sign.sh), amphitheater-cli, EULA.txt, THIRD-PARTY-NOTICES.txt

The folder is emptied first; a binary the build did not make stops it before anything is copied. Every engine the stage
lays out answers its handshake on this machine before the stage ends (both of Zenith's x86-64 builds where the CPU
takes both), so a stage holds only engines that run; an engine that needs a library a fresh machine lacks stops the
stage, or with --leave-out-unfit (a pull request's run, never a release) is left out of it with a warning. The cache is
tools/bundled_engines.py's.
"""
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import bundled_engines  # noqa: E402

BINARIES = {
    "linux": ["src/app/amphitheater", "src/cli/amphitheater-cli"],
    "macos": ["src/app/Amphitheater.app", "src/cli/amphitheater-cli"],
    "windows": ["src/app/Amphitheater.exe", "src/cli/amphitheater-cli.exe"],
}


def engines_folder(dist, platform_name):
    """Where a platform's stage holds the engines."""
    if platform_name == "macos":
        return dist / "Amphitheater.app" / "Contents" / "Resources" / "engines"
    return dist / "engines"


def main():
    arguments = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(arguments) < 3 or arguments[2] not in BINARIES:
        sys.exit(__doc__)
    build, dist, platform_name = Path(arguments[0]), Path(arguments[1]), arguments[2]
    cache = Path(sys.argv[sys.argv.index("--cache") + 1]) if "--cache" in sys.argv else bundled_engines.default_cache()
    for file in BINARIES[platform_name]:
        if not (build / file).exists():
            sys.exit(f"stage.py: {build / file} is not there: build the release tree first")
    if dist.exists():
        shutil.rmtree(dist)
    dist.mkdir(parents=True)
    for file in BINARIES[platform_name]:
        source = build / file
        if platform_name == "macos":
            subprocess.run(["ditto", str(source), str(dist / source.name)], check=True)
        else:
            shutil.copy2(source, dist / source.name)
    for document in ("EULA.txt", "THIRD-PARTY-NOTICES.txt"):
        shutil.copyfile(ROOT / document, dist / document)
    try:
        bundled_engines.stage(bundled_engines.load_pins(), platform_name, engines_folder(dist, platform_name), cache,
                              do_handshake=True, leave_out_unfit="--leave-out-unfit" in sys.argv)
    except bundled_engines.PinError as error:
        sys.exit(f"stage.py: {error}")
    if platform_name == "macos":
        # The build's ad-hoc signature sealed the bundle as it was; the engines added to its resources are sealed again.
        app = dist / "Amphitheater.app"
        subprocess.run(["codesign", "--force", "--sign", "-", str(app)], check=True)
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)
    print(f"staged into {dist}:")
    for path in sorted(dist.rglob("*")):
        relative = path.relative_to(dist).as_posix()
        is_listed = ".app/" not in relative or "/Resources/engines/" in relative or relative.endswith("/MacOS/Amphitheater")
        if path.is_file() and is_listed:
            print(f"  {relative}  {path.stat().st_size} bytes")


if __name__ == "__main__":
    main()

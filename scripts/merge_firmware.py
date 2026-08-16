"""
Post-build script: merge bootloader + partitions + firmware into a single
flashable image, copy it to the firmware project root, and emit a SHA256
sidecar file the web flasher can use to verify the download before
flashing.

WHY THIS EXISTS:
v0.5.0 was initially shipped by uploading `.pio/build/gh_release/firmware.bin`
directly to the web flasher. That file is ONLY the application — no bootloader,
no partition table. The web flasher wrote it to offset 0x0, clobbering the
bootloader region, and bricked every device that ran the update.

This script eliminates that failure mode: after every build it produces a
properly merged `sumi-v<VERSION>-full.bin` sitting right next to platformio.ini.
That is the ONLY file that should ever be shipped to the web flasher.

The SHA256 sidecar (`sumi-v<VERSION>-full.bin.sha256`, audit #52) closes
the second failure mode: a network mirror corrupting a single byte of
the download silently bricks devices. The web flasher should verify the
sidecar before writing.

The raw `firmware.bin` stays buried in `.pio/build/<env>/` where nobody reaches
for it by accident.
"""
import hashlib
import os
import shutil
import subprocess
import sys

Import("env")  # noqa: F821 - provided by PlatformIO


def _extract_version(scons_env) -> str:
    """Pull SUMI_VERSION out of CPPDEFINES (it is set per-env in platformio.ini)."""
    for define in scons_env.get("CPPDEFINES", []):
        if isinstance(define, (list, tuple)) and len(define) == 2 and define[0] == "SUMI_VERSION":
            # Value arrives with escaped quotes: '\\"0.5.0\\"' — scrub both.
            # str.strip() only peels from the ends and can leave a stray
            # backslash when the pattern is \"X\"; replace handles it cleanly.
            return str(define[1]).replace('\\', '').replace('"', '').strip()
    return "unknown"


# SCons post-action callbacks are invoked with (target, source, env) by name,
# so the parameter MUST be called `env` even though the module-level `env`
# is also in scope from `Import("env")`. The local parameter shadows it.
def merge_firmware(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    project_dir = env.subst("$PROJECT_DIR")
    python_exe = env.subst("$PYTHONEXE")
    version = _extract_version(env)

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware = os.path.join(build_dir, "firmware.bin")

    for required in (bootloader, partitions, firmware):
        if not os.path.isfile(required):
            print(f"[merge_firmware] SKIP — missing {required}")
            return

    merged_in_build = os.path.join(build_dir, "sumi-full.bin")
    merged_in_root = os.path.join(project_dir, f"sumi-v{version}-full.bin")

    # Use the env's configured flash size rather than hard-coding 16 MB — the
    # the emulator env declares 4 MB because the the emulator emulator's DevKitM-1 only has
    # that much flash, and esptool merge-bin will refuse if partitions
    # overflow the declared size.
    flash_size_mb = env.BoardConfig().get("build.flash_size", "16MB")

    # `python_exe -m esptool` assumes esptool is importable from that
    # interpreter. On Windows under msys2, $PYTHONEXE often resolves to
    # the msys2 system python which has no esptool installed, so fall
    # back to invoking the esptool.py script bundled in PIO's
    # tool-esptoolpy package. PIO already downloads that tool as part
    # of the espressif32 platform, so this path always exists.
    esptool_py = os.path.join(
        env.PioPlatform().get_package_dir("tool-esptoolpy") or "",
        "esptool.py",
    )
    # Use the bundled esptool.py — but it's an older release that still
    # uses `merge_bin` (underscore). The newer PyPI esptool renamed it to
    # `merge-bin` (dash) so we only fall back to `python -m esptool` if
    # the bundled script is missing.
    # Older bundled esptool.py uses `merge_bin` with underscore flags
    # (--flash_mode/--flash_freq/--flash_size). Newer PyPI esptool uses
    # `merge-bin` with dashed flags. Pick the right incantation based on
    # which one we're calling.
    if os.path.isfile(esptool_py):
        cmd = [python_exe, esptool_py]
        merge_subcmd = "merge_bin"
        fm_flag, ff_flag, fs_flag = "--flash_mode", "--flash_freq", "--flash_size"
    else:
        cmd = [python_exe, "-m", "esptool"]
        merge_subcmd = "merge-bin"
        fm_flag, ff_flag, fs_flag = "--flash-mode", "--flash-freq", "--flash-size"
    cmd += [
        "--chip", "esp32c3",
        merge_subcmd,
        "-o", merged_in_build,
        fm_flag, "dio",
        ff_flag, "80m",
        fs_flag, flash_size_mb,
        "0x0", bootloader,
        "0x8000", partitions,
        "0x10000", firmware,
    ]

    print(f"[merge_firmware] Merging bootloader + partitions + firmware -> {merged_in_build}")
    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as exc:
        print(f"[merge_firmware] esptool merge-bin FAILED: {exc}", file=sys.stderr)
        env.Exit(1)  # fail the build — do not let a broken release slip through
        return

    shutil.copy2(merged_in_build, merged_in_root)

    # SHA256 sidecar (audit #52). Same on-disk shape as `sha256sum > <file>`
    # so standard tools and the web flasher can verify it without bespoke
    # parsing.
    sha = hashlib.sha256()
    with open(merged_in_root, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            sha.update(chunk)
    digest = sha.hexdigest()
    sha_path = merged_in_root + ".sha256"
    # newline="" + explicit LF matches `sha256sum` output exactly across
    # OS — Python's default text-mode write would translate \n → \r\n on
    # Windows, and `sha256sum -c` reads the trailing CR as part of the
    # filename and reports "no such file".
    with open(sha_path, "w", encoding="utf-8", newline="") as out:
        # Two-space separator + filename (sans path) matches sha256sum's
        # default output format.
        out.write(f"{digest}  {os.path.basename(merged_in_root)}\n")

    size_kb = os.path.getsize(merged_in_root) // 1024
    banner = "=" * 72
    print()
    print(banner)
    print(f" MERGED FIRMWARE READY  ({size_kb} KB)")
    print(f"   {merged_in_root}")
    print(f"   sha256: {digest}")
    print()
    print(" SHIP THIS FILE to the web flasher.")
    print(" DO NOT SHIP .pio/build/*/firmware.bin — that will brick devices.")
    print(banner)
    print()


# Hook into the final program build step.
env.AddPostAction("buildprog", merge_firmware)  # noqa: F821

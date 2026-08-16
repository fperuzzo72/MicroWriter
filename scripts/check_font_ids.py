"""
Pre-build hook: detect drift between the literal font IDs in
`src/config.h` and the IDs that would be derived from the actual font
binaries in `lib/EpdFont/builtinFonts/`. Logs a warning if they differ.

Audit #54 background:
The IDs are used as cache keys for /.sumi/cache/<book>/pages_<fontId>.bin
— so changing a literal invalidates every user's existing EPUB cache.
We do NOT auto-regenerate the header at build time for that reason.
Instead, this script flags drift so a maintainer who updates a font
binary sees the warning and can decide whether to re-issue the IDs
(accepting the migration cost) or keep them pinned.

Algorithm (preserved from the Ruby snippet that originally seeded the
literals; must match exactly so a clean recompute matches in either
language):

    for each font file in a group:
        h = SHA256(file_contents) interpreted as a 256-bit integer
        total += h
    id = (total mod 2^32) - 2^31

Usage:
    python scripts/check_font_ids.py             # check + warn
    python scripts/check_font_ids.py --regenerate  # rewrite src/config.h

The PIO pre-build hook calls main() which only checks. The --regenerate
path is for a maintainer running the script by hand after deliberately
updating a font binary; it edits the literal block in src/config.h and
prints the new values for inclusion in the release notes.
"""
import hashlib
import os
import re
import sys

# --- Group definitions (must match the comments-in-config.h originals) ---
FONT_GROUPS = [
    ("READER_FONT_ID_XSMALL", [
        "lib/EpdFont/builtinFonts/reader_xsmall_regular_2b.h",
        "lib/EpdFont/builtinFonts/reader_xsmall_bold_2b.h",
        "lib/EpdFont/builtinFonts/reader_xsmall_italic_2b.h",
    ]),
    ("READER_FONT_ID", [
        "lib/EpdFont/builtinFonts/reader_2b.h",
        "lib/EpdFont/builtinFonts/reader_bold_2b.h",
        "lib/EpdFont/builtinFonts/reader_italic_2b.h",
    ]),
    ("READER_FONT_ID_MEDIUM", [
        "lib/EpdFont/builtinFonts/reader_medium_2b.h",
        "lib/EpdFont/builtinFonts/reader_medium_bold_2b.h",
        "lib/EpdFont/builtinFonts/reader_medium_italic_2b.h",
    ]),
    ("READER_FONT_ID_LARGE", [
        "lib/EpdFont/builtinFonts/reader_large_2b.h",
        "lib/EpdFont/builtinFonts/reader_large_bold_2b.h",
        "lib/EpdFont/builtinFonts/reader_large_italic_2b.h",
    ]),
    ("UI_FONT_ID", [
        "lib/EpdFont/builtinFonts/ui_12.h",
        "lib/EpdFont/builtinFonts/ui_bold_12.h",
    ]),
    ("SMALL_FONT_ID", [
        "lib/EpdFont/builtinFonts/small14.h",
    ]),
]

CONFIG_HEADER = "src/config.h"


def compute_font_id(paths, project_dir):
    total = 0
    for rel_path in paths:
        abs_path = os.path.join(project_dir, rel_path)
        if not os.path.isfile(abs_path):
            return None  # missing input — skip the check
        with open(abs_path, "rb") as f:
            total += int(hashlib.sha256(f.read()).hexdigest(), 16)
    return (total % (1 << 32)) - (1 << 31)


_DEFINE_RE = re.compile(r'^\s*#define\s+(\w+)\s+(\(?-?\d+\)?)', re.MULTILINE)


def parse_literals(config_path):
    """Pull `#define <NAME> <num>` from config.h, normalising the optional
    parens around negatives. Returns dict[name] -> int."""
    with open(config_path, "r", encoding="utf-8") as f:
        text = f.read()
    out = {}
    for m in _DEFINE_RE.finditer(text):
        name = m.group(1)
        raw = m.group(2)
        try:
            out[name] = int(raw.strip("()"))
        except ValueError:
            pass
    return out


def regenerate(config_path, ids):
    """Rewrite the IDs in src/config.h to match `ids` (list of (name,
    value)). Surgical edit — only touches lines that match
    `#define <NAME> ...` for a name in `ids`."""
    with open(config_path, "r", encoding="utf-8") as f:
        text = f.read()
    for name, value in ids:
        formatted = f"({value})" if value < 0 else str(value)
        new_text, n = re.subn(
            rf'^(\s*#define\s+{re.escape(name)}\s+)\(?-?\d+\)?',
            rf'\1{formatted}',
            text,
            flags=re.MULTILINE,
        )
        if n == 0:
            print(f"[font-ids] WARN: no #define {name} found, skipping",
                  file=sys.stderr)
        text = new_text
    with open(config_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def project_dir_from_env_or_cwd():
    # Prefer SCons env (when called as a PIO pre-build hook) but also
    # work as a stand-alone script from any cwd inside the project.
    if "env" in globals():
        try:
            return env.subst("$PROJECT_DIR")  # noqa: F821
        except Exception:
            pass
    # Stand-alone path. SCons exec doesn't define __file__, so guard.
    file_attr = globals().get("__file__")
    if file_attr:
        here = os.path.dirname(os.path.abspath(file_attr))
        return os.path.dirname(here)
    return os.getcwd()


def main(regen=False):
    project_dir = project_dir_from_env_or_cwd()
    config_path = os.path.join(project_dir, CONFIG_HEADER)
    literals = parse_literals(config_path) if os.path.isfile(config_path) else {}

    drifted = []
    computed_ids = []
    for name, paths in FONT_GROUPS:
        derived = compute_font_id(paths, project_dir)
        if derived is None:
            continue  # missing files — skip silently
        computed_ids.append((name, derived))
        existing = literals.get(name)
        if existing is None:
            drifted.append((name, "missing", derived))
        elif existing != derived:
            drifted.append((name, existing, derived))

    if regen:
        if not computed_ids:
            print("[font-ids] No font groups had complete inputs; nothing to regenerate.",
                  file=sys.stderr)
            return
        regenerate(config_path, computed_ids)
        print("[font-ids] Regenerated literals in src/config.h:")
        for name, value in computed_ids:
            print(f"[font-ids]   {name} = {value}")
        print("[font-ids] WARNING: this invalidates every existing user's")
        print("[font-ids] EPUB cache (cache filename embeds the font ID).")
        print("[font-ids] Document this in the release notes for the next ship.")
        return

    if drifted:
        print()
        print("[font-ids] WARN: drift detected between src/config.h literals and")
        print("[font-ids]       binary-derived IDs from lib/EpdFont/builtinFonts/.")
        print("[font-ids]       This means a font binary was updated without")
        print("[font-ids]       re-issuing the macro — runtime fonts still work")
        print("[font-ids]       (registration and lookup use the same literal),")
        print("[font-ids]       but cached glyphs may not match the binaries.")
        print("[font-ids]       To accept and regenerate (cache invalidation):")
        print("[font-ids]           python scripts/check_font_ids.py --regenerate")
        for name, existing, derived in drifted:
            print(f"[font-ids]       {name}: existing={existing}  derived={derived}")
        print()


# Determine invocation mode. When SCons execs us via the PIO pre-build
# hook, sys.argv is the pio invocation (no `check_font_ids.py` in
# argv[0]) and __name__ is the SCons-internal name, not "__main__".
# Distinguish by checking whether argv[0] points at this script.
_argv0 = (sys.argv[0] if sys.argv else "")
_invoked_as_script = "check_font_ids" in os.path.basename(_argv0)
main(regen=(_invoked_as_script and "--regenerate" in sys.argv))

#!/usr/bin/env python3
"""Discover, select, and flash Comtel-sign plugins.

Each plugin is a self-contained sketch at src/plugins/<id>.cpp (see
openspec / README). This tool lists the plugins, keeps one PlatformIO
env per plugin in sync inside a marked region of platformio.ini, and
runs `pio run -e <id> -t upload` for the chosen plugin.

Stdlib only. Usage:
    python tools/flash.py                 # menu -> pick -> build + upload
    python tools/flash.py <id>            # skip the menu (scriptable)
    python tools/flash.py --sync          # regenerate the env block and exit
    python tools/flash.py <id> --no-upload    # compile check, no upload
    python tools/flash.py <id> --port COM4    # explicit upload port
"""
import argparse
import glob
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLUGINS_DIR = os.path.join(REPO, "src", "plugins")
INI = os.path.join(REPO, "platformio.ini")

BEGIN = "; >>> BEGIN generated envs — regenerate with tools/flash.py --sync; do not edit by hand"
END = "; <<< END generated"

DESCRIPTOR_RE = re.compile(r"//\s*@plugin:\s*(.+?)\s*$")


def discover_plugins():
    """Return [(id, label)] for every src/plugins/*.cpp, sorted by id."""
    plugins = []
    for path in sorted(glob.glob(os.path.join(PLUGINS_DIR, "*.cpp"))):
        pid = os.path.splitext(os.path.basename(path))[0]
        label = ""
        # descriptor comment lives in the first few lines
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                for _ in range(10):
                    line = f.readline()
                    if not line:
                        break
                    m = DESCRIPTOR_RE.search(line)
                    if m:
                        label = m.group(1)
                        break
        except OSError:
            pass
        plugins.append((pid, label))
    return plugins


def render_block(plugins):
    """Render the marked env region (markers included) for the plugin set."""
    lines = [BEGIN]
    for pid, _ in plugins:
        lines.append("[env:%s]" % pid)
        lines.append(
            "build_src_filter = -<*> +<driver/> +<plugins/%s.cpp>" % pid)
        lines.append("lib_deps = adafruit/Adafruit GFX Library")
        lines.append("")
    lines.append(END)
    return "\n".join(lines)


def sync_env_block(plugins, verbose=False):
    """Rewrite the marked region if it differs from the plugin set.

    Everything outside the markers is preserved byte-for-byte. Returns True
    if the file was rewritten. If the markers are missing, the block is
    appended to the end of the file.
    """
    with open(INI, "r", encoding="utf-8") as f:
        text = f.read()

    new_block = render_block(plugins)
    start = text.find(BEGIN)
    end = text.find(END)

    if start != -1 and end != -1 and end > start:
        end += len(END)
        old_block = text[start:end]
        if old_block == new_block:
            return False
        updated = text[:start] + new_block + text[end:]
    else:
        # no markers yet: append the block
        sep = "" if text.endswith("\n\n") else ("\n" if text.endswith("\n") else "\n\n")
        updated = text + sep + new_block + "\n"

    with open(INI, "w", encoding="utf-8") as f:
        f.write(updated)
    if verbose:
        print("platformio.ini: regenerated env block for %d plugin(s)."
              % len(plugins))
    return True


def resolve_pio():
    """Locate the pio executable: PATH first, then the PlatformIO penv."""
    from shutil import which
    found = which("pio")
    if found:
        return found
    home = os.path.expanduser("~")
    if os.name == "nt":
        cand = os.path.join(home, ".platformio", "penv", "Scripts", "pio.exe")
    else:
        cand = os.path.join(home, ".platformio", "penv", "bin", "pio")
    if os.path.isfile(cand):
        return cand
    return None


def choose_plugin(plugins):
    """Print a numbered menu and return the chosen plugin id, or None."""
    print("Available plugins:")
    for i, (pid, label) in enumerate(plugins, 1):
        suffix = "  - %s" % label if label else ""
        print("  %d) %s%s" % (i, pid, suffix))
    try:
        raw = input("Select a plugin to flash [1-%d]: " % len(plugins)).strip()
    except (EOFError, KeyboardInterrupt):
        print()
        return None
    if not raw:
        return None
    if raw.isdigit() and 1 <= int(raw) <= len(plugins):
        return plugins[int(raw) - 1][0]
    # also accept a plugin id typed directly
    for pid, _ in plugins:
        if raw == pid:
            return pid
    print("Not a valid selection: %r" % raw, file=sys.stderr)
    return None


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Discover, select, and flash Comtel-sign plugins.")
    parser.add_argument("plugin", nargs="?",
                        help="plugin id to flash (skips the menu)")
    parser.add_argument("--sync", action="store_true",
                        help="regenerate the platformio.ini env block and exit")
    parser.add_argument("--port",
                        help="upload port (passed as --upload-port); "
                             "omit to let PlatformIO auto-detect")
    parser.add_argument("--no-upload", action="store_true",
                        help="build only, do not upload")
    args = parser.parse_args(argv)

    plugins = discover_plugins()

    if args.sync:
        changed = sync_env_block(plugins, verbose=True)
        if not changed:
            print("platformio.ini: env block already up to date (%d plugin(s))."
                  % len(plugins))
        return 0

    if not plugins:
        print("No plugins found in src/plugins/. Add a <name>.cpp there first.",
              file=sys.stderr)
        return 1

    # keep the env block matching the directory before every build
    sync_env_block(plugins, verbose=True)

    ids = [pid for pid, _ in plugins]
    if args.plugin:
        target = args.plugin
        if target not in ids:
            print("Unknown plugin %r. Available: %s"
                  % (target, ", ".join(ids)), file=sys.stderr)
            return 1
    else:
        target = choose_plugin(plugins)
        if not target:
            print("Nothing selected.", file=sys.stderr)
            return 1

    pio = resolve_pio()
    if not pio:
        print("Could not find the 'pio' executable. Run this from the "
              "PlatformIO IDE terminal, or ensure PlatformIO is installed "
              "(expected ~/.platformio/penv/).", file=sys.stderr)
        return 1

    cmd = [pio, "run", "-e", target]
    if not args.no_upload:
        cmd += ["-t", "upload"]
        if args.port:
            cmd += ["--upload-port", args.port]
    print("Running: %s" % " ".join(cmd))
    result = subprocess.run(cmd, cwd=REPO)
    if result.returncode != 0:
        print("%s failed (exit %d)."
              % ("Build" if args.no_upload else "Build/upload",
                 result.returncode), file=sys.stderr)
        return result.returncode
    print("Done: %s %s." % (target, "built" if args.no_upload else "flashed"))
    return 0


if __name__ == "__main__":
    sys.exit(main())

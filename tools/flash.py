#!/usr/bin/env python3
"""Discover, select, and flash Comtel-sign firmware.

Each plugin is one file at src/plugins/<id>.cpp implementing the plugin-host
interface (see README). Two kinds of image can be built:

  launcher  the host with WiFi, the web control page and OTA, plus every
            plugin; pick the running plugin from http://<hostname>.local
  <id>      a solo image: the host without networking plus that one plugin

This tool lists the targets, keeps the generated PlatformIO envs in sync
inside a marked region of platformio.ini, and builds/uploads the chosen one.

Stdlib only. Usage:
    python tools/flash.py                     # menu -> pick -> build + upload
    python tools/flash.py launcher            # skip the menu (scriptable)
    python tools/flash.py <id>                # a solo plugin image
    python tools/flash.py --sync              # regenerate the env block and exit
    python tools/flash.py <target> --no-upload    # compile check, no upload
    python tools/flash.py <target> --port COM4    # explicit USB upload port
    python tools/flash.py launcher --ota [host]   # upload over WiFi (espota);
                                                  # host defaults to
                                                  # <COMTEL_HOSTNAME>.local
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
SECRETS = os.path.join(REPO, "include", "secrets.h")

LAUNCHER = "launcher"
LAUNCHER_LABEL = "All plugins + web control page + OTA"

BEGIN = "; >>> BEGIN generated envs — regenerate with tools/flash.py --sync; do not edit by hand"
END = "; <<< END generated"

# Pinned so a library release can't silently break the launcher build.
# AsyncTCP is ESPAsyncWebServer's own dependency, pinned here as well.
WEB_LIBS = [
    "mathieucarbou/ESPAsyncWebServer @ 3.6.0",
    "mathieucarbou/AsyncTCP @ 3.3.2",
    "bblanchon/ArduinoJson @ 7.4.3",
]

# The AsyncTCP task (web server) runs on core 0 with the WiFi stack, so it
# never competes with the display scan on the main loop (core 1).
LAUNCHER_FLAGS = "-DCOMTEL_WEB=1 -DCONFIG_ASYNC_TCP_RUNNING_CORE=0"

DESCRIPTOR_RE = re.compile(r"//\s*@plugin:\s*(.+?)\s*$")
REGISTER_RE = re.compile(
    r"COMTEL_PLUGIN\(\s*(\w+)\s*,\s*\"((?:[^\"\\]|\\.)*)\"", re.S)
DEFINE_RE = r"^\s*#\s*define\s+%s\s+\"((?:[^\"\\]|\\.)*)\""


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


def lint_plugins(plugins):
    """Warn when a plugin's COMTEL_PLUGIN id/label disagree with its file.

    The id must equal the filename stem; the label must equal the
    `// @plugin:` text (or the id, when there is no descriptor comment).
    Returns the number of warnings printed.
    """
    warnings = 0
    for pid, label in plugins:
        path = os.path.join(PLUGINS_DIR, pid + ".cpp")
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                text = f.read()
        except OSError:
            continue
        m = REGISTER_RE.search(text)
        if not m:
            print("warning: %s.cpp has no COMTEL_PLUGIN(...) registration"
                  % pid, file=sys.stderr)
            warnings += 1
            continue
        reg_id, reg_label = m.group(1), m.group(2)
        if reg_id != pid:
            print("warning: %s.cpp registers id %r; it must equal the "
                  "filename stem %r" % (pid, reg_id, pid), file=sys.stderr)
            warnings += 1
        expected = label or pid
        if reg_label != expected:
            print("warning: %s.cpp registers label %r but its // @plugin: "
                  "comment says %r" % (pid, reg_label, expected),
                  file=sys.stderr)
            warnings += 1
    return warnings


def render_block(plugins):
    """Render the marked env region (markers included) for the plugin set."""
    lines = [
        BEGIN,
        "[comtel_base]",
        "build_src_filter = -<*> +<driver/> +<host/>",
        "lib_deps = adafruit/Adafruit GFX Library",
        "board_build.partitions = min_spiffs.csv",
        "",
        "[env:%s]" % LAUNCHER,
        "extends = comtel_base",
        "build_src_filter = ${comtel_base.build_src_filter} +<plugins/>",
        "build_flags = %s" % LAUNCHER_FLAGS,
        "lib_deps = ${comtel_base.lib_deps}",
    ]
    lines += ["  %s" % lib for lib in WEB_LIBS]
    lines.append("")
    for pid, _ in plugins:
        lines.append("[env:%s]" % pid)
        lines.append("extends = comtel_base")
        lines.append("build_src_filter = ${comtel_base.build_src_filter} "
                     "+<plugins/%s.cpp>" % pid)
        lines.append("build_flags = -DCOMTEL_WEB=0")
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


def resolve_espota():
    """Locate espota.py in the installed Arduino-ESP32 framework package."""
    core = os.environ.get("PLATFORMIO_CORE_DIR",
                          os.path.join(os.path.expanduser("~"), ".platformio"))
    cand = os.path.join(core, "packages", "framework-arduinoespressif32",
                        "tools", "espota.py")
    return cand if os.path.isfile(cand) else None


def read_secret(name):
    """Return the string value of `#define <name> "..."` in secrets.h, or None."""
    try:
        with open(SECRETS, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError:
        return None
    m = re.search(DEFINE_RE % re.escape(name), text, re.M)
    return m.group(1) if m else None


def choose_target(targets):
    """Print a numbered menu and return the chosen target id, or None."""
    print("Available targets:")
    for i, (pid, label) in enumerate(targets, 1):
        suffix = "  - %s" % label if label else ""
        print("  %d) %s%s" % (i, pid, suffix))
    try:
        raw = input("Select a target to flash [1-%d]: " % len(targets)).strip()
    except (EOFError, KeyboardInterrupt):
        print()
        return None
    if not raw:
        return None
    if raw.isdigit() and 1 <= int(raw) <= len(targets):
        return targets[int(raw) - 1][0]
    # also accept a target id typed directly
    for pid, _ in targets:
        if raw == pid:
            return pid
    print("Not a valid selection: %r" % raw, file=sys.stderr)
    return None


def ota_upload(target, host):
    """Upload the built image for `target` to `host` with espota. Returns rc."""
    espota = resolve_espota()
    if not espota:
        print("Could not find espota.py in the PlatformIO Arduino-ESP32 "
              "framework package.", file=sys.stderr)
        return 1
    image = os.path.join(REPO, ".pio", "build", target, "firmware.bin")
    if not os.path.isfile(image):
        print("No built image at %s." % image, file=sys.stderr)
        return 1
    cmd = [sys.executable, espota, "-i", host, "-f", image, "--progress"]
    password = read_secret("COMTEL_OTA_PASS")
    shown = list(cmd)
    if password:
        cmd += ["-a", password]
        shown += ["-a", "****"]
    print("Running: %s" % " ".join(shown))
    return subprocess.run(cmd, cwd=REPO).returncode


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Discover, select, and flash Comtel-sign firmware.")
    parser.add_argument("target", nargs="?",
                        help="'launcher' or a plugin id (skips the menu)")
    parser.add_argument("--sync", action="store_true",
                        help="regenerate the platformio.ini env block and exit")
    parser.add_argument("--port",
                        help="USB upload port (passed as --upload-port); "
                             "omit to let PlatformIO auto-detect")
    parser.add_argument("--no-upload", action="store_true",
                        help="build only, do not upload")
    parser.add_argument("--ota", nargs="?", const="", metavar="HOST",
                        help="upload over WiFi instead of USB (launcher only); "
                             "HOST defaults to <COMTEL_HOSTNAME>.local from "
                             "include/secrets.h")
    args = parser.parse_args(argv)

    plugins = discover_plugins()

    if args.sync:
        changed = sync_env_block(plugins, verbose=True)
        if not changed:
            print("platformio.ini: env block already up to date (%d plugin(s))."
                  % len(plugins))
        lint_plugins(plugins)
        return 0

    if not plugins:
        print("No plugins found in src/plugins/. Add a <name>.cpp there first.",
              file=sys.stderr)
        return 1

    # keep the env block matching the directory before every build
    sync_env_block(plugins, verbose=True)
    lint_plugins(plugins)

    targets = [(LAUNCHER, LAUNCHER_LABEL)] + plugins
    ids = [pid for pid, _ in targets]
    if args.target:
        target = args.target
        if target not in ids:
            print("Unknown target %r. Available: %s"
                  % (target, ", ".join(ids)), file=sys.stderr)
            return 1
    else:
        target = choose_target(targets)
        if not target:
            print("Nothing selected.", file=sys.stderr)
            return 1

    ota_host = None
    if args.ota is not None and not args.no_upload:
        if target != LAUNCHER:
            # A solo image has no OTA receiver: flashing one over the air
            # would leave the board reachable only by USB again.
            print("--ota only applies to the launcher; solo images have no "
                  "networking. Flash %r over USB." % target, file=sys.stderr)
            return 1
        ota_host = args.ota
        if not ota_host:
            hostname = read_secret("COMTEL_HOSTNAME")
            if not hostname:
                print("--ota needs a host: pass one, or define COMTEL_HOSTNAME "
                      "in include/secrets.h.", file=sys.stderr)
                return 1
            ota_host = hostname + ".local"

    pio = resolve_pio()
    if not pio:
        print("Could not find the 'pio' executable. Run this from the "
              "PlatformIO IDE terminal, or ensure PlatformIO is installed "
              "(expected ~/.platformio/penv/).", file=sys.stderr)
        return 1

    cmd = [pio, "run", "-e", target]
    if not args.no_upload and ota_host is None:
        cmd += ["-t", "upload"]
        if args.port:
            cmd += ["--upload-port", args.port]
    print("Running: %s" % " ".join(cmd))
    result = subprocess.run(cmd, cwd=REPO)
    if result.returncode != 0:
        print("%s failed (exit %d)."
              % ("Build" if args.no_upload or ota_host else "Build/upload",
                 result.returncode), file=sys.stderr)
        return result.returncode

    if ota_host is not None:
        rc = ota_upload(target, ota_host)
        if rc != 0:
            print("OTA upload to %s failed (exit %d). Is the sign on and "
                  "reachable?" % (ota_host, rc), file=sys.stderr)
            return rc
        print("Done: %s flashed over WiFi to %s." % (target, ota_host))
        return 0

    print("Done: %s %s." % (target, "built" if args.no_upload else "flashed"))
    return 0


if __name__ == "__main__":
    sys.exit(main())

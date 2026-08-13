#!/usr/bin/env python3
"""Generate .vscode C++ IntelliSense config from the esphome build.

PlatformIO's compiledb carries every compiler flag inline, but newer esphome
no longer leaves it at the build root, and its entries point at the build-tree
copies of the sources instead of the workspace files open in the editor. So:
run pio compiledb, filter the component TUs, remap their paths back to the
workspace, and point cpptools at the result.

The component name and the build directory are discovered, so this file is the
same in every one of the sibling component repos.
"""

import glob
import json
import os
import shlex
import shutil
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
WORKSPACE = os.path.dirname(HERE)
DEST_DIR = os.path.join(WORKSPACE, ".vscode")


def _component():
    root = os.path.join(WORKSPACE, "components")
    names = [
        d
        for d in sorted(os.listdir(root))
        if os.path.isdir(os.path.join(root, d)) and not d.startswith((".", "__"))
    ]
    return names[0] if names else ""


def _build_dir():
    builds = os.path.join(WORKSPACE, ".esphome", "build")
    hits = sorted(glob.glob(os.path.join(builds, "*intellisense*")))
    if hits:
        return hits[0]
    hits = [d for d in sorted(glob.glob(os.path.join(builds, "*"))) if os.path.isdir(d)]
    return hits[0] if hits else os.path.join(builds, "intellisense")


COMPONENT = _component()
BUILD = _build_dir()
DB = os.path.join(BUILD, "compile_commands.json")
# esp-idf builds: cmake writes this one, pio compiledb does not.
CMAKE_DB = os.path.join(BUILD, "build", "compile_commands.json")
DEFINES_H = os.path.join(BUILD, "src", "esphome", "core", "defines.h")
WS_COMPONENT = os.path.join(WORKSPACE, "components", COMPONENT)


def _resolve_compiler(raw):
    if os.path.isabs(raw) and os.path.exists(raw):
        return raw
    found = shutil.which(raw)
    if found:
        return found
    name = os.path.basename(raw)
    home = os.path.expanduser("~")
    pkgs = os.path.join(home, ".platformio", "packages")
    if os.path.isdir(pkgs):
        for pkg in sorted(os.listdir(pkgs)):
            c = os.path.join(pkgs, pkg, "bin", name)
            if "toolchain" in pkg and os.path.exists(c):
                return c
    idf = os.path.join(home, ".cache", "esphome", "idf", "tools")
    hits = sorted(glob.glob(os.path.join(idf, "**", "bin", name), recursive=True))
    if hits:
        return hits[0]
    return shutil.which(name) or ""


def _component_entries(db):
    marker = os.path.join("src", "esphome", "components", COMPONENT) + os.sep
    out = []
    for e in db:
        f = e.get("file", "")
        if not os.path.isabs(f):
            f = os.path.normpath(os.path.join(e.get("directory", ""), f))
        idx = f.find(marker)
        if idx != -1:
            out.append(
                {**e, "file": os.path.join(WS_COMPONENT, f[idx + len(marker) :])}
            )
        elif f.startswith(WS_COMPONENT + os.sep):
            out.append(e)
    return out


def _header_entries(entries):
    if not entries:
        return []
    tmpl = entries[0]
    args = tmpl.get("arguments") or shlex.split(tmpl.get("command", ""))
    src = os.path.basename(tmpl["file"])
    kept, skip = [], False
    for tok in args:
        if skip:
            skip = False
        elif tok in ("-o", "-MF", "-MT"):
            skip = True
        elif tok != "-c" and not tok.endswith(src):
            kept.append(tok)
    return [
        {
            "directory": tmpl.get("directory", WORKSPACE),
            "file": h,
            "arguments": [*kept, "-c", h],
        }
        for h in sorted(glob.glob(os.path.join(WS_COMPONENT, "*.h")))
    ]


def _unity_src(tests_dir):
    libdeps = os.path.join(tests_dir, ".pio", "libdeps")
    for attempt in range(2):
        for root, _dirs, files in os.walk(libdeps):
            if os.path.basename(root) == "src" and "unity.h" in files:
                return root
        if attempt == 0:
            # unity lands in libdeps only after a native test build
            subprocess.run(
                ["pio", "test", "-d", tests_dir, "-e", "native", "--without-testing"],
                check=False,
                timeout=600,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
    return None


def _test_std(tests_dir):
    # Read it rather than repeat it: a second copy here would keep resolving
    # symbols the real test build rejects. platformio.ini for the pio suites,
    # the shell runner for the ones built straight with g++.
    runners = sorted(
        os.path.basename(p) for p in glob.glob(os.path.join(tests_dir, "*.sh"))
    )
    for name in ("platformio.ini", *runners):
        try:
            with open(os.path.join(tests_dir, name)) as fh:
                for line in fh:
                    for token in line.split():
                        if token.strip("\\'\"").startswith("-std="):
                            return token.strip("\\'\"")
        except OSError:
            continue
    return "-std=gnu++17"


def _test_sources(tests_dir):
    found = sorted(glob.glob(os.path.join(tests_dir, "*.cpp")))
    found += sorted(glob.glob(os.path.join(tests_dir, "*", "*.cpp")))
    return found


def _native_test_entries():
    tests_dir = os.path.join(WORKSPACE, "tests")
    sources = _test_sources(tests_dir)
    if not sources:
        return []
    args = [shutil.which("g++") or "g++", _test_std(tests_dir), "-I" + WS_COMPONENT]
    unity_src = _unity_src(tests_dir)
    if unity_src:
        args.append("-I" + unity_src)
    return [
        {"directory": tests_dir, "file": src, "arguments": [*args, "-c", src]}
        for src in sources
    ]


def _find_db():
    if os.path.exists(CMAKE_DB):
        return CMAKE_DB
    subprocess.run(
        ["pio", "run", "-d", BUILD, "-t", "compiledb"],
        check=False,
        timeout=600,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if os.path.exists(DB):
        return DB
    return CMAKE_DB if os.path.exists(CMAKE_DB) else None


def main():
    if not COMPONENT:
        print(f"gen-cpp-properties: no component under {WORKSPACE}/components")
        return 1
    db_path = _find_db()
    if db_path is None:
        print(f"gen-cpp-properties: no compile_commands.json at {CMAKE_DB} or {DB}")
        print("gen-cpp-properties: run 'esphome compile .intellisense.yaml' first")
        return 1
    with open(db_path) as fh:
        db = json.load(fh)
    entries = _component_entries(db)
    if not entries:
        print(f"gen-cpp-properties: no {COMPONENT} TUs in {db_path}")
        return 1

    cmd = entries[0].get("command", "")
    argv0 = shlex.split(cmd)[0] if cmd else (entries[0].get("arguments") or [""])[0]
    entries += _header_entries(entries)
    compiler = _resolve_compiler(argv0)
    config = {
        "name": "ESP32",
        "compilerArgs": ["-mlongcalls"],
        "cStandard": "gnu17",
        "cppStandard": "gnu++20",
        "includePath": ["${workspaceFolder}/components/**"],
        "browse": {
            "path": ["${workspaceFolder}/components/**"],
            "limitSymbolsToIncludedHeaders": True,
        },
        "compileCommands": "${workspaceFolder}/.vscode/compile_commands.json",
    }
    if compiler:
        config["compilerPath"] = compiler
    else:
        print(f"gen-cpp-properties: no compiler found for {argv0}")
    # force-include the esphome defines per component TU, not globally, so it
    # never reaches the native test entries whose files have no esphome includes
    if os.path.exists(DEFINES_H):
        for e in entries:
            if e.get("command"):
                e["command"] += " -include " + shlex.quote(DEFINES_H)
            elif e.get("arguments"):
                e["arguments"] = [*e["arguments"], "-include", DEFINES_H]

    # pio compiledb omits test TUs; synthesize them so cpptools resolves each
    # with native flags instead of falling back to the ESP32 config
    entries += _native_test_entries()

    os.makedirs(DEST_DIR, exist_ok=True)
    with open(os.path.join(DEST_DIR, "compile_commands.json"), "w") as fh:
        json.dump(entries, fh, indent=2)
    with open(os.path.join(DEST_DIR, "c_cpp_properties.json"), "w") as fh:
        json.dump({"version": 4, "configurations": [config]}, fh, indent=4)
    print(f"gen-cpp-properties: {len(entries)} TUs -> {DEST_DIR}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

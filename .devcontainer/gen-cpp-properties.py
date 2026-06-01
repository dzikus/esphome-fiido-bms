#!/usr/bin/env python3
import json
import os
import shlex
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
WORKSPACE = os.path.dirname(HERE)
_BUILD = os.path.join(WORKSPACE, ".esphome", "build", "fiido-bms-intellisense")
CMAKE_DB = os.path.join(_BUILD, "compile_commands.json")
DEFINES_H = os.path.join(_BUILD, "src", "esphome", "core", "defines.h")
_BUILD_COMPONENT = os.path.join(_BUILD, "src", "esphome", "components", "fiido_bms")
_WS_COMPONENT = os.path.join(WORKSPACE, "components", "fiido_bms")
DEST_DIR = os.path.join(WORKSPACE, ".vscode")
DEST = os.path.join(DEST_DIR, "c_cpp_properties.json")
DEST_CMDS = os.path.join(DEST_DIR, "compile_commands.json")


def _resolve_compiler(raw):
    if os.path.isabs(raw) and os.path.exists(raw):
        return raw
    found = shutil.which(raw)
    if found:
        return found
    # bare compiler name from esp-idf; search platformio toolchains
    pkgs = os.path.join(os.path.expanduser("~"), ".platformio", "packages")
    if os.path.isdir(pkgs):
        for pkg in sorted(os.listdir(pkgs)):
            if "toolchain" in pkg:
                c = os.path.join(pkgs, pkg, "bin", os.path.basename(raw))
                if os.path.exists(c):
                    return c
    return raw


def _file_abs(entry):
    f = entry.get("file", "")
    if not os.path.isabs(f):
        f = os.path.normpath(os.path.join(entry.get("directory", ""), f))
    return f


def _component_entries(db):
    ws = _WS_COMPONENT + os.sep
    build = _BUILD_COMPONENT + os.sep
    out = []
    for e in db:
        f = _file_abs(e)
        if f.startswith(ws):
            out.append(e)
        elif f.startswith(build):
            out.append({**e, "file": _WS_COMPONENT + f[len(_BUILD_COMPONENT) :]})
    return out


def main():
    if not os.path.exists(CMAKE_DB):
        print(f"gen-cpp-properties: {CMAKE_DB} not found")
        return 1
    with open(CMAKE_DB) as fh:
        db = json.load(fh)
    if not db:
        print("gen-cpp-properties: cmake db is empty")
        return 1

    cmd = db[0].get("command", "")
    raw = shlex.split(cmd)[0] if cmd else (db[0].get("arguments") or [""])[0]
    compiler = _resolve_compiler(raw)

    config = {
        "name": "ESP32",
        "compilerPath": compiler,
        "compilerArgs": ["-mlongcalls"],
        "cStandard": "gnu17",
        "cppStandard": "gnu++20",
        "includePath": ["${workspaceFolder}/components/**"],
        "browse": {
            "path": ["${workspaceFolder}/components/**"],
            "limitSymbolsToIncludedHeaders": True,
        },
    }
    if os.path.exists(DEFINES_H):
        config["forcedInclude"] = [DEFINES_H]

    os.makedirs(DEST_DIR, exist_ok=True)
    entries = _component_entries(db)
    if entries:
        tmp = DEST_CMDS + ".tmp"
        with open(tmp, "w") as fh:
            json.dump(entries, fh, indent=2)
        os.replace(tmp, DEST_CMDS)
        config["compileCommands"] = DEST_CMDS
        print(f"gen-cpp-properties: {len(entries)} TUs -> {DEST_CMDS}")
    else:
        print(f"gen-cpp-properties: WARN 0 TUs; sample: {db[0].get('file')!r}")

    tmp = DEST + ".tmp"
    with open(tmp, "w") as fh:
        json.dump({"version": 4, "configurations": [config]}, fh, indent=4)
    os.replace(tmp, DEST)
    print(f"gen-cpp-properties: wrote {DEST}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

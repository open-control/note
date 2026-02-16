Import("env")

import os
from pathlib import Path


def find_tools_bin(project_dir: Path) -> Path | None:
    for candidate in [project_dir, *project_dir.parents]:
        tools_bin = candidate / "tools" / "bin"
        if (tools_bin / "gcc.cmd").exists() and (tools_bin / "g++.cmd").exists():
            return tools_bin
    return None


def configure_windows_native_toolchain() -> None:
    if os.name != "nt":
        return

    project_dir = Path(env.subst("$PROJECT_DIR")).resolve()
    tools_bin = find_tools_bin(project_dir)

    if tools_bin is None:
        print("[oc-note-native] tools/bin wrappers not found, using system gcc/g++")
        return

    wrappers = {
        "CC": tools_bin / "gcc.cmd",
        "CXX": tools_bin / "g++.cmd",
        "AR": tools_bin / "ar.cmd",
        "RANLIB": tools_bin / "ranlib.cmd",
    }

    path = env["ENV"].get("PATH", "")
    env["ENV"]["PATH"] = str(tools_bin) + (os.pathsep + path if path else "")

    env["ENV"].setdefault("SCONSFLAGS", "-j1")
    env["ENV"].update({key: str(value) for key, value in wrappers.items()})

    env.Replace(
        CC=str(wrappers["CC"]),
        CXX=str(wrappers["CXX"]),
        AR=str(wrappers["AR"]),
        RANLIB=str(wrappers["RANLIB"]),
    )

    print(f"[oc-note-native] using toolchain wrappers from {tools_bin}")


configure_windows_native_toolchain()

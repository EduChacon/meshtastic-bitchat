"""Patch the pinned BLE dependency before its compilation, only for bridge builds."""

import runpy
from pathlib import Path

Import("env")

patched = False


def patch_nimble(build_env, node):
    global patched
    if not patched:
        library = Path(node.srcnode().get_abspath()).parent.parent
        script = Path(build_env.subst("$PROJECT_DIR")) / "tools/bridge/patch_nimble.py"
        runpy.run_path(str(script))["apply"](library)
        patched = True
        print("Bridge: verified and applied NimBLE 1.4.3 startup guard")
    return node


env.AddBuildMiddleware(patch_nimble, "*/NimBLE-Arduino/src/NimBLEDevice.cpp")

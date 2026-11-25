#!/usr/bin/env python3
"""The release archive: dist/mhxx-smithy-tree-<version>.zip, holding the mod as it goes into the port's mods folder
(mhxx-smithy-tree/romfs/nativeNX/plugins/mhxx_smithy_tree.nro) and its licenses. Prints its size and SHA-256.
    python3 tools/dist.py <version>        (after ./build.sh; ./build.sh dist <version> does both)"""
import hashlib, os, re, sys, zipfile

def main():
    if len(sys.argv) != 2 or not re.fullmatch(r"\d+(\.\d+)*", sys.argv[1]):
        sys.exit("dist: give the release's version, e.g. 0.13.0")
    ver = sys.argv[1]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    nro = os.path.join(root, "build", "mhxx_smithy_tree.nro")
    if not os.path.isfile(nro):
        sys.exit("dist: no build/mhxx_smithy_tree.nro: run ./build.sh first")
    src = open(os.path.join(root, "src", "plugin.c"), encoding="utf-8").read()
    m = re.search(r'#define VERSION "([^"]+)"', src)
    if m and m.group(1) != ver:
        sys.exit(f"dist: the plugin says it is {m.group(1)}, not {ver}: set VERSION in src/plugin.c and build again")
    os.makedirs(os.path.join(root, "dist"), exist_ok=True)
    out = os.path.join(root, "dist", f"mhxx-smithy-tree-{ver}.zip")
    files = [(nro, "mhxx-smithy-tree/romfs/nativeNX/plugins/mhxx_smithy_tree.nro"),
             (os.path.join(root, "LICENSE"), "mhxx-smithy-tree/licenses/LICENSE"),
             (os.path.join(root, "link", "LICENSE.forge-rs"), "mhxx-smithy-tree/licenses/LICENSE.forge-rs"),
             (os.path.join(root, "third_party", "cimgui", "LICENSE"), "mhxx-smithy-tree/licenses/LICENSE.cimgui"),
             (os.path.join(root, "README.md"), "mhxx-smithy-tree/README.md")]
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for path, name in files:
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))  # (no build machine's times or names)
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(path, "rb") as f:
                z.writestr(info, f.read())
    data = open(out, "rb").read()
    print(f"{os.path.relpath(out, root)}: {len(data)} bytes, SHA-256 {hashlib.sha256(data).hexdigest()}")

main()

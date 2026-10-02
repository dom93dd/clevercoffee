"""
Orione build (-D CC_ORIONE) only: the web interface is the single file frontend-orione/index.html,
gzipped into data/html/index.html.gz. Runs after the other frontend scripts and replaces what they
wrote to data/ (the stock frontend), so the LittleFS image holds one ~10 KB file instead of ~140 KB.
"""

Import("env")  # noqa: F821  (PlatformIO)

import gzip
import io
import os
import shutil

if "CC_ORIONE" in " ".join(env.GetProjectOption("build_flags", [])):  # noqa: F821  (CPPDEFINES are not parsed yet in pre: scripts)
    root = env.subst("$PROJECT_DIR")  # noqa: F821
    source = os.path.join(root, "frontend-orione", "index.html")
    data = os.path.join(root, "data")

    shutil.rmtree(data, ignore_errors=True)
    os.makedirs(os.path.join(data, "html"))

    with open(source, "rb") as f:
        raw = f.read()

    packed = io.BytesIO()
    with gzip.GzipFile(fileobj=packed, mode="wb", compresslevel=9, mtime=0) as gz:  # mtime 0: same bytes, same ETag
        gz.write(raw)

    with open(os.path.join(data, "html", "index.html.gz"), "wb") as f:
        f.write(packed.getvalue())

    print(f"Orione frontend: {source} -> data/html/index.html.gz ({len(raw)} -> {len(packed.getvalue())} bytes)")

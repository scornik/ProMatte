#!/usr/bin/env python3
"""Fills sha256 / size_bytes in data/models/manifest.json from models/converted/.

Usage: python tools/models/update_manifest.py [--models-dir models/converted]
"""
import argparse
import hashlib
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ap = argparse.ArgumentParser()
ap.add_argument("--models-dir", default=os.path.join(ROOT, "models", "converted"))
ap.add_argument("--manifest", default=os.path.join(ROOT, "data", "models", "manifest.json"))
args = ap.parse_args()

with open(args.manifest, "r", encoding="utf-8") as f:
    manifest = json.load(f)

for m in manifest["models"]:
    path = os.path.join(args.models_dir, m["file_name"])
    if not os.path.exists(path):
        print(f"missing: {m['id']} ({path})")
        continue
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    m["sha256"] = h.hexdigest()
    m["size_bytes"] = os.path.getsize(path)
    print(f"{m['id']}: {m['size_bytes']} bytes sha256 {m['sha256'][:16]}...")

with open(args.manifest, "w", encoding="utf-8") as f:
    json.dump(manifest, f, indent=2)
    f.write("\n")
print("manifest updated")

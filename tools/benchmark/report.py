#!/usr/bin/env python3
"""Renders promatte-bench JSON results as Markdown tables.

Usage: python tools/benchmark/report.py results.json [more.json ...] > table.md
"""
import json
import sys


def render(path):
    d = json.load(open(path, encoding="utf-8"))
    sysinfo = d.get("system", {})
    gpus = ", ".join(g["name"] for g in sysinfo.get("gpus", []))
    inp = sysinfo.get("input", {})
    out = []
    out.append(f"**{path}** — {sysinfo.get('cpu', '?')}, GPUs: {gpus}; input {inp.get('width')}x{inp.get('height')}, {inp.get('frames')} frames\n")
    out.append("| Model | Backend | Device | Tier | AI input | Init ms | Inference ms (p95) | Total ms | AI FPS | CPU % | VRAM Δ MB | RSS Δ MB | Flicker | Softness | Coverage |")
    out.append("| ----- | ------- | ------ | ---- | -------- | ------: | -----------------: | -------: | -----: | ----: | --------: | -------: | ------: | -------: | -------: |")
    for r in d["results"]:
        if not r.get("ok"):
            out.append(f"| {r['model']} | {r['backend']} | | {r['tier']} | | | FAILED: {r.get('error', '')[:60]} | | | | | | | | |")
            continue
        out.append("| {model} | {backend} | {device} | {tier} | {input} | {init_ms:.0f} | {latency_ms:.1f} ({latency_p95_ms:.1f}) | {total_ms:.1f} | {fps:.1f} | {cpu_percent:.0f} | {vram_delta_mb:.0f} | {rss_delta_mb:.0f} | {flicker:.4f} | {softness:.3f} | {coverage:.3f} |".format(
            **{**r, "device": r.get("device", "")[:28]}))
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    for p in sys.argv[1:]:
        print(render(p))

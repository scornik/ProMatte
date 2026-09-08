#!/usr/bin/env python3
"""Verifies in real OBS that "Remove (transparent)" actually reveals what is
behind the camera in the scene.

Builds a scene with a solid magenta colour source *behind* the webcam, applies
ProMatte in transparent mode, screenshots the composited scene and checks that
the background region really became magenta. Then repeats with a green
background: the person must stay the same while the background follows.

Usage: python tests/integration/obs_transparency_check.py --password <ws password>
"""
import argparse
import datetime as dt
import os
import subprocess
import sys
import time

import numpy as np
import cv2
import obsws_python as obs

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_OBS = r"C:\Program Files\obs-studio\bin\64bit\obs64.exe"
SCENE = "ProMatte Transparency Check"
BG = "ProMatte Check Background"
FILTER_NAME = "ProMatte AI Background Removal"


def log(m):
    print(f"[{dt.datetime.now().strftime('%H:%M:%S')}] {m}", flush=True)


def connect(port, password, timeout=120):
    end = time.time() + timeout
    last = None
    while time.time() < end:
        try:
            c = obs.ReqClient(host="localhost", port=port, password=password, timeout=30)
            c.get_scene_list()
            return c
        except Exception as e:  # noqa: BLE001
            last = e
            time.sleep(2)
    raise SystemExit(f"could not connect: {last}")


def find_camera(c):
    for i in c.get_input_list().inputs:
        if i["inputKind"].startswith("dshow_input"):
            return i["inputName"]
    return None


def set_bg_colour(c, abgr):
    c.set_input_settings(BG, {"color": abgr, "width": 1920, "height": 1080}, True)


def shot(c, path):
    c.save_source_screenshot(SCENE, "png", os.path.abspath(path), 960, 540, -1)
    img = cv2.imread(os.path.abspath(path), cv2.IMREAD_UNCHANGED)
    return img[:, :, :3].astype(np.int16)  # BGR


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--obs", default=DEFAULT_OBS)
    ap.add_argument("--port", type=int, default=4455)
    ap.add_argument("--password", default=os.environ.get("OBS_WS_PASSWORD", "promatte-test"))
    ap.add_argument("--out", default=os.path.join(ROOT, "tests", "visual", "transparency"))
    ap.add_argument("--no-launch", action="store_true")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    proc = None
    if not args.no_launch:
        proc = subprocess.Popen([args.obs, "--websocket_port", str(args.port), "--websocket_password",
                                 args.password, "--disable-updater", "--disable-shutdown-check"],
                                cwd=os.path.dirname(args.obs))
        time.sleep(10)
    c = connect(args.port, args.password)
    log(f"connected to OBS {c.get_version().obs_version}")
    previous = c.get_current_program_scene().current_program_scene_name

    cam = find_camera(c)
    if cam is None:
        raise SystemExit("no webcam source found in this OBS profile")
    log(f"camera: {cam}")

    scenes = [s["sceneName"] for s in c.get_scene_list().scenes]
    if SCENE in scenes:
        c.remove_scene(SCENE)
        time.sleep(0.5)
    c.create_scene(SCENE)
    # Background first (drawn underneath), camera on top.
    c.create_input(SCENE, BG, "color_source_v3", {"color": 0xFFFF00FF, "width": 1920, "height": 1080}, True)
    time.sleep(0.5)
    c.create_scene_item(SCENE, cam, True)
    time.sleep(1.0)
    for it in c.get_scene_item_list(SCENE).scene_items:
        if it["sourceName"] == cam:
            c.set_scene_item_transform(SCENE, it["sceneItemId"],
                                       {"boundsType": "OBS_BOUNDS_SCALE_INNER", "boundsWidth": 1920,
                                        "boundsHeight": 1080, "positionX": 0, "positionY": 0})
    c.set_current_program_scene(SCENE)
    time.sleep(2)

    for f in c.get_source_filter_list(cam).filters:
        if f["filterName"] == FILTER_NAME:
            c.remove_source_filter(cam, FILTER_NAME)
            time.sleep(0.5)
    c.create_source_filter(cam, FILTER_NAME, "promatte_filter",
                           {"bg_mode": "transparent", "quality": "auto", "preset": "webcam"})
    log("filter added, waiting for the model to load")
    time.sleep(20)

    magenta = np.array([255, 0, 255], np.int16)  # BGR
    green = np.array([0, 255, 0], np.int16)
    set_bg_colour(c, 0xFFFF00FF)
    time.sleep(3)
    a = shot(c, os.path.join(args.out, "behind_magenta.png"))
    set_bg_colour(c, 0xFF00FF00)
    time.sleep(3)
    b = shot(c, os.path.join(args.out, "behind_green.png"))

    near_magenta = (np.abs(a - magenta).max(axis=2) < 40)
    near_green = (np.abs(b - green).max(axis=2) < 40)
    # Pixels that followed the background colour in BOTH shots = revealed background.
    revealed = near_magenta & near_green
    changed = (np.abs(a - b).max(axis=2) > 30)
    log(f"magenta background visible: {near_magenta.mean() * 100:.1f}% of frame")
    log(f"green background visible:   {near_green.mean() * 100:.1f}% of frame")
    log(f"pixels that follow whatever is behind the camera: {revealed.mean() * 100:.1f}%")
    log(f"pixels that changed between the two backgrounds:  {changed.mean() * 100:.1f}%")

    ok = revealed.mean() > 0.15 and changed.mean() > 0.15
    log("RESULT: " + ("PASS - transparent mode reveals the scene behind the camera"
                      if ok else "FAIL - the background was NOT revealed"))

    # Leave OBS in a clean state.
    c.remove_source_filter(cam, FILTER_NAME)
    c.set_current_program_scene(previous)
    c.remove_scene(SCENE)
    if proc:
        subprocess.run(["taskkill", "/IM", "obs64.exe"], capture_output=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

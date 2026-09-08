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
# Unique per run: OBS removes a scene asynchronously, so reusing a fixed name
# trips "a source already exists by that scene name" after an interrupted run.
_STAMP = dt.datetime.now().strftime("%H%M%S")
SCENE = f"ProMatte Transparency Check {_STAMP}"
BG = f"ProMatte Check Background {_STAMP}"
SCENE_PREFIX = "ProMatte Transparency Check"
BG_PREFIX = "ProMatte Check Background"
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


def retry(fn, attempts=8, delay=4, tolerate=()):
    """OBS answers slowly (or times out) until it has finished loading.

    A request that times out client-side may still have been executed, so the
    caller can tolerate the "already exists" that the retry then provokes.
    """
    last = None
    for _ in range(attempts):
        try:
            return fn()
        except Exception as e:  # noqa: BLE001
            last = e
            if any(t in str(e) for t in tolerate):
                return None
            time.sleep(delay)
    raise last


def wait_for(pred, what, attempts=15, delay=2):
    for _ in range(attempts):
        try:
            if pred():
                return
        except Exception:  # noqa: BLE001
            pass
        time.sleep(delay)
    raise SystemExit(f"timed out waiting for {what}")


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
        time.sleep(20)  # OBS needs to finish loading and open the camera
    c = connect(args.port, args.password)
    log(f"connected to OBS {retry(c.get_version).obs_version}")
    retry(c.get_scene_list)  # wait until the UI thread is actually free
    time.sleep(3)
    previous = retry(c.get_current_program_scene).current_program_scene_name

    cam = find_camera(c)
    if cam is None:
        raise SystemExit("no webcam source found in this OBS profile")
    log(f"camera: {cam}")

    # Clear anything an interrupted earlier run left behind, then make a fresh scene.
    for sc in [x["sceneName"] for x in retry(c.get_scene_list).scenes]:
        if sc.startswith(SCENE_PREFIX):
            try:
                c.remove_scene(sc)
                log(f"removed leftover scene '{sc}'")
                time.sleep(1)
            except Exception:  # noqa: BLE001
                pass
    for inp in [x["inputName"] for x in retry(c.get_input_list).inputs]:
        if inp.startswith(BG_PREFIX):
            try:
                c.remove_input(inp)
                time.sleep(0.5)
            except Exception:  # noqa: BLE001
                pass
    retry(lambda: c.create_scene(SCENE), tolerate=("601", "already exists"))
    wait_for(lambda: SCENE in [x["sceneName"] for x in c.get_scene_list().scenes], f"scene '{SCENE}'")
    # Background first (drawn underneath), camera on top.
    retry(lambda: c.create_input(SCENE, BG, "color_source_v3",
                                 {"color": 0xFFFF00FF, "width": 1920, "height": 1080}, True),
          tolerate=("601", "already exists"))
    wait_for(lambda: BG in [x["inputName"] for x in c.get_input_list().inputs], f"input '{BG}'")
    if cam not in [it["sourceName"] for it in retry(lambda: c.get_scene_item_list(SCENE)).scene_items]:
        retry(lambda: c.create_scene_item(SCENE, cam, True), tolerate=("601", "already exists"))
    wait_for(lambda: cam in [it["sourceName"] for it in c.get_scene_item_list(SCENE).scene_items],
             f"'{cam}' in the test scene")
    for it in retry(lambda: c.get_scene_item_list(SCENE)).scene_items:
        if it["sourceName"] == cam:
            c.set_scene_item_transform(SCENE, it["sceneItemId"],
                                       {"boundsType": "OBS_BOUNDS_SCALE_INNER", "boundsWidth": 1920,
                                        "boundsHeight": 1080, "positionX": 0, "positionY": 0})
    retry(lambda: c.set_current_program_scene(SCENE))
    time.sleep(3)

    for f in retry(lambda: c.get_source_filter_list(cam)).filters:
        if f["filterName"] == FILTER_NAME:
            c.remove_source_filter(cam, FILTER_NAME)
            time.sleep(0.5)
    retry(lambda: c.create_source_filter(cam, FILTER_NAME, "promatte_filter",
                                        {"bg_mode": "transparent", "quality": "auto", "preset": "webcam"}),
          tolerate=("601", "already exists"))
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

    # Leave OBS in a clean state whatever happened.
    for step in (lambda: c.remove_source_filter(cam, FILTER_NAME),
                 lambda: c.set_current_program_scene(previous),
                 lambda: c.remove_scene(SCENE),
                 lambda: c.remove_input(BG)):
        try:
            step()
            time.sleep(0.5)
        except Exception:  # noqa: BLE001
            pass
    if proc:
        subprocess.run(["taskkill", "/IM", "obs64.exe"], capture_output=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

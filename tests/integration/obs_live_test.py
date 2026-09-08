#!/usr/bin/env python3
"""Live end-to-end test of the ProMatte filter against a running OBS Studio.

Drives OBS through obs-websocket:
  1. Starts OBS if needed (websocket enabled) and waits until it is ready.
  2. Creates a dedicated scene that contains ONLY the webcam and switches to it,
     so every screenshot/recording shows the filter output and nothing else on
     screen. The previous scene is restored at the end.
  3. Adds the ProMatte filter and walks through presets, background modes,
     debug views, quality levels, backends and models, capturing a screenshot
     and OBS render statistics for each step.
  4. Toggles the filter, switches scenes, changes the camera resolution.
  5. Optionally records 10 s of the camera-only scene (--record) and runs a soak
     test (--soak MINUTES) sampling render FPS and memory every 30 s.
  6. Reads back the ProMatte lines of the OBS log and writes a JSON report.

Usage
  python tests/integration/obs_live_test.py --password <obs-websocket password>
        [--obs "C:/Program Files/obs-studio/bin/64bit/obs64.exe"] [--port 4455]
        [--out DIR] [--soak 30] [--record] [--no-launch] [--keep-open]
"""
import argparse
import datetime as dt
import glob
import json
import os
import subprocess
import sys
import time

import obsws_python as obs

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_OBS = r"C:\Program Files\obs-studio\bin\64bit\obs64.exe"
LOG_DIR = os.path.join(os.environ.get("APPDATA", ""), "obs-studio", "logs")
TEST_SCENE = "ProMatte Test Scene"
ALT_SCENE = "ProMatte Switch Target"
FILTER_NAME = "ProMatte AI Background Removal"


def log(msg):
    print(f"[{dt.datetime.now().strftime('%H:%M:%S')}] {msg}", flush=True)


def newest_obs_log():
    files = sorted(glob.glob(os.path.join(LOG_DIR, "*.txt")), key=os.path.getmtime)
    return files[-1] if files else None


def read_log_tail(path, start=0):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        f.seek(start)
        return f.read()


def connect(port, password, timeout=90):
    end = time.time() + timeout
    last = None
    while time.time() < end:
        try:
            return obs.ReqClient(host="localhost", port=port, password=password, timeout=10)
        except Exception as e:  # noqa: BLE001
            last = e
            time.sleep(2)
    raise SystemExit(f"could not connect to obs-websocket: {last}")


def wait_ready(c, port, password):
    """OBS answers requests with code 207 until its UI has finished loading."""
    for _ in range(60):
        try:
            return c, c.get_version()
        except Exception:  # noqa: BLE001
            time.sleep(2)
            c = connect(port, password)
    raise SystemExit("OBS never became ready")


def launch_obs(exe, port, password):
    args = [exe, "--websocket_port", str(port), "--websocket_password", password,
            "--disable-updater", "--disable-shutdown-check"]
    log("launching OBS: " + " ".join(args))
    return subprocess.Popen(args, cwd=os.path.dirname(exe))


def obs_stats(c):
    s = c.get_stats()
    return {
        "render_fps": round(s.active_fps, 2),
        "render_ms": round(s.average_frame_render_time, 3),
        "render_skipped": s.render_skipped_frames,
        "render_total": s.render_total_frames,
        "output_skipped": s.output_skipped_frames,
        "memory_mb": round(s.memory_usage, 1),
        "cpu": round(s.cpu_usage, 1),
    }


def find_camera(c):
    for i in c.get_input_list().inputs:
        if i["inputKind"].startswith("dshow_input"):
            return i["inputName"]
    return None


def ensure_scene(c, name):
    names = [s["sceneName"] for s in c.get_scene_list().scenes]
    if name not in names:
        c.create_scene(name)
        time.sleep(0.5)


def setup_test_scene(c):
    """Dedicated scene containing only the webcam, scaled to fill the canvas."""
    cam = find_camera(c)
    created_camera = False
    ensure_scene(c, TEST_SCENE)
    ensure_scene(c, ALT_SCENE)
    if cam is None:
        log("no webcam source found; creating one")
        c.create_input(TEST_SCENE, "ProMatte Test Camera", "dshow_input", {}, True)
        time.sleep(3)
        cam = "ProMatte Test Camera"
        created_camera = True
        props = c.get_input_properties_list_property_items(cam, "video_device_id").property_items
        if props:
            c.set_input_settings(cam, {"video_device_id": props[0]["itemValue"]}, True)
            log(f"selected camera device: {props[0]['itemName']}")
            time.sleep(3)
    else:
        items = c.get_scene_item_list(TEST_SCENE).scene_items
        if cam not in [it["sourceName"] for it in items]:
            c.create_scene_item(TEST_SCENE, cam, True)
            time.sleep(1)
    for it in c.get_scene_item_list(TEST_SCENE).scene_items:
        if it["sourceName"] == cam:
            try:
                c.set_scene_item_transform(TEST_SCENE, it["sceneItemId"],
                                           {"boundsType": "OBS_BOUNDS_SCALE_INNER", "boundsWidth": 1920,
                                            "boundsHeight": 1080, "positionX": 0, "positionY": 0})
            except Exception as e:  # noqa: BLE001
                log(f"transform skipped: {e}")
            c.set_scene_item_enabled(TEST_SCENE, it["sceneItemId"], True)
    return cam, created_camera


def get_filter(c, source, name):
    for f in c.get_source_filter_list(source).filters:
        if f["filterName"] == name:
            return f
    return None


def screenshot(c, source, out_dir, label):
    path = os.path.abspath(os.path.join(out_dir, f"{label}.png"))
    c.save_source_screenshot(source, "png", path, 960, 540, -1)
    return path


def promatte_errors(text):
    return [l for l in text.splitlines() if "[promatte]" in l and ("error" in l.lower() or "warn" in l.lower())]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--obs", default=DEFAULT_OBS)
    ap.add_argument("--port", type=int, default=4455)
    ap.add_argument("--password", default=os.environ.get("OBS_WS_PASSWORD", "promatte-test"))
    ap.add_argument("--out", default=os.path.join(ROOT, "tests", "visual", "live"))
    ap.add_argument("--soak", type=float, default=0, help="soak minutes after the functional pass")
    ap.add_argument("--record", action="store_true", help="record 10 s of the camera-only test scene")
    ap.add_argument("--no-launch", action="store_true")
    ap.add_argument("--keep-open", action="store_true")
    ap.add_argument("--report", default=None)
    args = ap.parse_args()
    args.out = os.path.abspath(args.out)
    os.makedirs(args.out, exist_ok=True)

    proc = None
    if not args.no_launch:
        proc = launch_obs(args.obs, args.port, args.password)
        time.sleep(8)
    c = connect(args.port, args.password)
    c, ver = wait_ready(c, args.port, args.password)
    log(f"connected: OBS {ver.obs_version}, obs-websocket {ver.obs_web_socket_version}")
    report = {"obs_version": ver.obs_version, "started": dt.datetime.now().isoformat(), "steps": [], "errors": []}

    logfile = newest_obs_log()
    log_pos = os.path.getsize(logfile) if logfile else 0
    boot_log = read_log_tail(logfile) if logfile else ""
    loaded = [l for l in boot_log.splitlines() if "[promatte]" in l]
    for l in loaded[:10]:
        log("obs log: " + l.strip())
    if not any("loading" in l for l in loaded):
        report["errors"].append("ProMatte module did not log its load message")

    original_scene = c.get_current_program_scene().current_program_scene_name
    cam, created_camera = setup_test_scene(c)
    c.set_current_program_scene(TEST_SCENE)
    time.sleep(2)
    log(f"camera '{cam}' in scene '{TEST_SCENE}' (was '{original_scene}')")
    report["camera"] = {"name": cam, "settings": c.get_input_settings(cam).input_settings}

    if get_filter(c, cam, FILTER_NAME):
        c.remove_source_filter(cam, FILTER_NAME)
        time.sleep(0.5)
    c.create_source_filter(cam, FILTER_NAME, "promatte_filter", {"quality": "auto", "bg_mode": "transparent"})
    log("filter added")
    time.sleep(8)

    def step(label, settings=None, wait=4.0, shot=True):
        if settings:
            c.set_source_filter_settings(cam, FILTER_NAME, settings, True)
        time.sleep(wait)
        st = obs_stats(c)
        entry = {"step": label, "settings": settings or {}, "stats": st}
        if shot:
            try:
                entry["screenshot"] = screenshot(c, cam, args.out, label)
            except Exception as e:  # noqa: BLE001
                entry["screenshot_error"] = str(e)
        report["steps"].append(entry)
        log(f"{label:36s} render {st['render_fps']:5.1f} fps  {st['render_ms']:.2f} ms  "
            f"skipped {st['render_skipped']}  mem {st['memory_mb']} MB")
        return entry

    step("00_baseline_filter_added", wait=3)
    for preset in ["webcam", "talking_head", "gaming", "high_quality", "low_end", "green_screen"]:
        step(f"10_preset_{preset}", {"preset": preset, "bg_mode": "transparent"}, wait=5)
    step("20_mode_transparent", {"preset": "webcam", "bg_mode": "transparent"})
    step("21_mode_blur", {"bg_mode": "blur", "blur_amount": 0.6})
    step("22_mode_color", {"bg_mode": "color", "bg_color": 0xFF2060FF, "bg_color_opacity": 1.0})
    step("23_mode_dim", {"bg_mode": "dim", "dim_brightness": 0.3})
    img = os.path.join(ROOT, "tests", "visual", "assets", "portrait_obama.jpg")
    if os.path.exists(img):
        step("24_mode_image", {"bg_mode": "image", "bg_image": img, "bg_fit": "cover"})
    step("25_debug_matte", {"bg_mode": "transparent", "debug_view": "matte"})
    step("26_debug_edges", {"debug_view": "edges"})
    step("27_debug_confidence", {"debug_view": "confidence"})
    step("28_debug_foreground", {"debug_view": "foreground"})
    step("29_overlay", {"debug_view": "none", "debug_overlay": True})
    for q in ["performance", "balanced", "quality", "ultra", "auto"]:
        step(f"30_quality_{q}", {"quality": q, "debug_overlay": True}, wait=8)
    step("40_backend_cpu", {"backend": "cpu", "quality": "auto"}, wait=10)
    step("41_backend_auto", {"backend": "auto"}, wait=10)
    for model in ["mediapipe_selfie_landscape", "pphumanseg_v2_lite", "mediapipe_selfie_multiclass",
                  "modnet_portrait", "rvm_mobilenetv3", "auto"]:
        step(f"50_model_{model}", {"model": model, "backend": "auto"}, wait=10)
    step("55_blur_auto_model", {"model": "auto", "bg_mode": "blur", "debug_overlay": False}, wait=6)

    for _ in range(4):
        c.set_source_filter_enabled(cam, FILTER_NAME, False)
        time.sleep(1.0)
        c.set_source_filter_enabled(cam, FILTER_NAME, True)
        time.sleep(1.0)
    step("60_after_toggle", wait=3)
    for _ in range(3):
        c.set_current_program_scene(ALT_SCENE)
        time.sleep(1.5)
        c.set_current_program_scene(TEST_SCENE)
        time.sleep(1.5)
    step("61_after_scene_switch", wait=3)

    try:
        items = c.get_input_properties_list_property_items(cam, "resolution").property_items
        if items:
            res = [it["itemValue"] for it in items]
            low = next((r for r in res if r.startswith("640x")), res[-1])
            c.set_input_settings(cam, {"res_type": 1, "resolution": low}, True)
            step("70_camera_low_res", wait=8)
            c.set_input_settings(cam, {"res_type": 0}, True)
            step("71_camera_default_res", wait=8)
    except Exception as e:  # noqa: BLE001
        log(f"resolution change skipped: {e}")

    if args.record:
        try:
            rec_dir = os.path.join(args.out, "recording")
            os.makedirs(rec_dir, exist_ok=True)
            c.set_record_directory(rec_dir)
            time.sleep(1)
            c.start_record()
            for _ in range(20):
                time.sleep(0.5)
                if c.get_record_status().output_active:
                    break
            if not c.get_record_status().output_active:
                raise RuntimeError("recording did not start")
            time.sleep(10)
            out = c.stop_record()
            for _ in range(20):
                time.sleep(0.5)
                if not c.get_record_status().output_active:
                    break
            report["recording"] = out.output_path
            log(f"recorded {out.output_path}")
        except Exception as e:  # noqa: BLE001
            log(f"recording failed: {e}")
            report["errors"].append(f"recording: {e}")

    step("80_final", {"debug_overlay": False, "bg_mode": "transparent", "preset": "webcam"}, wait=3)

    if args.soak > 0:
        log(f"soak test for {args.soak} minutes")
        soak = []
        end = time.time() + args.soak * 60
        while time.time() < end:
            st = obs_stats(c)
            st["t"] = round(time.time(), 0)
            soak.append(st)
            log(f"soak: render {st['render_fps']} fps  skipped {st['render_skipped']}  "
                f"mem {st['memory_mb']} MB  cpu {st['cpu']}%")
            time.sleep(30)
        report["soak"] = soak
        step("90_after_soak", wait=2)

    if logfile:
        tail = read_log_tail(logfile, log_pos)
        report["log_warnings_errors"] = promatte_errors(tail)
        report["promatte_log_lines"] = [l.strip() for l in tail.splitlines() if "[promatte]" in l][-300:]
        rendered = [l for l in tail.splitlines() if "source size" in l and "promatte" in l]
        report["filter_rendered"] = bool(rendered)
        if not rendered:
            report["errors"].append("the filter never rendered a frame (camera not visible?)")
        for l in report["log_warnings_errors"][:20]:
            log("  log: " + l.strip())

    try:
        c.set_current_program_scene(original_scene)
        c.remove_source_filter(cam, FILTER_NAME)
        if created_camera:
            c.remove_input(cam)
        c.remove_scene(TEST_SCENE)
        c.remove_scene(ALT_SCENE)
    except Exception as e:  # noqa: BLE001
        log(f"cleanup: {e}")

    report["finished"] = dt.datetime.now().isoformat()
    report_path = args.report or os.path.join(args.out, "live-report.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)
    log(f"report written to {report_path}")

    if not args.keep_open and proc:
        log("closing OBS")
        subprocess.run(["taskkill", "/IM", "obs64.exe"], capture_output=True)
    return 0 if not report["errors"] else 1


if __name__ == "__main__":
    sys.exit(main())

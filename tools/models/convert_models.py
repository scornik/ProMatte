#!/usr/bin/env python3
"""Fetches the source models and converts them to ONNX in models/converted/.

Runs from a clean checkout: the source models are downloaded into
models/candidates/ (both directories are git-ignored) and converted.

Sources and licences are recorded in THIRD_PARTY_LICENSES.md:
  * MediaPipe Selfie Segmenter (general 256x256, landscape 144x256) and Selfie
    Multiclass 256x256 - tflite, converted with tf2onnx. The graphs use the
    MediaPipe custom op Convolution2DTransposeBias, which fix_mediapipe_ops.py
    rewrites as a standard ConvTranspose.
  * PP-HumanSeg v2 lite 192x192 and portrait 256x144 - Paddle inference models,
    converted with paddle2onnx. Run fix_pphumanseg.py afterwards to resolve the
    adaptive pooling and drop the trailing ArgMax.
  * RVM MobileNetV3 and MODNet are already ONNX and are downloaded on demand by
    the Model Manager rather than bundled, so they are optional here.

Usage: python tools/models/convert_models.py [--with-optional]
"""
import argparse
import os
import shutil
import subprocess
import sys
import urllib.request
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CAND = os.path.join(ROOT, "models", "candidates")
OUT = os.path.join(ROOT, "models", "converted")
PY = sys.executable

MEDIAPIPE = "https://storage.googleapis.com/mediapipe-models/image_segmenter"
PADDLE = "https://paddleseg.bj.bcebos.com/dygraph/pp_humanseg_v2"

# name -> url. Downloaded into models/candidates/.
SOURCES = {
    "selfie_segmenter.tflite": f"{MEDIAPIPE}/selfie_segmenter/float16/latest/selfie_segmenter.tflite",
    "selfie_segmenter_landscape.tflite":
        f"{MEDIAPIPE}/selfie_segmenter_landscape/float16/latest/selfie_segmenter_landscape.tflite",
    "selfie_multiclass_256x256.tflite":
        f"{MEDIAPIPE}/selfie_multiclass_256x256/float32/latest/selfie_multiclass_256x256.tflite",
    "pp_humansegv2_lite_192x192.zip":
        f"{PADDLE}/human_pp_humansegv2_lite_192x192_inference_model.zip",
    "portrait_pp_humansegv2_lite_256x144.zip":
        f"{PADDLE}/portrait_pp_humansegv2_lite_256x144_smaller/portrait_pp_humansegv2_lite_256x144_inference_model.zip",
}
OPTIONAL_SOURCES = {
    "rvm_mobilenetv3_fp32.onnx":
        "https://github.com/PeterL1n/RobustVideoMatting/releases/download/v1.0.0/rvm_mobilenetv3_fp32.onnx",
    "modnet_xenova.onnx": "https://huggingface.co/Xenova/modnet/resolve/main/onnx/model.onnx",
}
# zip name -> directory it is extracted into under models/candidates/
ZIP_DESTS = {
    "pp_humansegv2_lite_192x192.zip": "pphumanseg_lite",
    "portrait_pp_humansegv2_lite_256x144.zip": "pphumanseg_portrait",
}


def run(cmd):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-3000:])
        print(r.stderr[-3000:])
        raise SystemExit(f"command failed: {cmd[0]}")
    return r


def fetch(name, url, optional=False):
    dst = os.path.join(CAND, name)
    if os.path.exists(dst):
        print(f"have {name}")
        return dst
    print(f"downloading {name}", flush=True)
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "ProMatte-build/1.0"})
        with urllib.request.urlopen(req, timeout=300) as r, open(dst + ".part", "wb") as f:
            shutil.copyfileobj(r, f)
        os.replace(dst + ".part", dst)
    except Exception as e:  # noqa: BLE001
        if os.path.exists(dst + ".part"):
            os.remove(dst + ".part")
        if optional:
            print(f"  skipped (optional): {e}")
            return None
        raise SystemExit(f"could not download {name}: {e}")
    print(f"  {os.path.getsize(dst) // 1024} KB")
    return dst


def unzip(name):
    target = os.path.join(CAND, ZIP_DESTS[name])
    if os.path.isdir(target):
        return target
    with zipfile.ZipFile(os.path.join(CAND, name)) as z:
        z.extractall(target)
    return target


def paddle_model_dir(extracted):
    """The archives contain a single inference-model directory."""
    for base, _dirs, files in os.walk(extracted):
        if any(f.endswith(".pdmodel") for f in files):
            return base
    raise SystemExit(f"no .pdmodel found under {extracted}")


def tflite_to_onnx(name, out_name):
    dst = os.path.join(OUT, out_name)
    if os.path.exists(dst):
        print("exists", out_name)
        return
    run([PY, "-m", "tf2onnx.convert", "--tflite", os.path.join(CAND, name), "--output", dst, "--opset", "15"])


def paddle_to_onnx(model_dir, out_name):
    dst = os.path.join(OUT, out_name)
    if os.path.exists(dst):
        print("exists", out_name)
        return
    exe = os.path.join(os.path.dirname(PY), "Scripts", "paddle2onnx.exe") if os.name == "nt" else "paddle2onnx"
    run([exe, "--model_dir", model_dir, "--model_filename", "model.pdmodel",
         "--params_filename", "model.pdiparams", "--save_file", dst, "--opset_version", "13"])


def copy(name, out_name):
    src = os.path.join(CAND, name)
    dst = os.path.join(OUT, out_name)
    if not os.path.exists(src):
        print(f"skip {out_name} (source not downloaded)")
        return
    if not os.path.exists(dst):
        shutil.copyfile(src, dst)
        print("copied", out_name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--with-optional", action="store_true",
                    help="also fetch RVM and MODNet (normally downloaded by the Model Manager)")
    args = ap.parse_args()
    os.makedirs(CAND, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)

    for name, url in SOURCES.items():
        fetch(name, url)
    if args.with_optional:
        for name, url in OPTIONAL_SOURCES.items():
            fetch(name, url, optional=True)

    import fix_mediapipe_ops  # noqa: E402  (needs the download to have happened)

    tflite_to_onnx("selfie_segmenter.tflite", "mediapipe_selfie_general_256.onnx")
    fix_mediapipe_ops.fix(os.path.join(OUT, "mediapipe_selfie_general_256.onnx"))
    tflite_to_onnx("selfie_segmenter_landscape.tflite", "mediapipe_selfie_landscape_144x256.onnx")
    fix_mediapipe_ops.fix(os.path.join(OUT, "mediapipe_selfie_landscape_144x256.onnx"))
    tflite_to_onnx("selfie_multiclass_256x256.tflite", "mediapipe_selfie_multiclass_256.onnx")

    paddle_to_onnx(paddle_model_dir(unzip("pp_humansegv2_lite_192x192.zip")), "pphumanseg_v2_lite_192.onnx")
    paddle_to_onnx(paddle_model_dir(unzip("portrait_pp_humansegv2_lite_256x144.zip")),
                   "pphumanseg_v2_portrait_256x144.onnx")

    copy("rvm_mobilenetv3_fp32.onnx", "rvm_mobilenetv3_fp32.onnx")
    copy("modnet_xenova.onnx", "modnet_portrait.onnx")

    import onnx  # noqa: E402

    for f in sorted(os.listdir(OUT)):
        if not f.endswith(".onnx"):
            continue
        m = onnx.load(os.path.join(OUT, f), load_external_data=False)

        def shp(t):
            return [(d.dim_value if d.dim_value else d.dim_param) for d in t.type.tensor_type.shape.dim]

        size = os.path.getsize(os.path.join(OUT, f)) // 1024
        opsets = [o.version for o in m.opset_import if o.domain == ""]
        print(f"== {f}: opset {opsets}, size {size} KB")
        for i in m.graph.input[:1]:
            print("   in ", i.name, shp(i))
        for o in m.graph.output:
            print("   out", o.name, shp(o))


if __name__ == "__main__":
    main()

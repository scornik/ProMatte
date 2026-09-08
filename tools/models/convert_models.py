#!/usr/bin/env python3
"""Converts the candidate segmentation models to ONNX and writes them to models/converted/.

Sources (see THIRD_PARTY_LICENSES.md):
  * MediaPipe Selfie Segmenter (general 256x256, landscape 144x256), Selfie Multiclass 256x256  (tflite -> onnx via tf2onnx)
  * PP-HumanSeg v2 lite 192x192 / portrait 256x144 (Paddle inference model -> onnx via paddle2onnx)
  * RVM MobileNetV3 fp32, MODNet, SINet: already ONNX, copied as-is.

Usage: python tools/models/convert_models.py
"""
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CAND = os.path.join(ROOT, "models", "candidates")
OUT = os.path.join(ROOT, "models", "converted")
os.makedirs(OUT, exist_ok=True)
PY = sys.executable


def run(cmd):
    print("+", " ".join(cmd), flush=True)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-3000:])
        print(r.stderr[-3000:])
        raise SystemExit(f"command failed: {cmd[0]}")
    return r


def tflite_to_onnx(name, out_name):
    src = os.path.join(CAND, name)
    dst = os.path.join(OUT, out_name)
    if os.path.exists(dst):
        print("exists", dst)
        return
    run([PY, "-m", "tf2onnx.convert", "--tflite", src, "--output", dst, "--opset", "15"])


def paddle_to_onnx(model_dir, out_name):
    dst = os.path.join(OUT, out_name)
    if os.path.exists(dst):
        print("exists", dst)
        return
    run([os.path.join(os.path.dirname(PY), "Scripts", "paddle2onnx.exe") if os.name == "nt" else "paddle2onnx", "--model_dir", model_dir, "--model_filename", "model.pdmodel",
         "--params_filename", "model.pdiparams", "--save_file", dst, "--opset_version", "13"])


def copy(name, out_name):
    dst = os.path.join(OUT, out_name)
    if not os.path.exists(dst):
        shutil.copyfile(os.path.join(CAND, name), dst)
        print("copied", out_name)


tflite_to_onnx("selfie_segmenter.tflite", "mediapipe_selfie_general_256.onnx")
import fix_mediapipe_ops  # noqa: E402
fix_mediapipe_ops.fix(os.path.join(OUT, "mediapipe_selfie_general_256.onnx"))
tflite_to_onnx("selfie_segmenter_landscape.tflite", "mediapipe_selfie_landscape_144x256.onnx")
fix_mediapipe_ops.fix(os.path.join(OUT, "mediapipe_selfie_landscape_144x256.onnx"))
tflite_to_onnx("selfie_multiclass_256x256.tflite", "mediapipe_selfie_multiclass_256.onnx")
paddle_to_onnx(os.path.join(CAND, "pphumanseg_lite", "human_pp_humansegv2_lite_192x192_inference_model"),
               "pphumanseg_v2_lite_192.onnx")
paddle_to_onnx(os.path.join(CAND, "pphumanseg_portrait", "portrait_pp_humansegv2_lite_256x144_inference_model"),
               "pphumanseg_v2_portrait_256x144.onnx")
copy("rvm_mobilenetv3_fp32.onnx", "rvm_mobilenetv3_fp32.onnx")
copy("modnet_xenova.onnx", "modnet_portrait.onnx")
copy("SINet_Softmax_simple.onnx", "sinet_portrait_320.onnx")

# Report I/O of every converted model
import onnx  # noqa: E402

for f in sorted(os.listdir(OUT)):
    if not f.endswith(".onnx"):
        continue
    m = onnx.load(os.path.join(OUT, f), load_external_data=False)

    def shp(t):
        return [(d.dim_value if d.dim_value else d.dim_param) for d in t.type.tensor_type.shape.dim]

    print(f"== {f}: opset {[o.version for o in m.opset_import if o.domain == '']}, size {os.path.getsize(os.path.join(OUT, f)) // 1024} KB")
    for i in m.graph.input:
        print("   in ", i.name, shp(i))
    for o in m.graph.output:
        print("   out", o.name, shp(o))

# PP-HumanSeg: fix input shapes (the export uses dynamic dims which leaves an
# adaptive pool with an unresolved kernel) and drop the trailing ArgMax so the
# softmax probabilities are exposed.
import onnxsim  # noqa: E402


def finalize_pphumanseg(name, shape):
    p = os.path.join(OUT, name)
    m = onnx.load(p)
    g = m.graph
    argmax = [n for n in g.node if n.op_type == "ArgMax"]
    if argmax:
        node = argmax[0]
        src = node.input[0]
        g.node.remove(node)
        g.output.remove(g.output[0])
        g.output.append(onnx.helper.make_tensor_value_info(src, onnx.TensorProto.FLOAT, ["N", 2, "H", "W"]))
    simp, ok = onnxsim.simplify(m, overwrite_input_shapes={"x": shape})
    assert ok
    onnx.save(simp, p)
    print("finalized", name)


finalize_pphumanseg("pphumanseg_v2_lite_192.onnx", [1, 3, 192, 192])
finalize_pphumanseg("pphumanseg_v2_portrait_256x144.onnx", [1, 3, 144, 256])

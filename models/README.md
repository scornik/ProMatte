# Models

ProMatte does not train models. It runs ONNX models produced from published,
permissively licensed sources. See
[THIRD_PARTY_LICENSES.md](../THIRD_PARTY_LICENSES.md) for the licence of every
model and [docs/model-evaluation.md](../docs/model-evaluation.md) for benchmarks.

## Layout

```
models/
  candidates/   raw downloads (tflite / Paddle / onnx) - not committed
  converted/    ONNX models ready for the plugin       - not committed
```

`data/models/manifest.json` is the shipped catalogue: id, file name, licence,
download URL, SHA-256, tensor layout, normalisation, output interpretation and
the resolution tiers used by the quality controller.

## Reproducing the converted models

```powershell
py -3.12 -m pip install -r ../tools/models/requirements.txt
py -3.12 ../tools/models/convert_models.py      # downloads + converts + fixes
py -3.12 ../tools/models/update_manifest.py     # refreshes size + sha256
```

`convert_models.py`
* MediaPipe `.tflite` → ONNX with tf2onnx (opset 15), then
  `fix_mediapipe_ops.py` replaces the MediaPipe custom op
  `Convolution2DTransposeBias` with a standard `ConvTranspose` (weights
  transposed OHWI → IOHW) so ONNX Runtime can execute the graph.
* PP-HumanSeg Paddle inference models → ONNX with paddle2onnx (opset 14), then
  `fix_pphumanseg.py` pins the input shape, drops the trailing `ArgMax` (so the
  two-class softmax is exposed) and rewrites the SPPM adaptive average pools as
  exact `AveragePool` (+`Gather`) for the now-known feature-map size.
* RVM and MODNet are used exactly as published (checksum pinned).

## Which models ship where

| Model | Bundled with the installer | Downloaded on demand |
| ----- | -------------------------- | -------------------- |
| MediaPipe Selfie landscape / square / multiclass | yes | – |
| PP-HumanSeg v2 lite / portrait | yes | – |
| Robust Video Matting (GPL-3.0, 15 MB) | no | yes |
| MODNet (Apache-2.0, 25 MB) | no | yes |

Downloaded models live in
`%APPDATA%\obs-studio\plugin_config\promatte\models\` and are verified against
the manifest's SHA-256 before use. They survive plugin updates and uninstall.

## Adding your own model

*Filters → ProMatte → Model manager → Custom ONNX model*. The file is treated
like a MODNet-style matting network: single input `NCHW` RGB normalised to
[-1, 1], single output alpha in [0, 1], dimensions a multiple of 32. Models
with a different contract need a manifest entry (see the schema in
`data/models/manifest.json`).

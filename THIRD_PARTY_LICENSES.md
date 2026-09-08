# Third-party components and licences

ProMatte itself is licensed under the GNU General Public License v2.0 or later
(see [LICENSE](LICENSE)), the customary licence for OBS Studio plugins.
Everything below is either linked into the plugin, shipped next to it, or
downloaded by the Model Manager at the user's request. Nothing in this list
phones home; the only network activity in ProMatte is the explicit model
download.

## Runtime libraries shipped with the plugin

| Component | Version | Licence | Use | Notes |
| --------- | ------- | ------- | --- | ----- |
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) (`onnxruntime.dll`, `onnxruntime_providers_shared.dll`) | 1.24.4 (Microsoft.ML.OnnxRuntime.DirectML NuGet) | MIT | inference engine (CPU + DirectML execution providers) | Copyright (c) Microsoft Corporation. `ThirdPartyNotices.txt` from the package applies to its own dependencies. |
| [DirectML](https://www.nuget.org/packages/Microsoft.AI.DirectML) (`DirectML.dll`) | 1.15.4 | Microsoft Software License Terms for DirectML (redistributable) | GPU acceleration on any D3D12 GPU (NVIDIA / AMD / Intel) | Closed-source Microsoft binary. The licence permits redistribution "in applications and services you develop … that run on Windows". Microsoft notices are preserved unmodified. This is the only non-open-source component and it is optional at runtime: without it the plugin falls back to the CPU provider. |
| [libobs](https://github.com/obsproject/obs-studio) | 31.1.1 headers/import library (runs against the installed OBS) | GPL-2.0-or-later | OBS plugin API | Not redistributed; provided by OBS Studio itself. |
| [nlohmann/json](https://github.com/nlohmann/json) | from obs-deps 2025-07-11 | MIT | model manifest parsing | Header-only, compiled into the plugin. |
| [doctest](https://github.com/doctest/doctest) | 2.4.11 | MIT | unit / integration tests | Test-only, not shipped. |
| Windows SDK APIs (DXGI, D3D12, WinHTTP, PSAPI) | – | Windows OS components | adapter enumeration, DirectML device creation, model download, statistics | Part of Windows. |

## AI models

Bundled models are installed with the plugin in `data/obs-plugins/promatte/models/`.
Downloadable models are fetched from the URLs below with a pinned SHA-256
(see `data/models/manifest.json`) and stored in
`%APPDATA%\obs-studio\plugin_config\promatte\models\`.

| Model | Origin | Licence | Distribution | Modifications |
| ----- | ------ | ------- | ------------ | ------------- |
| Robust Video Matting, MobileNetV3 (`rvm_mobilenetv3_fp32.onnx`) | Lin et al., ByteDance — https://github.com/PeterL1n/RobustVideoMatting | GPL-3.0 | **download on demand** from the authors' GitHub release v1.0.0 | none (byte-identical, checksum pinned) |
| MODNet portrait matting (`modnet_portrait.onnx`) | Ke et al., CUHK — https://github.com/ZHKKKe/MODNet ; ONNX conversion mirrored at https://huggingface.co/Xenova/modnet | Apache-2.0 | **download on demand** | none (checksum pinned) |
| MediaPipe Selfie Segmenter (landscape 256×144, general 256×256) | Google — https://developers.google.com/mediapipe/solutions/vision/image_segmenter | Apache-2.0 (code); model card CC-BY-4.0 | bundled | converted from TFLite to ONNX with tf2onnx; the MediaPipe custom op `Convolution2DTransposeBias` replaced by a standard `ConvTranspose` (`tools/models/fix_mediapipe_ops.py`) |
| MediaPipe Selfie Multiclass 256×256 | Google | Apache-2.0 / CC-BY-4.0 | bundled | converted from TFLite to ONNX with tf2onnx |
| PP-HumanSeg v2 lite 192×192 and portrait 256×144 | PaddlePaddle / PaddleSeg — https://github.com/PaddlePaddle/PaddleSeg | Apache-2.0 | bundled | exported with paddle2onnx; trailing ArgMax removed so soft probabilities are available |
| SINet portrait 320×320 | NAVER Corp. — https://github.com/clovaai/ext_portrait_segmentation | MIT | bundled | ONNX export as published by the obs-backgroundremoval project (`SINet_Softmax_simple.onnx`) |

Attribution for the CC-BY-4.0 model cards: "Selfie Segmentation" and
"Multiclass Selfie Segmentation" models by Google LLC, licensed under
CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/).

Models that were evaluated and **rejected for licensing reasons**: BRIA RMBG
1.4 / 2.0 (non-commercial licence). Models rejected for technical reasons are
listed in `docs/architecture-research.md`.

## Build-time tooling (not shipped)

tf2onnx (Apache-2.0), TensorFlow (Apache-2.0), paddle2onnx (Apache-2.0),
PaddlePaddle (Apache-2.0), onnx (Apache-2.0), onnxsim (Apache-2.0),
OpenCV-Python (Apache-2.0), obsws-python (MIT), Inno Setup (Inno Setup
licence), CMake (BSD-3), Visual Studio Build Tools.

## Visual test assets (not shipped)

Public-domain photographs listed in `tests/visual/assets/SOURCES.md` (official
White House portraits, US federal government works).

# ProMatte — Architecture Research

Research phase findings that drove the design of ProMatte, the real-time AI
background removal filter for OBS Studio. Written before implementation and
updated with measured data from `tools/benchmark` (see
[model-evaluation.md](model-evaluation.md) for the full benchmark tables).

Research date: September 2026. Target OBS: 31.x / 32.x (tested on 32.2.2).

---

## 1. OBS Studio plugin and rendering architecture

### 1.1 Plugin model

* A plugin is a shared library (`promatte.dll`) that exports
  `obs_module_load()` and registers one or more `obs_source_info` structures via
  `obs_register_source()`. Filters are sources of type `OBS_SOURCE_TYPE_FILTER`
  with `OBS_SOURCE_VIDEO` in `output_flags`.
* Plugin layout on Windows (both are honoured by OBS 30+):
  * inside the OBS install: `obs-plugins/64bit/<plugin>.dll` +
    `data/obs-plugins/<plugin>/…`
  * per machine, no admin needed: `%ProgramData%\obs-studio\plugins\<plugin>\bin\64bit\`
    + `…\data\`
* Module data (`effects/`, `locale/`, `models/`) is resolved with
  `obs_module_file()`; per-user writable storage with `obs_module_config_path()`
  (`%APPDATA%\obs-studio\plugin_config\<plugin>\`). Filter settings are stored by
  OBS itself in the scene collection JSON, so persistence across restarts and
  updates comes for free.
* The official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)
  pins `obs-studio 31.1.1` and the `obs-deps 2025-07-11` prebuilt bundle;
  libobs' ABI is stable across minor versions, which is why a plugin built
  against 31.1.1 loads in 32.2.2. ProMatte follows the same dependency versions
  and builds libobs from source via `tools/setup-deps.ps1`.

### 1.2 Filter render path (libobs graphics)

* `video_render(data, effect)` runs on the OBS **graphics thread** for every
  output frame. Anything slow here directly reduces OBS' render FPS ("rendering
  lag"), which is the main failure mode of naïve AI filters.
* A filter obtains its input by calling `obs_source_process_filter_begin()`
  (which renders the upstream source into an internal texture) and draws the
  result with `obs_source_process_filter_end()` inside its own render target.
* libobs' graphics API (`gs_*`) abstracts D3D11 (Windows) and OpenGL
  (macOS/Linux): `gs_texrender_t` render targets, `gs_texture_t` textures
  (`GS_DYNAMIC` for CPU uploads), `gs_stagesurf_t` staging surfaces for GPU→CPU
  readback (`gs_stage_texture` + `gs_stagesurface_map`), and `gs_effect_t`
  HLSL-like shaders (`.effect` files).
* Readback is the expensive operation: `gs_stagesurface_map()` blocks until
  the GPU copy has finished. The standard mitigation is a **ring of staging
  surfaces mapped one frame later**, which is what ProMatte does.
* OBS filters output straight (non-premultiplied) alpha; the compositor blends
  it. ProMatte returns `float4(rgb, alpha)` and disables sRGB framebuffer
  conversion for its intermediate passes so colours pass through unchanged.

### 1.3 Existing OBS background-removal plugins (reviewed on this machine)

| Plugin | Inference | Models | Notes |
| ------ | --------- | ------ | ----- |
| obs-backgroundremoval 1.4.1 (occ-ai) | ONNX Runtime 1.12 (DirectML / CUDA / CPU) | MediaPipe selfie, PP-HumanSeg, SINet, RVM, selfie-multiclass, BRIA RMBG (non-commercial) | Readback of the whole frame at AI res; mask blur/contour filter; no motion-adaptive temporal filter; UI exposes ML terms (threshold, contour, "mask every X frames"). GPL-2. |
| obs-virtualbg (kounoike) | ONNX Runtime + DirectML | one bundled model | Minimal UI, no refinement. |
| Bria background removal 0.1.2 | proprietary + Sentry telemetry | RMBG | Telemetry and licensing make it unsuitable as a reference. |
| live-backgroundremoval-lite | ncnn (Vulkan) | undocumented | Closed model source. |

Common weaknesses: synchronous or barely decoupled inference, binary/blurred
masks (no edge-aware upsampling), no decontamination, no adaptive quality, and
GPU selection defaulting to adapter 0 (on laptops that is usually the iGPU).
All of these are addressed in ProMatte's design.

---

## 2. Inference engines

| Engine | Windows NVIDIA | Windows AMD/Intel | macOS | Linux | Redistribution | Verdict |
| ------ | -------------- | ----------------- | ----- | ----- | -------------- | ------- |
| **ONNX Runtime 1.24.4** (MIT) | CPU, DirectML, CUDA EP, TensorRT EP | CPU, DirectML | CPU, CoreML EP | CPU, CUDA | permissive | **Selected** — one runtime, every vendor |
| DirectML 1.15.4 (Microsoft redistributable) | yes (D3D12) | yes | – | – | allowed inside apps | **Selected** as the Windows GPU path |
| TensorRT | fastest on NVIDIA | – | – | yes | ~1 GB runtime + CUDA/cuDNN, engine build takes minutes | supported by the backend code when present; not bundled |
| CUDA EP | yes | – | – | yes | needs CUDA 12 + cuDNN 9 (~1.5 GB) | same as above |
| OpenVINO | – | Intel only | – | Intel | ~150 MB | supported via EP name; not bundled |
| CoreML | – | – | Apple Silicon | – | – | supported via EP name; macOS build not yet produced |
| ncnn / Vulkan | yes | yes | Metal via MoltenVK | yes | permissive | rejected: no RVM/MODNet operator coverage without conversions, weaker tooling |

Why ONNX Runtime + DirectML:

1. **No vendor lock-in.** DirectML runs on every D3D12-capable GPU (NVIDIA,
   AMD, Intel, Qualcomm) including the old Maxwell GPU in the test laptop, which
   TensorRT 10 no longer supports at all.
2. **Single binary.** The DirectML build of `onnxruntime.dll` (12 MB) contains
   both the DML and CPU providers, so CPU fallback needs no extra files.
3. **Optional acceleration is not faked.** The backend enumerates
   `Ort::GetAvailableProviders()` at runtime; TensorRT/CUDA/CoreML/OpenVINO code
   paths exist and are exercised only when an ONNX Runtime build that contains
   them is installed. The UI lists only providers that are actually available.
4. **Adapter control.** With the `SessionOptionsAppendExecutionProvider_DML1`
   API we create the D3D12 device on the adapter OBS renders on. On this laptop
   the default (adapter 0 = Intel HD 620) took 30–76 s to compile the first
   session; selecting the NVIDIA adapter cut that to ~3 s.

DirectML.dll is a closed-source Microsoft binary under a redistribution
license that explicitly permits shipping it inside applications. It is the
only practical vendor-neutral GPU path on Windows; this is called out in
[THIRD_PARTY_LICENSES.md](../THIRD_PARTY_LICENSES.md).

---

## 3. Segmentation / matting models

### 3.1 Candidates and licences

| Model | Type | Input | Size | Licence | Redistributable |
| ----- | ---- | ----- | ---- | ------- | --------------- |
| Robust Video Matting, MobileNetV3 (RVM) | recurrent video matting, alpha + foreground colour | any (multiple of 32), internal downsample ratio | 15 MB | GPL-3.0 | yes (plugin is GPL-2.0-or-later; downloaded on demand from the authors' GitHub release) |
| MODNet | trimap-free portrait matting | any (multiple of 32) | 25 MB | Apache-2.0 | yes (download from the Xenova/modnet ONNX mirror, checksum pinned) |
| MediaPipe Selfie Segmenter (landscape 256×144, general 256×256) | binary person segmentation | fixed | 0.45 MB | Apache-2.0 code, CC-BY-4.0 model card | yes (bundled) |
| MediaPipe Selfie Multiclass 256×256 | 6-class (hair/skin/clothes…) | fixed | 16 MB | Apache-2.0 / CC-BY-4.0 | yes (bundled) |
| PP-HumanSeg v2 lite 192×192 / portrait 256×144 | binary person segmentation | fixed | 12 / 4 MB | Apache-2.0 | yes (bundled) |
| SINet 320×320 | binary portrait segmentation | fixed | 0.4 MB | MIT | yes (bundled) |
| BiRefNet / BiRefNet-lite | dichotomous segmentation | 1024² | 170–900 MB | MIT | too heavy for real time on consumer GPUs (not evaluated) |
| BRIA RMBG 1.4 / 2.0 | segmentation | 1024² | 44 MB (int8) | non-commercial | **rejected** (licence) |
| BackgroundMattingV2 | matting with clean plate | – | – | MIT | rejected (needs a background capture) |

Conversion notes (all reproducible with `tools/models/convert_models.py`):

* MediaPipe `.tflite` → ONNX via tf2onnx (opset 15). The graphs contain the
  MediaPipe custom op `Convolution2DTransposeBias`; `fix_mediapipe_ops.py`
  replaces it with a standard `ConvTranspose` + bias (weights transposed OHWI →
  IOHW).
* PP-HumanSeg Paddle inference models → ONNX via paddle2onnx (opset 14); the
  trailing `ArgMax` is removed to expose the two-class softmax, and the graph
  is simplified with fixed input shapes (the dynamic export leaves an adaptive
  pool with an unresolved kernel).
* RVM and MODNet are used as published (ONNX, opset 12 / 11).

### 3.2 What a webcam product needs from the model

Static segmentation accuracy is not the deciding factor. For a live camera:

* **Temporal stability** — flicker and mask crawl are the most visible defects.
  RVM carries recurrent state (r1–r4) across frames and is dramatically more
  stable than per-frame models; ProMatte additionally applies motion-adaptive
  smoothing to every model.
* **Soft alpha** — matting models (RVM, MODNet) produce real alpha for hair;
  segmentation models produce probabilities that look "cut out" after
  thresholding. The confidence curve and joint-bilateral upsampling in ProMatte
  recover some softness, but the base model matters.
* **Foreground colour** — RVM also predicts the clean foreground colour, which
  gives principled edge decontamination (no more background-coloured fringes).
  For other models ProMatte estimates the background colour from a
  person-excluded blur and un-mixes the edge pixels.
* **Cost / resolution scaling** — dynamic-shape models (RVM, MODNet) allow the
  performance controller to trade resolution for speed; fixed-shape models only
  have one tier.

### 3.3 Benchmark summary (this machine)

Test machine: Intel Core i7-7500U (2C/4T), 32 GB RAM, NVIDIA GeForce 940MX
(4 GB, Maxwell), Intel HD 620, Windows 11 Pro N, OBS 32.2.2. This is
deliberately a **low-end** target; a desktop RTX/RX card is 10–30× faster.

The full tables (all models × backends × tiers, JSON in
`tools/benchmark/results/`) are in [model-evaluation.md](model-evaluation.md).
Key numbers from the run on real portrait frames:

| Model | DirectML (940MX) inference | CPU (2 threads) inference | Flicker | Softness | Notes |
| ----- | -------------------------: | ------------------------: | ------: | -------: | ----- |
| RVM MobileNetV3, Performance 512×288 | 71 ms | 90 ms | 0.0040 | 0.024 | best edges + temporal; too heavy for this GPU, right on mid-range and up |
| RVM MobileNetV3, Balanced 640×352 | 170 ms | 157 ms | 0.0038 | 0.020 | |
| MODNet, Performance 320×192 | 158 ms | 149 ms | 0.0000 | 0.017 | soft hair, no temporal state |
| PP-HumanSeg v2 lite 192×192 | 21 ms | 19 ms | 0.0082 | 0.124 | best value, bundled |
| PP-HumanSeg v2 portrait 256×144 | 29 ms | 18 ms | 0.0045 | 0.027 | bundled |
| MediaPipe Selfie landscape 256×144 | 5 ms | 6 ms | 0.0000 | 0.037 | cheapest; Auto's starting point |
| MediaPipe Selfie Multiclass 256×256 | 182 ms | 197 ms | 0.0040 | 0.050 | expensive for its quality |
| SINet 320×320 | 46 ms | 52 ms | – | – | rejected (inverts on complex backgrounds) |

### 3.4 Decision

* **Default model on GPU: Robust Video Matting (MobileNetV3)** at the
  *Performance* or *Balanced* tier chosen by the performance controller. It has
  the best hair quality, built-in temporal stability and a usable foreground
  prediction, and its cost scales with resolution.
* **Default model on CPU-only machines: PP-HumanSeg v2 lite** (192×192,
  Apache-2.0, bundled), with MediaPipe Selfie (landscape) as the cheapest
  fallback for very weak CPUs. Both stay well inside the CPU budget while the
  GPU pipeline does the edge work.
* **MODNet** is offered as an optional download for users who want the softest
  hair on still-ish shots and have GPU headroom; it has no temporal state so
  it relies fully on ProMatte's stabiliser.
* **Selfie Multiclass** is bundled for hair-aware segmentation on machines
  where RVM is not downloaded yet.

Because RVM is GPL-3.0 and 15 MB it is **not bundled**; the Model Manager
downloads it from the authors' GitHub release with a pinned SHA-256. All other
bundled models are Apache-2.0 / MIT / CC-BY-4.0.

---

## 4. Architecture decisions

| Decision | Choice | Rationale |
| -------- | ------ | --------- |
| Threading | one inference worker thread per filter, single-slot latest-frame-wins mailbox | never blocks the graphics thread; bounded latency; no queue growth |
| GPU↔CPU transfer | GPU downscale to AI resolution → staging ring (1-frame delay) → CPU; matte uploaded as a small RGBA8 texture | transfer ≤ 2.6 MB/frame at the Ultra tier, 0.6 MB at Performance; no full-resolution readback |
| Matte refinement | CPU at AI resolution (confidence curve, morphology, component filter, temporal) + GPU at output resolution (joint bilateral upsampling, feather, decontamination, halo removal) | cheap where data is small, edge-accurate where it matters |
| Background blur | person-excluded dual-Kawase blur at half resolution, re-normalised | no ghost halo of the subject in the blur; the same texture is the background estimate for decontamination |
| Quality control | tier ladder per model + hysteresis controller on the worker's total per-frame time | adapts within seconds, cannot oscillate |
| Model management | JSON manifest with licences, URLs, SHA-256; bundled small models; WinHTTP downloads with checksum verification | legal clarity, offline-first, no installer bloat |
| Privacy | no network code except the explicit model download; no telemetry | requirement |

Alternatives considered and rejected:

* **Zero-copy D3D11→DirectML texture sharing** via `CreateGPUAllocationFromD3DResource`.
  Requires D3D11/D3D12 shared handles and NV12/RGBA→float conversion on the
  device; complexity is high and the saving (< 1 ms) is negligible next to the
  model itself at the sizes we use. Left as a future optimisation.
* **Running the model at full 1080p.** Benchmarks show RVM at 1280×704 costs
  5× the Performance tier for edge gains that the joint-bilateral upsampler
  already provides at a fraction of the cost.
* **Qt UI.** OBS' property system is sufficient for a professional panel and
  avoids the Qt dependency; live statistics are shown on demand and in the
  developer overlay.

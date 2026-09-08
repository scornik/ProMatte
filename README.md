# ProMatte — AI Background Removal for OBS Studio

ProMatte is a professional, fully local, real-time AI background removal and
replacement filter for OBS Studio. Add it to your webcam and choose *Remove*,
*Blur*, *Replace* (colour / image / video / another OBS source) or *Dim*.

* Runs entirely on your PC — no cloud, no account, no telemetry, no frames
  ever leave the machine.
* GPU acceleration on NVIDIA, AMD and Intel (DirectML); automatic CPU fallback.
* Never lags OBS: inference runs on its own thread with a latest-frame-wins
  mailbox; the render thread only ever uses the newest matte.
* Professional edges: joint-bilateral upsampling, feathering, motion-adaptive
  temporal stabilisation, edge decontamination and halo removal.
* Adaptive quality: an *Auto* mode picks the model and AI resolution for your
  hardware and steps down gracefully when the machine is busy.
* Model Manager with licence-checked models, checksums and offline operation.

## Quick start (users)

1. Run `ProMatte-Setup.exe` (Windows 10/11 64-bit, OBS 30.0 or newer).
2. Start OBS, right-click your webcam source → **Filters** → **+** → **ProMatte AI Background Removal**.
3. Leave *Background* on **Remove (transparent)** (or pick Blur / Replace) and *Quality* on **Auto**. Done.
4. Optional: in the *Model manager* section download **Robust Video Matting**
   for the best hair detail on GPUs (15 MB, one-time download).

See [docs/troubleshooting.md](docs/troubleshooting.md) if something looks wrong.

## Requirements

| | Minimum | Recommended |
| - | - | - |
| OS | Windows 10 1903 64-bit | Windows 11 |
| OBS | 30.0 | 31 / 32 |
| GPU | any D3D12-capable GPU, or CPU only | NVIDIA GTX 1050 / AMD RX 560 / Intel Arc or newer |
| CPU (CPU-only mode) | 4 threads | 8 threads |
| Camera | 720p | 1080p 30 fps |

## Building from source

Prerequisites: Visual Studio 2022/2026 Build Tools (C++ workload), CMake ≥ 3.28,
Python 3.12 (for model conversion only), Inno Setup 6 (installer only).

```powershell
# 1. download and build dependencies (obs-studio 31.1.1 + obs-deps, ONNX Runtime, DirectML)
powershell -ExecutionPolicy Bypass -File tools/setup-deps.ps1

# 2. convert the bundled models (one-time; needs Python + tf2onnx/paddle2onnx)
py -3.12 -m pip install -r tools/models/requirements.txt
py -3.12 tools/models/convert_models.py
py -3.12 tools/models/update_manifest.py

# 3. configure and build
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel

# 4. run the tests
build\bin\Release\promatte-unit-tests.exe
build\bin\Release\promatte-integration-tests.exe      # headless libobs

# 5. install into OBS for development (no admin: %ProgramData%\obs-studio\plugins)
cmake --build build --config Release --target deploy-user

# 6. build the installer
powershell -ExecutionPolicy Bypass -File installer/build-installer.ps1
```

The staged plugin tree is in `build/stage/` and mirrors the OBS install layout.

## Repository layout

```
CMakeLists.txt             build system (core library, plugin, tools, tests)
src/plugin/                module entry point
src/obs/                   OBS filter, settings, presets, background sources
src/rendering/             GPU pipeline (effects: downscale, refine, blur, composite), debug overlay
src/inference/             SegmentationBackend abstraction, model descriptors, worker thread
src/backends/              ONNX Runtime backend (DirectML / CUDA / TensorRT / CoreML / CPU providers)
src/preprocessing/         BGRA → tensor conversion, resizing
src/postprocessing/        CPU matte refinement (confidence curve, morphology, components)
src/temporal/              motion-adaptive temporal stabiliser
src/performance/           adaptive quality controller and statistics
src/models/                model manager + WinHTTP downloader
src/ui/                    OBS properties panel
data/                      effects, locale, model manifest (+ bundled models after conversion)
models/                    model documentation and conversion outputs
tools/benchmark/           promatte-bench (JSON benchmark utility)
tools/models/              conversion + manifest scripts
tests/unit, tests/integration, tests/visual
installer/                 Inno Setup script + build script
docs/                      research, architecture, performance, model evaluation, troubleshooting, verification
```

## Documentation

* [docs/architecture-research.md](docs/architecture-research.md) — ecosystem research and why ONNX Runtime + DirectML + RVM were chosen
* [docs/architecture.md](docs/architecture.md) — layers, threading, GPU pipeline, data flow
* [docs/model-evaluation.md](docs/model-evaluation.md) — benchmark tables
* [docs/performance.md](docs/performance.md) — performance controller, budgets, measured numbers
* [docs/troubleshooting.md](docs/troubleshooting.md)
* [docs/final-verification.md](docs/final-verification.md) — verification report
* [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)

## Privacy

ProMatte processes camera frames in memory on the local machine only. It does
not upload, save or transmit frames, does not collect usage data and does not
require an account. The only outgoing network request is the model download
that the user starts explicitly from the Model Manager (HTTPS to the model
authors' release pages, verified by SHA-256).

## License

GPL-2.0-or-later. Third-party components are listed in
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

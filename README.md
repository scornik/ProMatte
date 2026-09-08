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
| OS | Windows 10 1903 64-bit, Ubuntu 22.04 or macOS 11 | Windows 11 |
| OBS | 30.0 | 31 / 32 |
| GPU | any D3D12-capable GPU, or CPU only | NVIDIA GTX 1050 / AMD RX 560 / Intel Arc or newer |
| CPU (CPU-only mode) | 4 threads | 8 threads |
| Camera | 720p | 1080p 30 fps |

### Platform support

| Platform | Package | GPU acceleration | State |
| -------- | ------- | ---------------- | ----- |
| Windows x64 | `ProMatte-Setup-<version>.exe` | DirectML on any D3D12 GPU (NVIDIA / AMD / Intel); CUDA and TensorRT when an ONNX Runtime build providing them is installed | released and verified on real hardware, see [docs/final-verification.md](docs/final-verification.md) |
| Linux x86_64 | `.deb` and `.tar.gz` | CPU; CUDA when an ONNX Runtime build providing it is installed | builds, unit tests pass, package installs and the module loads; not yet exercised against a running OBS |
| macOS arm64 | `.zip` / `.tar.gz` of `promatte.plugin` | CPU; CoreML when an ONNX Runtime build providing it is installed | built, unit-tested and packaged by CI; never run inside OBS by the author, see the limitations in [docs/final-verification.md](docs/final-verification.md) |

Packages for Linux and macOS are produced by
[the build workflow](.github/workflows/build.yml) and attached to each run as
artifacts.

### Installing on Linux

```bash
sudo dpkg -i promatte_1.0.0_amd64.deb
```

Or, on a distribution without dpkg, the tarball holds the same `usr/` tree:

```bash
sudo tar xzf promatte_1.0.0_linux-x86_64.tar.gz --strip-components=1 -C /
```

Either way `promatte.so` lands in `/usr/lib/obs-plugins` and its data in
`/usr/share/obs/obs-plugins/promatte`, which is where a distribution OBS looks.
ONNX Runtime is shipped beside the module in `/usr/lib/obs-plugins/promatte/`
because no distribution packages it, and the module's `RUNPATH` points there.

### Installing on macOS

Unpack the archive and move the bundle into your plugins directory:

```bash
unzip ProMatte-1.0.0-macos-arm64.zip
mkdir -p ~/Library/Application\ Support/obs-studio/plugins
mv promatte.plugin ~/Library/Application\ Support/obs-studio/plugins/
```

The bundle carries its own ONNX Runtime in `Contents/Frameworks`, so there is
nothing else to install. It is unsigned and unnotarised, so Gatekeeper will
quarantine it; clear that with:

```bash
xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/promatte.plugin
```

## Building from source

### Windows

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

### Linux

```bash
sudo apt install build-essential cmake ninja-build pkg-config \
     libobs-dev libcurl4-openssl-dev nlohmann-json3-dev

# ONNX Runtime is not packaged by any distribution; use the upstream release
curl -fsSLO https://github.com/microsoft/onnxruntime/releases/download/v1.24.4/onnxruntime-linux-x64-1.24.4.tgz
tar xzf onnxruntime-linux-x64-1.24.4.tgz

python3 -m pip install -r tools/models/requirements.txt   # bundled models, one-time
python3 tools/models/convert_models.py && python3 tools/models/fix_pphumanseg.py

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DPROMATTE_ORT_ROOT="$PWD/onnxruntime-linux-x64-1.24.4"
cmake --build build --parallel
LD_LIBRARY_PATH="$PWD/onnxruntime-linux-x64-1.24.4/lib" ./build/bin/promatte-unit-tests
cd build && cpack          # produces the .deb and the .tar.gz
```

### macOS

`libobs` is not available as a Homebrew formula, so build the library on its own
first; [the CI workflow](.github/workflows/build.yml) does exactly this and is
the reference for the steps. Then configure with `-DCMAKE_PREFIX_PATH` pointing
at the resulting SDK and `-DPROMATTE_ORT_ROOT` at an
`onnxruntime-osx-arm64-<version>` tarball, build, and run `cpack` to get the
`.zip` and `.tar.gz` of the bundle.

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
src/models/                model manager + downloader (WinHTTP on Windows, libcurl elsewhere)
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

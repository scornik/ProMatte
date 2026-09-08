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

**Downloads:** [latest release](https://github.com/scornik/ProMatte/releases/latest)
— Windows installer, Linux `.deb`/`.tar.gz`, macOS bundle archive.

## Quick start

Install the package for your platform (below), then on every platform:

1. Start OBS and right-click your camera source → **Filters** → **+** →
   **ProMatte AI Background Removal**.
2. Leave *Background* on **Remove (transparent)** — or pick Blur / Replace / Dim
   — and *Quality* on **Auto**. Done.
3. Optional: in the *Model manager* section download **Robust Video Matting**
   for the best hair detail on GPUs (15 MB, one-time download).

Note that the presets tune matte quality and cost only. None of them crops or
zooms — "Tutorial / Talking head" does not frame your head for you. Use OBS'
own **Crop/Pad** filter or the source transform for that.

See [docs/troubleshooting.md](docs/troubleshooting.md) if something looks wrong.

### Windows

Run `ProMatte-Setup-1.0.2.exe`. Windows 10/11 64-bit, OBS 30.0 or newer. The
installer finds your OBS directory and removes any older copy for you.

### Linux

On Debian, Ubuntu and derivatives:

```bash
sudo dpkg -i promatte_1.0.2_amd64.deb
sudo apt-get install -f      # only if dpkg reported missing dependencies
```

On any other distribution the tarball carries the same `usr/` tree:

```bash
sudo tar xzf promatte_1.0.2_linux-x86_64.tar.gz --strip-components=1 -C /
```

Either way `promatte.so` lands in `/usr/lib/obs-plugins` and its data in
`/usr/share/obs/obs-plugins/promatte`, which is where a distribution OBS looks.
ONNX Runtime is shipped beside the module in `/usr/lib/obs-plugins/promatte/`
because no distribution packages it, and the module's `RUNPATH` points there.

Check that it resolved before starting OBS:

```bash
ldd /usr/lib/obs-plugins/promatte.so | grep "not found"     # no output is good
```

This will not work with a Flatpak or Snap OBS: those run in a sandbox with their
own plugin directory and their own libraries. Install OBS from your distribution
or from the official PPA instead.

### macOS — Apple Silicon (M1/M2/M3/M4) and Intel

One download covers both; the bundle is a universal binary.

```bash
# 1. unpack (the archive contains a versioned folder)
unzip ProMatte-1.0.2-macos-universal.zip

# 2. install for the current user
mkdir -p ~/Library/Application\ Support/obs-studio/plugins
mv ProMatte-1.0.2-macos-universal/promatte.plugin \
   ~/Library/Application\ Support/obs-studio/plugins/

# 3. the bundle is unsigned, so clear the quarantine flag Gatekeeper sets
xattr -dr com.apple.quarantine \
   ~/Library/Application\ Support/obs-studio/plugins/promatte.plugin
```

Then quit OBS completely — ⌘Q, not just closing the window — and start it again.
The bundle carries its own ONNX Runtime in `Contents/Frameworks`, so there is
nothing else to install.

**Do not use the 1.0.2 macOS build's predecessor.** 1.0.1 makes OBS report *"the
following OBS plugins failed to load: promatte"*: its binary had no rpath that
could reach `libobs` inside OBS.app, and the ONNX Runtime inside the bundle was
saved under the wrong filename. Both are fixed in 1.0.2. If you installed 1.0.1,
delete `~/Library/Application Support/obs-studio/plugins/promatte.plugin` before
installing the new one.

If the filter does not appear in the list, open **Help → Log Files → View
Current Log** and search for `promatte`: the module logs its version and the
backends it found as soon as OBS loads it, and dyld prints the exact unresolved
library if it did not.

## Requirements

| | Minimum | Recommended |
| - | - | - |
| OS | Windows 10 1903 64-bit, Ubuntu 22.04, or macOS 11 (Intel or Apple Silicon) | Windows 11 |
| OBS | 30.0 | 31 / 32 |
| GPU | any D3D12-capable GPU, or CPU only | NVIDIA GTX 1050 / AMD RX 560 / Intel Arc or newer |
| CPU (CPU-only mode) | 4 threads | 8 threads |
| Camera | 720p | 1080p 30 fps |

### Platform support

| Platform | Package | GPU acceleration | State |
| -------- | ------- | ---------------- | ----- |
| Windows x64 | `ProMatte-Setup-<version>.exe` | DirectML on any D3D12 GPU (NVIDIA / AMD / Intel); CUDA and TensorRT when an ONNX Runtime build providing them is installed | released and verified on real hardware, see [docs/final-verification.md](docs/final-verification.md) |
| Linux x86_64 | `.deb` and `.tar.gz` | CPU; CUDA when an ONNX Runtime build providing it is installed | builds, unit tests pass, package installs and the module loads; not yet exercised against a running OBS |
|  macOS (Intel + Apple Silicon) | `.zip` / `.tar.gz` of `promatte.plugin` | CPU; CoreML when an ONNX Runtime build providing it is installed | 1.0.1 failed to load in OBS on a real Mac; 1.0.2 fixes the two linkage faults that caused it, and CI now proves every load command resolves against the bundle. Still not run inside OBS by the author — see [docs/final-verification.md](docs/final-verification.md) |

Packages for Linux and macOS are produced by
[the build workflow](.github/workflows/build.yml) and attached to each run as
artifacts.

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

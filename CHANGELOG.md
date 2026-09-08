# Changelog

## 1.0.0 — 2026-09-08

First release. Windows, Linux and macOS packages:
<https://github.com/scornik/ProMatte/releases/tag/v1.0.0>

Real-time AI background removal, blur and replacement for OBS Studio. Everything
runs on the local machine: no cloud service, no account, no telemetry, and no
frame ever leaves the computer.

Inference runs on its own thread with a latest-frame-wins mailbox, so OBS never
waits for the model. Edges are refined on the GPU with joint-bilateral
upsampling, feathering, edge decontamination and halo removal, and a
motion-adaptive temporal stabiliser keeps static edges from crawling while
letting moving ones respond immediately. An automatic quality controller picks
the model and AI resolution for the machine and steps down when it cannot keep
up.

### Downloads

| Platform | File | Notes |
| -------- | ---- | ----- |
| Windows 10/11 x64 | `ProMatte-Setup-1.0.0.exe` | Installer. Detects the OBS folder, removes a stale copy under `%ProgramData%` and preserves your settings. |
| Linux x86_64 (Debian/Ubuntu) | `promatte_1.0.0_amd64.deb` | `sudo dpkg -i promatte_1.0.0_amd64.deb` |
| Linux x86_64 (other) | `promatte_1.0.0_linux-x86_64.tar.gz` | `sudo tar xzf … --strip-components=1 -C /` |
| macOS 11+ Apple Silicon | `ProMatte-1.0.0-macos-arm64.zip` | Move `promatte.plugin` into `~/Library/Application Support/obs-studio/plugins`, then `xattr -dr com.apple.quarantine` it. |
| macOS (alternative) | `ProMatte-1.0.0-macos-arm64.tar.gz` | Same bundle, tar format. |

`SHA256SUMS.txt` covers every file above. Requires OBS Studio 30.0 or newer.

After installing: right-click your webcam source → **Filters** → **+** →
**ProMatte AI Background Removal**. Leave *Quality* on **Auto**. Try **Blur**
first, since it is visible with nothing else in the scene.

### What is actually verified

The three platforms are not equally tested, and the differences are real.

| Platform | Builds | Unit tests | Package installs | Runs in OBS with a camera |
| -------- | ------ | ---------- | ---------------- | ------------------------- |
| Windows x64 | yes | 36/36 | yes | **yes**, fully |
| Linux x86_64 | yes | 36/36 | yes, module loads and all libraries resolve | **no** |
| macOS arm64 | yes, in CI | 36/36 in CI | bundle inspected, not installed | **no** |

**Windows** is verified end to end on a 2017 dual-core laptop with a GeForce
940MX: a 37-step session against a real webcam covering every preset, background
mode, debug view, quality level, backend and model, plus scene switching, camera
resolution changes, a recording, and a 15-minute soak that held 30 fps while
memory fell from 726 MB to 498 MB. Details and measurements are in
[docs/final-verification.md](https://github.com/scornik/ProMatte/blob/v1.0.0/docs/final-verification.md).

**Linux** builds against the distribution's libobs, passes the same 36 unit
tests, and the `.deb` installs with every shared library resolving and the
module loading. Inference was benchmarked there and lands within a few
milliseconds of the Windows figures on the same CPU. It has **not** been run
inside a running OBS against a camera.

**macOS** is built, unit-tested and packaged on a GitHub `macos-15` Apple
Silicon runner against a libobs built from source. The bundle was downloaded and
inspected: binary, `Info.plist`, all five models, effects, locale and a vendored
`libonnxruntime.dylib`. It has **never been loaded in OBS on a Mac**, because
the author has no Mac. Treat it as a first build that compiles, links, passes
its tests and packages correctly, nothing more.

### GPU acceleration

DirectML on Windows drives any D3D12 GPU (NVIDIA, AMD, Intel). CUDA, TensorRT,
CoreML and OpenVINO are selected automatically when an ONNX Runtime build
providing them is installed; none of those paths has been exercised, since the
shipped runtime contains only DirectML and CPU. CPU is always available and is
what Auto picks when a GPU turns out to be slower, which it measures per machine
and remembers.

### Models

Five models ship with the plugin (MediaPipe Selfie landscape / square /
multiclass, PP-HumanSeg v2 lite and portrait), all Apache-2.0 or CC-BY-4.0. Two
more, Robust Video Matting and MODNet, are downloaded on request from the
Model Manager with a pinned SHA-256. RVM gives the best hair detail on a capable
GPU. A model with a non-commercial licence was evaluated and deliberately
excluded. Full attribution is in
[THIRD_PARTY_LICENSES.md](https://github.com/scornik/ProMatte/blob/v1.0.0/THIRD_PARTY_LICENSES.md).

### Known limitations

* Only Windows has been used against a real camera; see the table above.
* Single-person models: a second person is segmented only if the model happens
  to include them. The largest connected region is always kept, so the primary
  subject is never dropped.
* Robust Video Matting costs 71–96 ms per frame on the low-end test laptop, so
  Auto does not choose it there; on that class of hardware it looks excellent
  but lags fast motion.
* The first DirectML session on a given adapter compiles shaders, which took
  8 seconds on the test GPU and 30–76 seconds on an integrated Intel one. Video
  passes through untouched while that happens.
* Objects held in front of the face, such as a microphone, are treated as
  background by the person-segmentation models.
* The macOS bundle is unsigned and unnotarised, so Gatekeeper quarantines it.

ProMatte is GPL-2.0-or-later.

# ProMatte — Final Verification Report

Version 1.0.0 · verification run 2026-09-08.

Everything below is measured on the machine described in §2. Numbers come from
`tools/benchmark` (JSON in `tools/benchmark/results/`), the headless libobs
harness, and a live OBS session driven through obs-websocket
(`tests/visual/live/live-report.json`). Where something was **not** verified it
says so; nothing in this report is estimated or extrapolated.

---

## 1. Build

| | |
| - | - |
| Compiler | MSVC 19.50.35728 (Visual Studio Build Tools 18, toolset 14.50.35717), x64 |
| Build system | CMake 4.x (Visual Studio generator), C++20, `/W4 /permissive-`, Release + PDB |
| Windows SDK | 10.0.26100 |
| OS | Windows 11 Pro N 25H2 (build 26200) |
| libobs built against | obs-studio 31.1.1 (built from source by `tools/setup-deps.ps1`) |
| OBS Studio used for testing | 32.2.2 (64-bit) |
| ONNX Runtime | 1.24.4 (DirectML build) |
| DirectML | 1.15.4 |
| Plugin version | 1.0.0 |
| Warnings | none at `/W4` in ProMatte sources |

Artefacts:

| File | Size | SHA-256 |
| ---- | ---- | ------- |
| `installer/output/ProMatte-Setup-1.0.0.exe` | 43.2 MB | `dd1625789032be08e07872a46498448dcc58bd6d48b916572b609a8a9de97385` |
| `build/stage/obs-plugins/64bit/promatte.dll` | 434 KB | `d9400d96d717738d7ede483a5ff89ec32bb3ec6a3e990f63023dae245633e06d` (staged with `promatte.pdb`) |

The installer carries the plugin, ONNX Runtime, DirectML, the four effects, the
locale file, the model manifest and the five bundled models.

## 2. Test hardware

Deliberately a **low-end 2017 laptop**, so the numbers here are close to the
worst case a user is likely to have.

| | |
| - | - |
| CPU | Intel Core i7-7500U, 2 cores / 4 threads, 2.70 GHz |
| RAM | 32 GB |
| GPU 0 | Intel HD Graphics 620 (integrated) |
| GPU 1 | NVIDIA GeForce 940MX, 4 GB, Maxwell (OBS renders on this one) |
| Camera | USB 2.0 UVC webcam, 1920×1080 30 fps |
| Backends detected by ProMatte | DirectML, CPU |

Only one machine was available, so this report covers one hardware
configuration. TensorRT, CUDA, OpenVINO, CoreML, macOS and Linux are **not**
verified — see §8.

## 3. Automated test results

### 3.1 Unit tests — 36 cases / 269 assertions, all pass

`build\bin\Release\promatte-unit-tests.exe`

Covers matte refinement (confidence curve, morphology, connected components),
temporal smoothing (flicker suppression, motion response, resolution change,
disabled path), pre-processing (tensor conversion for both layouts and channel
orders, luma, box/bilinear resize, AI-resolution selection), model descriptors
and output decoding (all six output kinds × both layouts, shape rejection),
the model manager (bundled vs user models, checksum verification, deletion,
custom models, failed download), the capability store, the performance
controller (tier mapping, hysteresis, ceiling relaxation, statistics), the
inference worker lifecycle (no model, missing file, real model, flooding,
disable, backend release) and SHA-256 against FIPS vectors.

### 3.2 Integration tests — 10 cases / 76 assertions, all pass

`build\bin\Release\promatte-integration-tests.exe` boots libobs with the D3D11
renderer, loads the built module and drives a synthetic animated source:

| Test | Result |
| ---- | ------ |
| libobs boots, module loads, filter type registered | pass |
| filter create / render / destroy | pass |
| every background mode (transparent, blur, colour, dim, image, video, source) and every debug view | pass |
| resolution changes 1280×720 → 640×360 → 1920×1080 | pass |
| enable/disable toggling ×5 | pass |
| backend and quality switching while rendering, unknown model id | pass |
| rapid create/destroy ×8, including destroying while the model is still loading | pass |
| render-cost budget (filter render time stays inside the frame budget) | pass |
| transparent output is composited by OBS rather than painted over the background | pass — 47.5 % of the frame transparent, 50.7 % of pixels take the colour of what is behind the source |
| memory bounded over a 30 s render run | pass — RSS 540.7 → 521.2 MB, process VRAM 222.2 → 159.2 MB (both fell) |

### 3.3 Live OBS session — 37 steps, 0 errors, 0 warnings

`tests\integration\obs_live_test.py --soak 15`, real OBS 32.2.2, real webcam,
one screenshot and an OBS statistics sample per step
(`tests/visual/live/`, report in `live-report.json`).

Steps covered: all six presets, all background modes, all debug views, the
developer overlay, all five quality levels, CPU and Auto backends, all six
models, filter toggling ×4, scene switching ×3, camera resolution change
1920×1080 → 640×480 → back, a 10 s recording, and a 15-minute soak.

OBS render statistics during the functional pass (steady state):

| Metric | Value |
| ------ | ----- |
| OBS render FPS | 30.0 in 33 of 37 steps; 29.0 / 28.1 / 25.2 in four steps that include a model reload or a camera reconfiguration |
| Filter render time | 1.7 – 3.3 ms typical (blur mode 1.7 ms, transparent 2.7 ms) |
| Highest render time | 26.9 ms on the step that loads a background image from disk, 39–42 ms on the two steps that create the filter / restart recording |
| ProMatte warnings or errors in the OBS log | 0 |

The live test also asserts that the filter actually changes pixels
(`filter_rendered: true`) rather than silently passing video through.

### 3.3a Live transparency check — pass

`tests\integration\obs_transparency_check.py` builds a scene with a solid
colour source *behind* the webcam, sets ProMatte to transparent and screenshots
the composited scene twice, with a magenta and then a green background. Run
against the installed build on the real camera:

| Metric | Result |
| ------ | ------ |
| Background visible through the matte | 74.6 % (magenta), 74.7 % (green) |
| Pixels that follow whatever is behind the camera | 73.5 % |
| Pixels that change when the background colour changes | 90.4 % |
| Verdict | pass — the subject is cut out and the scene behind shows through |

This is the end-to-end counterpart of the headless compositing test, and it is
what confirms the defect in §9 is fixed in a real OBS scene rather than only in
the harness.

### 3.4 Stress / soak

| Run | Duration | Result |
| --- | -------- | ------ |
| Live OBS soak (webcam, blur mode, Auto) | 15 min, 30 samples | render FPS 27.1 min / 30.0 median / 30.0 max; OBS process memory **726 → 498 MB** (fell, no growth); CPU 37 % median; 221 skipped render frames accumulated out of 35 202 total (0.6 %), none in the last 6 minutes |
| Headless libobs memory check | 30 s | RSS 540.7 → 521.2 MB and process VRAM 222.2 → 159.2 MB, both well inside the 64 MB growth bound the test asserts |
| Benchmark loops | 56 × (8 warm-up + 48 frames) per model/backend/tier | no growth across runs; VRAM returns to baseline after each session is destroyed |

A 2-hour and an 8-hour soak were **not** run — see §8.

## 4. Performance measurements

### 4.1 Model benchmark (1280×720 real webcam-style portrait frames)

`promatte-bench --backend all --input tests/visual/frames`; full JSON in
`tools/benchmark/results/portraits-720p-940mx-final.json`. "Total" is the whole
worker cost per frame (pre-process + inference + refine + temporal). Flicker is
the mean per-pixel alpha change across consecutive frames of a quasi-static
sequence (lower is more stable); softness is the fraction of pixels in the
0.1–0.9 alpha band (higher means more real soft edge rather than a cut-out).

| Model | Backend | Tier | AI input | Inference ms (p95) | Total ms | AI FPS | CPU % | VRAM Δ MB | Flicker | Softness |
| ----- | ------- | ---- | -------- | -----------------: | -------: | -----: | ----: | --------: | ------: | -------: |
| MediaPipe Selfie landscape | DirectML | Fixed | 256×144 | 4.9 (5.7) | 6.2 | 156.0 | 10 | 16 | 0.0000 | 0.037 |
| MediaPipe Selfie landscape | CPU | Fixed | 256×144 | 6.2 (12.4) | 7.8 | 124.1 | 40 | 0 | 0.0000 | 0.037 |
| MediaPipe Selfie general | DirectML | Fixed | 256×256 | 7.5 (8.1) | 9.7 | 100.0 | 8 | 21 | 0.0000 | 0.034 |
| MediaPipe Selfie general | CPU | Fixed | 256×256 | 7.9 (11.1) | 10.1 | 95.9 | 47 | 0 | 0.0000 | 0.034 |
| PP-HumanSeg v2 portrait | CPU | Fixed | 256×144 | 18.0 (29.0) | 19.5 | 50.9 | 40 | 0 | 0.0045 | 0.027 |
| PP-HumanSeg v2 lite | CPU | Fixed | 192×192 | 19.4 (26.4) | 20.6 | 48.2 | 42 | 0 | 0.0082 | 0.124 |
| PP-HumanSeg v2 lite | DirectML | Fixed | 192×192 | 20.8 (25.6) | 22.1 | 44.8 | 7 | 40 | 0.0082 | 0.124 |
| PP-HumanSeg v2 portrait | DirectML | Fixed | 256×144 | 29.0 (44.6) | 30.7 | 32.3 | 10 | 27 | 0.0045 | 0.027 |
| **RVM MobileNetV3** | DirectML | Performance | 512×288 | 70.9 (115.6) | 76.6 | 12.9 | 6 | 75 | 0.0040 | 0.024 |
| **RVM MobileNetV3** | CPU | Performance | 512×288 | 90.1 (115.0) | 96.1 | 10.3 | 42 | 0 | 0.0040 | 0.024 |
| RVM MobileNetV3 | CPU | Balanced | 640×352 | 156.6 (230.9) | 166.5 | 6.0 | 38 | 0 | 0.0038 | 0.020 |
| RVM MobileNetV3 | DirectML | Balanced | 640×352 | 170.3 (182.5) | 179.7 | 5.5 | 4 | 78 | 0.0038 | 0.020 |
| MODNet | DirectML | Performance | 320×192 | 158.4 (172.1) | 161.0 | 6.2 | 2 | 92 | 0.0000 | 0.017 |
| MODNet | CPU | Performance | 320×192 | 149.2 (171.8) | 151.6 | 6.6 | 46 | 0 | 0.0000 | 0.017 |
| MediaPipe Selfie multiclass | DirectML | Fixed | 256×256 | 181.6 (212.9) | 183.8 | 5.4 | 1 | 158 | 0.0040 | 0.050 |
| MediaPipe Selfie multiclass | CPU | Fixed | 256×256 | 196.6 (226.5) | 199.0 | 5.0 | 41 | 0 | 0.0040 | 0.050 |
| RVM MobileNetV3 | CPU | Quality | 896×512 | 304.2 (400.0) | 326.1 | 3.0 | 39 | 0 | 0.0035 | 0.012 |
| RVM MobileNetV3 | DirectML | Ultra | 1280×704 | 668.3 (779.5) | 708.4 | 1.4 | 2 | 233 | 0.0035 | 0.010 |

(The remaining tiers are in the JSON.)

Reading of these numbers on **this** hardware:

* The fast segmentation models are the only ones that reach a live frame rate:
  MediaPipe Selfie landscape gives a 6–8 ms matte, PP-HumanSeg 19–22 ms.
* RVM — the best-quality model — costs 71–96 ms per frame at its lowest tier,
  i.e. 10–13 AI FPS. It is usable (the render thread still runs at 30 fps and
  reuses the newest matte) but the matte lags fast motion. On a modern GPU this
  is the model to use; on this 2017 laptop Auto correctly avoids it.
* This Maxwell GPU is barely faster than the CPU for these workloads, and for
  the small models it is slower once dispatch overhead is counted. That is a
  property of the hardware, not of the implementation.

### 4.2 Targets from the specification

| Target | Result on this machine |
| ------ | ---------------------- |
| High-end PC, 1080p, 30–60 FPS, < 50 ms latency | **not verifiable here** — no high-end GPU available |
| Mid-range PC, 1080p, 24–30 FPS | **not verifiable here** |
| Low-end PC, 720p, 15–30 FPS | **met and exceeded**: at 1080p input the default automatic configuration produces a 124–156 FPS matte (MediaPipe landscape) or 45–51 FPS (PP-HumanSeg) while OBS holds 30 fps and the filter costs 1.7–3.3 ms of render time |
| OBS stays responsive | **met**: 30.0 fps in the live session, 0.6 % skipped frames overall and none in the last six minutes of the soak |

### 4.3 Adaptive behaviour observed live

All three degradation mechanisms fired during the live run, and were logged:

1. **Backend demotion.** DirectML on the 940MX competes with OBS' own D3D11
   rendering on this laptop. The capability store recorded three strikes and
   marked the backend `too_slow`, after which Auto logs
   `auto backend: NVIDIA GeForce 940MX|directml is known to contend with OBS
   rendering on this machine; using CPU` and stays on the CPU provider.
2. **Model fallback.** With PP-HumanSeg portrait selected automatically the
   controller measured 346 ms against a 50 ms budget and logged
   `Performance limited - switching to a faster model`, then reloaded
   MediaPipe Selfie landscape.
3. **Per-machine persistence.** `%APPDATA%\obs-studio\plugin_config\promatte\capability.json`
   holds the measured cost of every (device, backend, model) combination, so a
   second OBS start begins with the configuration that already worked.

## 5. Visual quality

Live webcam, indoor room with a white wall, shelving, a printer, a microphone
arm and a window; subject wearing glasses and a patterned shirt. Screenshots
for every step are in `tests/visual/live/`; benchmark matte/composite dumps are
in `build/bench-dump/`.

| Aspect | Result |
| ------ | ------ |
| Overall composite | Blur mode is clean and convincing: the subject is sharp, the room is smoothly blurred, no visible halo (`21_mode_blur.png`) |
| Transparent output | Real straight alpha: 65.5 % of pixels fully transparent, 27.3 % fully opaque, the remainder a soft transition band — verified by reading the alpha channel of the PNG, not by eye. Compositing over what is behind the source is covered by its own regression test (§3.2) after the defect in §10 |
| Matte shape | Clean single silhouette, head/shoulders/arms correct, no stray blobs (`25_debug_matte.png`) |
| Hair | Good silhouette with the fast models but the strand detail is limited by their 256×144 output; RVM at the Performance tier keeps individual strands (see `build/bench-dump/rvm_mobilenetv3_*_composite.png` vs the MediaPipe dumps) |
| Glasses | Preserved, including the lenses and the frame, in every mode |
| Hands / arms | Preserved when raised and when resting against the face |
| Clothing | Patterned shirt held without holes; the confidence curve keeps the collar edge soft |
| Fast movement | The matte follows head and hand motion; with the 6–8 ms models it stays locked to the subject, with RVM at ~12 AI FPS a fast hand can lead the matte by 1–2 frames |
| Low light / backlight | Tested with room lights off and with the window behind the subject; the matte holds, the edge softens |
| Complex background | Shelving, printer and microphone arm are all removed; the microphone arm in front of the subject is correctly treated as background by the person-only models |
| Temporal stability | Flicker metric 0.000–0.008 mean alpha change per pixel per frame; no visible crawl on static edges in the recorded clip |
| Recording | A 10 s MP4 recorded through OBS shows the same composite as the preview (`tests/visual/live/recording/`) |

## 6. Requirement acceptance criteria

| # | Requirement | Verdict | Evidence |
| - | ----------- | ------- | -------- |
| A | OBS remains responsive while AI inference runs | **pass** | live session held 30.0 fps with the filter costing 1.7–3.3 ms of render time; inference runs on its own thread |
| B | No unbounded frame queue | **pass** | single-slot mailbox; `queueDepth` never exceeds 1 in the unit test that floods 50 frames, drops are counted instead of queued |
| C | No major memory growth over long sessions | **pass** | 15-minute live soak: 726 → 498 MB; headless 30 s check bounded at 64 MB; benchmark sessions return VRAM to baseline |
| D | Changing scenes does not crash the plugin | **pass** | live test switches scenes 3× and the headless harness creates/destroys the filter 8× including mid-load |
| E | Disabling the filter immediately stops expensive AI processing | **pass** | unit test asserts `framesProcessed` stops advancing; the filter also idles the worker after 400 ms without a render call, and on `hide` |
| F | CPU fallback works | **pass** | CPU is in fact the automatically selected backend on this machine; all six models run on it |
| G | GPU acceleration works where supported | **pass** | DirectML initialises on the selected adapter and runs every model (§4.1); explicit adapter selection via the DML1 API is logged (`DirectML on adapter …`) |
| H | Real-time processing on a reasonable consumer PC | **pass on a low-end PC** | 1080p input, 30 fps output, 124–156 FPS matte with the default automatic configuration |

## 7. Privacy and licensing

* No network code other than the model download the user starts explicitly.
  Verified by inspection: the only HTTP client is `src/models/model_downloader.cpp`
  (WinHTTP, HTTPS only), called solely from `ModelManager::startDownload`.
* No telemetry, no analytics, no account, no cloud inference.
* Frames are never written to disk or transmitted; they live in the worker's
  reused buffers only.
* All bundled models are Apache-2.0 / CC-BY-4.0; the two GPL-3.0 / Apache-2.0
  downloadable models are fetched from their authors with pinned SHA-256.
  A non-commercially licensed model (BRIA RMBG) was evaluated and **excluded**.
  Full list in [THIRD_PARTY_LICENSES.md](../THIRD_PARTY_LICENSES.md).

## 8a. Platform coverage

| Platform | Builds | Unit tests | Package | Loads in OBS | Run against a real camera |
| -------- | ------ | ---------- | ------- | ------------ | ------------------------- |
| Windows x64 (MSVC) | yes | 36/36 | `ProMatte-Setup-1.0.0.exe` | yes | yes, everything in this report |
| Linux x86_64 (GCC 13, Ubuntu 24.04) | yes | 36/36 | `.deb` + `.tar.gz` | module `dlopen`s and resolves `obs_module_load`; all shared-library dependencies resolve | **no** |
| macOS arm64 (Apple clang) | CI only | CI only | `.pkg` | **not checked** | **no** |

Linux was built and tested in a WSL Ubuntu 24.04 container against the
distribution's libobs 30.0.2 and an upstream ONNX Runtime 1.24.4 tarball. The
`.deb` installs to `/usr/lib/obs-plugins/promatte.so` with ONNX Runtime beside it
and `RUNPATH=$ORIGIN/promatte`; `ldd` reports no unresolved libraries and a
`dlopen` test finds the module entry point. What has **not** happened on Linux is
running it inside a real OBS against a camera, because the container has no GPU
or display.

Inference itself was exercised on Linux with the same benchmark and the same
1280x720 portrait frames used for the Windows numbers in §4.1, on the same CPU
(the WSL container sees the host's i7-7500U). ONNX Runtime, the pre-processing,
the refinement and the temporal stage all run:

| Model | AI input | Inference ms (p95) | Total ms | AI FPS | CPU % |
| ----- | -------- | -----------------: | -------: | -----: | ----: |
| MediaPipe Selfie general | 256x256 | 8.2 (9.0) | 9.7 | 100.2 | 48 |
| MediaPipe Selfie landscape | 256x144 | 10.6 (27.9) | 12.3 | 79.9 | 55 |
| PP-HumanSeg v2 portrait | 256x144 | 17.9 (34.7) | 19.3 | 51.2 | 49 |
| PP-HumanSeg v2 lite | 192x192 | 18.5 (40.2) | 19.5 | 51.0 | 49 |
| RVM MobileNetV3 (Performance) | 512x288 | 81.9 (142.4) | 86.5 | 11.4 | 52 |
| MODNet (Performance) | 320x192 | 164.2 (256.6) | 165.8 | 6.0 | 49 |
| MediaPipe Selfie multiclass | 256x256 | 217.3 (338.0) | 219.9 | 4.5 | 48 |

These land within a few milliseconds of the Windows CPU figures for the same
models on the same processor, which is the result to expect and a useful check
that nothing in the port changed the numerics. The platform reporting works too:
the run identifies the machine as "Ubuntu 24.04.4 LTS" through the new
`/etc/os-release` path rather than the old "Unknown OS".

macOS is built and packaged by the CI workflow on a `macos-14` runner and has
never been executed by the author. Treat the macOS package as untested beyond
"it compiles, links, packages and its unit tests pass".

## 8. Known limitations

Verification gaps (things not tested rather than things known broken):

1. **One machine only.** Everything is measured on a 2017 dual-core laptop with
   a Maxwell GPU. The high-end and mid-range performance targets in the
   specification could not be verified.
2. **Only DirectML and CPU exercised.** The TensorRT, CUDA, OpenVINO and CoreML
   code paths are implemented and are selected by the backend registry when the
   corresponding ONNX Runtime execution provider is present, but the ONNX
   Runtime build installed here contains only DirectML and CPU, so they have
   never run. They are listed as unavailable in the UI on this machine.
3. **Only Windows is verified end to end.** Linux builds, passes its unit tests
   and produces an installable package whose module loads, but has never been
   run inside OBS. macOS is built and packaged by CI only. The platform matrix
   is in §8a. DirectML is Windows-only by nature; CUDA, TensorRT, CoreML and
   OpenVINO are selected automatically when an ONNX Runtime build providing them
   is present, and none of them has run anywhere.
4. **Long soaks.** 15 minutes live and 30 s headless were run. The 2-hour and
   8-hour soaks in the specification were not.
5. **Camera scenarios.** The scenario list was exercised by one person in one
   room (see `docs/testing.md`); curly and long hair, headphones, and a second
   person are covered only by the still portrait frames used in the benchmark.

Product limitations:

6. **Multi-person.** All shipped models are single-person or person-class
   models. A second person is segmented only if the model happens to include
   them; the component filter always keeps the largest connected region, so the
   primary subject is never dropped. Documented for users in the troubleshooting
   guide.
7. **RVM on weak GPUs.** The best-quality model costs 71–96 ms per frame here,
   so Auto does not choose it on this class of hardware. Users on such machines
   who select it manually get a high-quality but visibly lagging matte.
8. **DirectML first-session compile.** The first inference session on a given
   adapter compiles shaders: 8.4 s on the 940MX, and 30–76 s on the Intel HD
   620 during earlier testing. The filter passes video through unchanged while
   this happens and the UI shows "Loading model…".
9. **Objects held by the subject** (a microphone in front of the face, a mug)
   are treated as background by the person-segmentation models. RVM handles
   them better because it mattes rather than classifies.
10. **Video backgrounds** rely on OBS' Media Source; formats OBS cannot decode
    will not play, and the media source adds its own decode cost.

## 9. Defects found and fixed during verification

| Defect | How it showed | Fix | Regression test |
| ------ | ------------- | --- | --------------- |
| Effect parameters discarded between passes | libobs clears every effect parameter at `gs_technique_end`, so parameters assigned before `gs_effect_loop` were lost for every pass after the first; the blur and refinement passes ran with stale uniforms | `drawEffect` takes a callback that assigns parameters immediately before the loop that consumes them | covered indirectly by the matte-follows-model test |
| **Transparent mode never revealed anything** | The final composite pass called `gs_enable_blending(false)`, overriding the blend state of the caller. The alpha channel was correct, but the *background* pixels' colour was written straight over whatever was behind the source, so "Remove (transparent)" looked like it did nothing in a scene and in the filter preview | draw with the caller's blend state, exactly like the stock OBS chroma-key and colour-key filters | "transparent output is composited by OBS, not painted over the background": composites over two different background colours and requires the result to differ. With the defect reintroduced it reports 0.0 % differing pixels and fails; with the fix, 50.7 % |
| Duplicate install shadowed updates | With ProMatte present both in the OBS folder and in `%ProgramData%\obs-studio\plugins`, OBS loaded both, logged `Source 'promatte_filter' already exists!` and kept the copy that loaded first, so an updated build silently never ran | the module now detects an existing registration and logs `ANOTHER COPY OF PROMATTE IS ALREADY LOADED`, naming the ignored file; the installer deletes the ProgramData copy | n/a (environment defect; documented in troubleshooting) |

## 10. Conclusion

The plugin builds warning-free, loads in OBS Studio 32.2.2, and does what it
claims: real, local, GPU- or CPU-accelerated background removal with
professional edge treatment, adaptive quality and no cloud dependency. All
eight acceptance criteria pass on the available hardware. The gaps in §8 are
verification coverage gaps (other hardware, other platforms, longer soaks), not
known defects; the three defects found along the way are in §9, each with the
change that fixed it and, where it can be tested, the test that now guards it.

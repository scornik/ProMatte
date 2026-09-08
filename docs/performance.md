# ProMatte — Performance

## Design goals

| Goal | Mechanism |
| ---- | --------- |
| OBS never waits for AI | dedicated worker thread, single-slot latest-frame-wins mailbox, render thread only swaps buffers |
| No unbounded queue | mailbox depth is 0 or 1; an unread frame is overwritten and counted as dropped |
| Minimal GPU↔CPU traffic | frame is downscaled on the GPU to AI resolution before readback; matte returns as a small RGBA8 texture |
| Readback never stalls the compositor | staging ring, and the map (a GPU sync point) only happens when the worker is ready for a frame |
| One pass per frame | OBS renders a filter once per output and again per preview/projector; the AI feed and the refinement passes run on the first render of a frame only and are reused by the others |
| Cheap full-resolution edge work | joint bilateral upsampling with one fetch per neighbour (colour + matte packed in one texture), 8-bit blur pyramid at 1/4 resolution |
| Graceful degradation | tier ladder → cheaper model → CPU backend, all learned and persisted per machine |

## Adaptive quality controller

Inputs, all measured continuously:

* worker time per frame (pre-process + inference + refinement + temporal), EMA
* render-thread time of the filter, EMA (detects GPU contention)
* submitted / dropped frame counters, AI FPS

Budget: `1000 / clamp(source fps, 15, 30)` ms (≤ 20 fps target on the CPU backend).

Decisions (Auto mode only):

| Condition | Action |
| --------- | ------ |
| worker cost > 3× budget after 3 frames | immediate step down one tier (fast path), or model fallback at the lowest tier |
| worker cost > 1.15× budget for 2 s | step down one tier |
| render time > 25 % of budget for 2 s with a GPU backend | treated as over budget (step down) |
| lowest tier still > 1.6× budget for 4 s | switch to the cheapest recommended model |
| render pressure persists 6 s at the lowest tier | move inference to the CPU backend; remembered |
| worker cost < 0.55× budget for 6 s | step up one tier (bounded by a ceiling that relaxes 90 s after the last downgrade) |
| top tier at < 0.4× budget for 9 s | try the next better installed model |

Manual quality modes (Performance / Balanced / Quality / Ultra) pin a tier and never adapt.

### Capability store

`%APPDATA%\obs-studio\plugin_config\promatte\capability.json` records, per
`<device>|<backend>`, the measured cost of every model at its lowest tier and
whether it was too slow (3 consecutive measurements > 2× budget; a measurement
inside budget rehabilitates it). Auto uses it to

* start with the best model already known to fit the budget on this device,
  otherwise the cheapest recommended model;
* never auto-select a model recorded as too slow;
* skip a GPU backend recorded as contending with OBS rendering (`__backend__`).

Delete the file to make Auto re-learn from scratch.

## Measured numbers — test laptop

Intel Core i7-7500U (2C/4T), NVIDIA GeForce 940MX (Maxwell, 4 GB), Intel HD 620,
Windows 11 Pro N, OBS 32.2.2, camera 1920×1080 @ 30 fps. This is an entry-level
machine; it was chosen deliberately as the worst case.

### Inference (tools/benchmark, real portrait frames)

| Model | Backend / device | AI input | Inference ms | Worker total ms | AI FPS |
| ----- | ---------------- | -------- | -----------: | --------------: | -----: |
| MediaPipe Selfie landscape | DirectML / 940MX | 256×144 | 4.9 | 6.2 | 156 |
| MediaPipe Selfie landscape | CPU (2 threads) | 256×144 | 6.2 | 7.8 | 124 |
| PP-HumanSeg v2 lite | DirectML / 940MX | 192×192 | 20.8 | 22.1 | 45 |
| PP-HumanSeg v2 lite | CPU | 192×192 | 19.4 | 20.6 | 48 |
| RVM MobileNetV3, Performance | DirectML / 940MX | 512×288 (ratio 0.5) | 70.9 | 76.6 | 13 |
| RVM MobileNetV3, Performance | CPU | 512×288 | 90.1 | 96.1 | 10 |
| RVM MobileNetV3, Balanced | DirectML / 940MX | 640×352 | 170 | 180 | 5.5 |
| MODNet, Performance | DirectML / 940MX | 320×192 | 158 | 161 | 6 |
| Selfie Multiclass | DirectML / 940MX | 256×256 | 182 | 184 | 5 |

The 940MX is slower than a modern iGPU: the Intel HD 620 ran RVM Performance in
49 ms and MODNet in 105 ms, but needs 7–100 s to compile the first DirectML
session, which is why the render adapter is preferred by default.

### Filter render cost (headless libobs harness, 20th percentile, `tests/integration`)

| Configuration | Render ms |
| ------------- | --------: |
| baseline, filter disabled, 1080p | 0.5 |
| 1080p transparent, no AI running | 1.7 |
| 1080p transparent, CPU inference | 4.0 |
| 1080p transparent, GPU inference (same GPU) | 1.3 |
| 1080p blur, GPU inference | 2.5 |
| 1080p fast (3×3) upsample | 1.8 |
| 720p transparent, GPU inference | 1.8 |

Before the readback gating and per-frame reuse the same table read 8–53 ms;
before the 8-bit quarter-resolution blur pyramid the blur case was 29 ms.

### Live OBS (obs-websocket driven, `tests/integration/obs_live_test.py`)

With a 1080p camera and the preview open, OBS reports (average frame render
time / render FPS):

| Backend | Model (Auto) | Render ms | OBS FPS |
| ------- | ------------ | --------: | ------: |
| DirectML on the 940MX (same GPU as OBS) | Selfie landscape | 30–38 | 26–29 |
| CPU (2 threads) | Selfie landscape / PP-HumanSeg | 9.7 | 29–30 |
| DirectML, camera at 640×480 | Selfie landscape | 5.0 | 30 |

On this laptop DirectML inference and OBS rendering share one weak GPU whose
WDDM scheduler time-slices them coarsely; the controller's backend fallback
therefore moves inference to the CPU after a few seconds and remembers that
choice. Desktop GPUs with hardware preemption do not show this behaviour.

## Memory

Integration harness, 1080p blur mode, 30 s: RSS 161 → 164 MB, process VRAM 40 →
42 MB. Live OBS soak (6 min): stable within ±5 MB after warm-up. All per-frame
buffers (tensors, staging surfaces, matte textures, luma/alpha planes) are
allocated once per resolution change and reused.

## What to expect on other hardware (estimates from the model FLOP counts)

| Machine | Expected Auto result at 1080p |
| ------- | ----------------------------- |
| RTX 3060 / RX 6600 or better | RVM Balanced–Quality at 30 AI FPS, render < 2 ms |
| GTX 1650 / Intel Arc / Apple M1-class iGPU (via future CoreML) | RVM Performance–Balanced at 25–30 AI FPS |
| Intel Iris Xe / AMD 680M | RVM Performance or PP-HumanSeg |
| 2-core laptop, CPU only | PP-HumanSeg lite or MediaPipe Selfie at 20–30 AI FPS |

Run `promatte-bench --backend all --input <frames>` to measure your own machine.

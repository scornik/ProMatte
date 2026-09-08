# ProMatte — Architecture

## 1. Layers

```
OBS Studio (graphics thread)                 ProMatte inference worker thread
─────────────────────────────                ────────────────────────────────
obs_source_info callbacks (src/obs/filter.cpp)
  video_render
    ├─ capture source → srcRender (full res)          ┌────────────────────────┐
    ├─ GPU downscale → aiRender (AI res)              │ FrameBuffer mailbox    │
    ├─ stage[N] ← aiRender ; map stage[N-1] ──submit─►│ (single slot, latest   │
    │                                                 │  frame wins)           │
    │                                                 └───────────┬────────────┘
    │                                                             ▼
    │                                                 preprocess (BGRA→tensor, luma)
    │                                                 SegmentationBackend::processFrame
    │                                                   (ONNX Runtime: DirectML/CUDA/CPU)
    │                                                 decode output → alpha [0,1]
    │                                                 MatteRefiner (CPU, AI res)
    │                                                 TemporalStabilizer (motion adaptive)
    │                                                 PerformanceController.record*
    │                                                 ┌────────────────────────┐
    ├─ takeMatte() ◄──────────────────────────────────│ MatteBuffer mailbox    │
    ├─ uploadMatte → matteTex (RGBA8, AI res)         └────────────────────────┘
    ├─ GpuPipeline::render
    │    refine: JBU upsample (3x3/5x5) → feather (separable Gaussian)
    │    background estimate: person-excluded dual-Kawase blur (½ res) → normalise
    │    composite: decontaminate, halo removal, mode (transparent/colour/image/video/source/blur/dim)
    └─ developer overlay (optional)
```

Source directories map to the layers of the specification:

| Layer | Directory | Key types |
| ----- | --------- | --------- |
| OBS filter | `src/obs/` | `FilterContext`, `FilterSettings`, presets, `BackgroundSource` |
| Frame acquisition / conversion | `src/rendering/gpu_pipeline.cpp` (capture, downscale, staging ring) | `GpuPipeline` |
| Pre-processing | `src/preprocessing/` | `bgraToTensor`, `bgraToLuma`, `resizeBgra`, `chooseAiResolution` |
| AI segmentation / matting | `src/inference/`, `src/backends/` | `SegmentationBackend`, `OnnxRuntimeBackend`, `ModelDescriptor`, `InferenceWorker` |
| Temporal stabilisation | `src/temporal/` | `TemporalStabilizer` |
| Matte refinement | `src/postprocessing/` (CPU) + `data/effects/promatte_refine.effect` (GPU) | `MatteRefiner` |
| Edge processing / composition | `data/effects/promatte_blur.effect`, `promatte_composite.effect` | `GpuPipeline::render` |
| Performance controller | `src/performance/` | `PerformanceController`, `PerfStats` |
| Model management | `src/models/` | `ModelManager`, `downloadFile` |
| UI | `src/ui/properties.cpp` | OBS properties panel |
| Utilities | `src/utils/` | logging, SHA-256, file system, timers, system info |

`promatte-core` (static library) contains everything that does not depend on
libobs; it is reused by the benchmark and the unit tests. `promatte` (the OBS
module) adds the OBS-facing code and the GPU pipeline.

## 2. Threading model

* **Graphics thread** (OBS): `video_render` does only GPU work plus two
  constant-time mailbox operations. It never waits for inference. Cost per
  frame at 1080p on the test GPU: 0.3–1.5 ms (measured in `PerfStats::renderMs`).
* **Inference worker** (one `std::thread` per filter instance): owns the
  backend/session, tensors, refiner and stabiliser. Configuration changes,
  model loads, resolution changes and adaptive tier changes are all applied
  here; the graphics thread only posts requests.
* **Mailboxes**: `FrameBuffer` and `MatteBuffer` slots are protected by a mutex
  held only for a `std::swap` (no copies, no allocations after warm-up). If a
  frame arrives before the previous one was consumed it is overwritten and
  counted as a drop (`PerfStats::framesDropped`). Queue depth is therefore 0 or 1.
* **Shutdown**: `filter_destroy` calls `InferenceWorker::stop()` which sets the
  stop flag, calls `SegmentationBackend::cancel()` (`OrtRunOptions::SetTerminate`)
  so an in-flight run aborts, and joins the thread before any GPU resource is
  freed. No thread outlives the filter.
* **Enable/disable, hide/show**: `video_tick` watches the time since the last
  render call (OBS stops calling `video_render` for disabled filters); after
  400 ms without a render the worker is paused and drops frames without doing
  any AI work. `hide`/`show` do the same immediately.
* **Model download** runs on its own thread inside `ModelManager` with a
  cancellation flag and is joined on destruction.

## 3. GPU pipeline details

1. **Capture.** `obs_source_process_filter_begin/end` draws the upstream source
   into `srcRender` (RGBA, full resolution) with blending disabled.
2. **Downscale.** `promatte_downscale.effect` renders `srcRender` into
   `aiRender` (BGRA, AI resolution) with four bilinear taps arranged as a box
   filter. The AI resolution is chosen by the worker
   (`chooseAiResolution`): tier pixel budget × source aspect ratio, aligned to
   the model's alignment (32 for RVM/MODNet), never larger than the source.
3. **Staging ring.** Two `gs_stagesurf_t`; frame N stages surface N%2 and maps
   surface (N-1)%2 which the GPU finished a frame ago, so `map` does not stall.
   The mapped BGRA rows are copied into the worker's `FrameBuffer` (0.6 MB at
   512×288).
4. **Matte upload.** The worker publishes RGBA8 at AI resolution: `a` = alpha,
   `rgb` = predicted foreground colour when the model provides one (RVM `fgr`).
   `gs_texture_set_image` on a `GS_DYNAMIC` texture.
5. **Refine** (`promatte_refine.effect`): joint bilateral upsampling — for each
   output pixel the 2×2 (fast) or 4×4 (high) low-resolution matte neighbourhood
   is weighted by spatial distance and by colour similarity between the
   full-resolution pixel and the low-resolution colour (`aiRender`), which
   snaps the alpha edge to the true image edge. Optional separable Gaussian
   feather (radius from the *Feather* slider, up to ~1.2 % of frame height).
6. **Background estimate** (`promatte_blur.effect`): the source is
   pre-multiplied by (1 − alpha) at half resolution, blurred with a dual-Kawase
   pyramid and re-normalised by the accumulated weight. Result: a blurred
   background with the subject inpainted away — used directly for *Blur* mode
   and as the background colour estimate for decontamination/halo removal.
7. **Composite** (`promatte_composite.effect`):
   * decontamination: with RVM, blend towards the model's foreground colour in
     the transition band; otherwise un-mix with the matting equation
     `fg = (src − (1−a)·bg) / a`;
   * halo removal: semi-transparent pixels whose colour matches the background
     estimate get their alpha reduced;
   * modes: transparent (straight alpha), solid colour with opacity, image /
     video / OBS source with fill/fit/stretch mapping, blur, dim;
   * debug views: matte, edges, confidence heat map, foreground only.

All intermediate passes run with sRGB framebuffer conversion disabled so the
source colours pass through unchanged.

## 4. CPU-side refinement (worker, AI resolution)

`MatteRefiner::process` — component filter (drop foreground blobs below 0.4 %
of the frame, fill enclosed holes below 0.2 %, always keeps the largest
component), morphological erode/dilate (edge shift), confidence curve
(smoothstep around the *separation* threshold with a *smoothness* band, keeps
hair semi-transparency instead of a binary cut).

`TemporalStabilizer::process` — per-pixel blend factor from a blurred luma
difference map: static regions converge slowly (stability slider), moving
regions follow immediately (motion response), large disagreements pass through,
skipped frames are compensated with `1 − (1−k)^frames`. Recurrent models
(RVM) get half the smoothing strength because they are already stable.

## 5. Adaptive quality

`PerformanceController` measures the worker's total per-frame time and drives
a per-model tier ladder (see `data/models/manifest.json`):

| Model | Tiers |
| ----- | ----- |
| RVM | 512×288 · 640×352 · 896×512 · 1280×704 (all with downsample ratio 0.5, i.e. the encoder runs at half of that) |
| MODNet | 320×192 · 448×256 · 512×288 · 640×384 |
| fixed-shape models | one tier |

Budget = 1000 / clamp(source fps, 15, 30) ms (20 fps target on CPU). Over
budget by 15 % for 2 s → step down; under 55 % for 6 s → step up, bounded by a
ceiling that only relaxes 90 s after the last downgrade. If the lowest tier is
still 60 % over budget for 4 s and the model was chosen automatically, the
worker switches to the cheapest recommended model (PP-HumanSeg lite). Manual
quality modes map to fixed tiers and never adapt.

## 6. Settings persistence

All settings are OBS filter settings (`obs_data_t`), stored by OBS in the scene
collection JSON, so they survive restarts and plugin updates. Defaults are
declared in `settingsDefaults()`. Downloaded models live in
`%APPDATA%\obs-studio\plugin_config\promatte\models\` and are never touched
by the installer.

## 7. Error handling and fail-safe

| Failure | Behaviour |
| ------- | --------- |
| effects fail to compile / data dir missing | filter passes the source through (`obs_source_skip_video_filter`), error in the status text and log |
| no model installed | pass-through, status explains how to download one |
| backend init fails | fallback chain TensorRT → CUDA → DirectML → CPU, then error state with pass-through |
| inference exception | logged, state = Error, last matte kept; pass-through if there is none |
| device-resident recurrent state unsupported | automatic switch to CPU round-trip |
| frame at unexpected size | resized on the CPU instead of rejected |
| model too slow | tier ladder, then model fallback (Auto) |
| OBS destroys the filter mid-inference | run is terminated, thread joined, then GPU resources released |

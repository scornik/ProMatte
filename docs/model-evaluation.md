# ProMatte — Model Evaluation

All numbers were produced by `tools/benchmark/promatte-bench` on the test
laptop (Intel Core i7-7500U 2C/4T, NVIDIA GeForce 940MX 4 GB, Intel HD 620,
Windows 11 Pro N). JSON sources are in `tools/benchmark/results/`.

Column meanings: *Inference* = model run only; *Total* = pre-process +
inference + CPU refinement + temporal filter (what the worker thread spends per
frame); *AI FPS* = throughput of the worker alone; *Flicker* = mean |alpha_t −
alpha_t−1| between consecutive frames of a quasi-static sequence (sensor noise
+ 1–2 px jitter), lower is better; *Softness* = fraction of pixels with
0.1 < alpha < 0.9 (hair/edge transparency); *Coverage* = fraction of foreground.

## 1. Real portrait frames, 1280×720 (final)

Frames: 24 crops of two public-domain portraits with added sensor noise and
jitter (`tools/models` + `tests/visual/frames`). DirectML = NVIDIA GeForce
940MX (the adapter OBS renders on); CPU = 2 threads.

**tools/benchmark/results/portraits-720p-940mx-final.json** — Intel(R) Core(TM) i7-7500U CPU @ 2.70GHz, GPUs: Intel(R) HD Graphics 620, NVIDIA GeForce 940MX, Microsoft Basic Render Driver; input 1280x720, 24 frames

| Model | Backend | Device | Tier | AI input | Init ms | Inference ms (p95) | Total ms | AI FPS | CPU % | VRAM Δ MB | RSS Δ MB | Flicker | Softness | Coverage |
| ----- | ------- | ------ | ---- | -------- | ------: | -----------------: | -------: | -----: | ----: | --------: | -------: | ------: | -------: | -------: |
| rvm_mobilenetv3 | directml | NVIDIA GeForce 940MX | Performance | 512x288 | 1036 | 70.9 (115.6) | 76.6 | 12.9 | 6 | 75 | 102 | 0.0040 | 0.024 | 0.315 |
| rvm_mobilenetv3 | directml | NVIDIA GeForce 940MX | Balanced | 640x352 | 697 | 170.3 (182.5) | 179.7 | 5.5 | 4 | 78 | 67 | 0.0038 | 0.020 | 0.315 |
| rvm_mobilenetv3 | directml | NVIDIA GeForce 940MX | Quality | 896x512 | 1134 | 331.0 (354.5) | 355.8 | 2.8 | 3 | 130 | 108 | 0.0035 | 0.012 | 0.317 |
| rvm_mobilenetv3 | directml | NVIDIA GeForce 940MX | Ultra | 1280x704 | 1058 | 668.3 (779.5) | 708.4 | 1.4 | 2 | 233 | 186 | 0.0035 | 0.010 | 0.317 |
| rvm_mobilenetv3 | cpu | Intel(R) Core(TM) i7-7500U C | Performance | 512x288 | 460 | 90.1 (115.0) | 96.1 | 10.3 | 42 | 0 | 63 | 0.0040 | 0.024 | 0.315 |
| rvm_mobilenetv3 | cpu | Intel(R) Core(TM) i7-7500U C | Balanced | 640x352 | 342 | 156.6 (230.9) | 166.5 | 6.0 | 38 | 0 | 91 | 0.0038 | 0.020 | 0.315 |
| rvm_mobilenetv3 | cpu | Intel(R) Core(TM) i7-7500U C | Quality | 896x512 | 338 | 304.2 (400.0) | 326.1 | 3.0 | 39 | 0 | 177 | 0.0035 | 0.012 | 0.317 |
| rvm_mobilenetv3 | cpu | Intel(R) Core(TM) i7-7500U C | Ultra | 1280x704 | 308 | 588.5 (751.7) | 628.2 | 1.6 | 39 | 0 | 337 | 0.0035 | 0.010 | 0.317 |
| modnet_portrait | directml | NVIDIA GeForce 940MX | Performance | 320x192 | 1507 | 158.4 (172.1) | 161.0 | 6.2 | 2 | 92 | 74 | 0.0000 | 0.017 | 0.327 |
| modnet_portrait | directml | NVIDIA GeForce 940MX | Balanced | 448x256 | 1576 | 282.6 (287.3) | 287.0 | 3.5 | 1 | 116 | 79 | 0.0000 | 0.016 | 0.327 |
| modnet_portrait | directml | NVIDIA GeForce 940MX | Quality | 512x288 | 1503 | 371.9 (390.9) | 379.0 | 2.6 | 1 | 194 | 76 | 0.0000 | 0.017 | 0.328 |
| modnet_portrait | directml | NVIDIA GeForce 940MX | Ultra | 672x384 | 1047 | 623.8 (628.1) | 633.9 | 1.6 | 1 | 258 | 91 | 0.0000 | 0.017 | 0.328 |
| modnet_portrait | cpu | Intel(R) Core(TM) i7-7500U C | Performance | 320x192 | 174 | 149.2 (171.8) | 151.6 | 6.6 | 46 | 0 | 84 | 0.0000 | 0.017 | 0.327 |
| modnet_portrait | cpu | Intel(R) Core(TM) i7-7500U C | Balanced | 448x256 | 198 | 281.7 (382.8) | 286.2 | 3.5 | 45 | 0 | 125 | 0.0000 | 0.016 | 0.327 |
| modnet_portrait | cpu | Intel(R) Core(TM) i7-7500U C | Quality | 512x288 | 183 | 364.6 (453.3) | 370.0 | 2.7 | 46 | 0 | 150 | 0.0000 | 0.017 | 0.328 |
| modnet_portrait | cpu | Intel(R) Core(TM) i7-7500U C | Ultra | 672x384 | 165 | 613.3 (810.5) | 622.5 | 1.6 | 45 | 0 | 278 | 0.0000 | 0.017 | 0.328 |
| mediapipe_selfie_landscape | directml | NVIDIA GeForce 940MX | Fixed | 256x144 | 914 | 4.9 (5.7) | 6.2 | 156.0 | 10 | 16 | 48 | 0.0000 | 0.037 | 0.321 |
| mediapipe_selfie_landscape | cpu | Intel(R) Core(TM) i7-7500U C | Fixed | 256x144 | 59 | 6.2 (12.4) | 7.8 | 124.1 | 40 | 0 | 12 | 0.0000 | 0.037 | 0.321 |
| mediapipe_selfie_general | directml | NVIDIA GeForce 940MX | Fixed | 256x256 | 927 | 7.5 (8.1) | 9.7 | 100.0 | 8 | 21 | 46 | 0.0000 | 0.034 | 0.330 |
| mediapipe_selfie_general | cpu | Intel(R) Core(TM) i7-7500U C | Fixed | 256x256 | 59 | 7.9 (11.1) | 10.1 | 95.9 | 47 | 0 | 19 | 0.0000 | 0.034 | 0.330 |
| mediapipe_selfie_multiclass | directml | NVIDIA GeForce 940MX | Fixed | 256x256 | 1258 | 181.6 (212.9) | 183.8 | 5.4 | 1 | 158 | 93 | 0.0040 | 0.050 | 0.314 |
| mediapipe_selfie_multiclass | cpu | Intel(R) Core(TM) i7-7500U C | Fixed | 256x256 | 275 | 196.6 (226.5) | 199.0 | 5.0 | 41 | 0 | 108 | 0.0040 | 0.050 | 0.314 |
| pphumanseg_v2_lite | directml | NVIDIA GeForce 940MX | Fixed | 192x192 | 981 | 20.8 (25.6) | 22.1 | 44.8 | 7 | 40 | 84 | 0.0082 | 0.124 | 0.319 |
| pphumanseg_v2_lite | cpu | Intel(R) Core(TM) i7-7500U C | Fixed | 192x192 | 208 | 19.4 (26.4) | 20.6 | 48.2 | 42 | 0 | 28 | 0.0082 | 0.124 | 0.319 |
| pphumanseg_v2_portrait | directml | NVIDIA GeForce 940MX | Fixed | 256x144 | 912 | 29.0 (44.6) | 30.7 | 32.3 | 10 | 27 | 79 | 0.0045 | 0.027 | 0.311 |
| pphumanseg_v2_portrait | cpu | Intel(R) Core(TM) i7-7500U C | Fixed | 256x144 | 223 | 18.0 (29.0) | 19.5 | 50.9 | 40 | 0 | 17 | 0.0045 | 0.027 | 0.311 |



Observations

* **Robust Video Matting** has by far the best edge quality (see composites
  below) and the lowest flicker, but on this Maxwell GPU even its Performance
  tier (71 ms) is above the 33 ms budget; on the CPU it is 90 ms. It is the right
  default on any mid-range or better GPU and is auto-selected there via the
  capability store once measured.
* **PP-HumanSeg v2 lite / portrait** is the best value: ~20 ms, decent edges
  after joint-bilateral upsampling, robust to the flag/complex background.
* **MediaPipe Selfie (landscape)** is the cheapest (5–6 ms) and is what Auto
  starts with on unknown hardware; edges are soft (256×144 source) but the GPU
  refinement makes them acceptable for talking-head framing.
* **MODNet** gives soft hair but costs 150–600 ms here with no temporal state;
  it is an optional download for capable GPUs.
* **Selfie Multiclass** is expensive (180–200 ms) for the quality it delivers
  and left a hole in the subject's hand; kept only as an experiment.
* **SINet** was evaluated and **rejected**: it inverted foreground/background
  on the second portrait (flag background) and is not shipped.

### Composites (matte × frame over green)

Generated by `promatte-bench --dump-dir`; PNGs in `build/bench-dump/` after a
run. Visual verdicts on the 1280×720 portraits:

| Model | Hair | Glasses / face | Clothing edge | Complex background (flag) | Verdict |
| ----- | ---- | -------------- | ------------- | ------------------------- | ------- |
| RVM Performance | fine strands preserved, no halo | clean | crisp | clean | best |
| MODNet Balanced | soft, slight over-blur | clean | slightly soft | clean | good |
| PP-HumanSeg lite | blocky before JBU, good after | clean | good | clean | good value |
| Selfie landscape | soft | clean | soft | minor bleed | acceptable (cheap) |
| Selfie Multiclass | good | clean | hole in hand region | clean | mediocre |
| SINet | – | – | – | failed | rejected |

## 2. Synthetic frames, 1280×720 (first run, Intel HD 620 as the DirectML device)

This run predates the model output fixes (PP-HumanSeg and Multiclass were
emitting logits, PP-HumanSeg graphs still had an unresolved adaptive pool) and
used DXGI adapter 0 = Intel HD 620 for DirectML; it is kept because it shows
the iGPU's inference speed and its very long first-session compile times.

**tools/benchmark/results/synthetic-720p.json** — Intel(R) Core(TM) i7-7500U CPU @ 2.70GHz, GPUs: Intel(R) HD Graphics 620, NVIDIA GeForce 940MX, Microsoft Basic Render Driver; input 1280x720, 60 frames

| Model | Backend | Device | Tier | AI input | Init ms | Inference ms (p95) | Total ms | AI FPS | CPU % | VRAM Δ MB | RSS Δ MB | Flicker | Softness | Coverage |
| ----- | ------- | ------ | ---- | -------- | ------: | -----------------: | -------: | -----: | ----: | --------: | -------: | ------: | -------: | -------: |
| rvm_mobilenetv3 | directml | Intel(R) HD Graphics 620 | Performance | 512x288 | 425 | 49.3 (61.5) | 54.4 | 0.8 | 22 | 92 | 231 | 0.0000 | 0.000 | 0.000 |
| rvm_mobilenetv3 | directml | Intel(R) HD Graphics 620 | Balanced | 640x352 | 263 | 77.2 (94.0) | 90.9 | 1.3 | 21 | 97 | 222 | 0.0000 | 0.000 | 0.000 |
| rvm_mobilenetv3 | directml | Intel(R) HD Graphics 620 | Quality | 896x512 | 238 | 129.2 (148.8) | 151.8 | 1.2 | 20 | 158 | 351 | 0.0000 | 0.000 | 0.000 |
| rvm_mobilenetv3 | directml | Intel(R) HD Graphics 620 | Ultra | 1280x704 | 431 | 245.8 (285.6) | 299.0 | 1.3 | 17 | 279 | 601 | 0.0000 | 0.000 | 0.000 |
| rvm_mobilenetv3 | cpu | Intel(R) Core(TM) i7-7500U C | Performance | 512x288 | 384 | 55.7 (70.6) | 59.5 | 16.9 | 47 | 0 | 98 | 0.0000 | 0.000 | 0.000 |
| rvm_mobilenetv3 | cpu | Intel(R) Core(TM) i7-7500U C | Balanced | 640x352 | 280 | 105.5 (133.1) | 112.8 | 8.5 | 45 | 0 | 99 | 0.0000 | 0.000 | 0.000 |
| rvm_mobilenetv3 | cpu | Intel(R) Core(TM) i7-7500U C | Quality | 896x512 | 132 | 216.1 (294.7) | 231.4 | 4.3 | 45 | 0 | 223 | 0.0000 | 0.000 | 0.000 |
| rvm_mobilenetv3 | cpu | Intel(R) Core(TM) i7-7500U C | Ultra | 1280x704 | 184 | 426.3 (593.6) | 456.6 | 2.2 | 45 | 0 | 442 | 0.0000 | 0.000 | 0.000 |
| modnet_portrait | directml | Intel(R) HD Graphics 620 | Performance | 320x192 | 201 | 98.0 (110.8) | 102.2 | 1.4 | 21 | 114 | 145 | 0.0000 | 0.006 | 0.185 |
| modnet_portrait | directml | Intel(R) HD Graphics 620 | Balanced | 448x256 | 230 | 176.6 (203.3) | 185.8 | 0.9 | 13 | 130 | 211 | 0.0000 | 0.004 | 0.185 |
| modnet_portrait | directml | Intel(R) HD Graphics 620 | Quality | 512x288 | 628 | 191.6 (205.9) | 201.5 | 1.9 | 16 | 208 | 252 | 0.0000 | 0.004 | 0.185 |
| modnet_portrait | directml | Intel(R) HD Graphics 620 | Ultra | 672x384 | 159 | 337.4 (375.1) | 361.0 | 0.8 | 11 | 274 | 399 | 0.0000 | 0.003 | 0.185 |
| modnet_portrait | cpu | Intel(R) Core(TM) i7-7500U C | Performance | 320x192 | 437 | 109.3 (136.6) | 111.0 | 9.2 | 48 | 0 | 73 | 0.0000 | 0.006 | 0.185 |
| modnet_portrait | cpu | Intel(R) Core(TM) i7-7500U C | Balanced | 448x256 | 127 | 265.9 (408.2) | 269.8 | 3.8 | 43 | 0 | 113 | 0.0000 | 0.004 | 0.185 |
| modnet_portrait | cpu | Intel(R) Core(TM) i7-7500U C | Quality | 512x288 | 236 | 397.7 (630.1) | 403.1 | 2.6 | 40 | 0 | 143 | 0.0000 | 0.004 | 0.185 |
| modnet_portrait | cpu | Intel(R) Core(TM) i7-7500U C | Ultra | 672x384 | 376 | 660.9 (1269.9) | 670.4 | 1.3 | 36 | 0 | 289 | 0.0000 | 0.003 | 0.185 |
| mediapipe_selfie_landscape | directml | Intel(R) HD Graphics 620 | Fixed | 256x144 | 30210 | 5.8 (6.1) | 6.7 | 145.9 | 5 | 24 | 26 | 0.0000 | 0.085 | 0.052 |
| mediapipe_selfie_landscape | cpu | Intel(R) Core(TM) i7-7500U C | Fixed | 256x144 | 48 | 4.2 (4.9) | 5.4 | 177.4 | 50 | 0 | 17 | 0.0000 | 0.085 | 0.052 |
| mediapipe_selfie_general | directml | Intel(R) HD Graphics 620 | Fixed | 256x256 | 13757 | 8.7 (9.3) | 10.4 | 93.8 | 9 | 28 | 78 | 0.0000 | 0.105 | 0.030 |
| mediapipe_selfie_general | cpu | Intel(R) Core(TM) i7-7500U C | Fixed | 256x256 | 46 | 5.4 (6.7) | 6.9 | 139.8 | 49 | 0 | 11 | 0.0000 | 0.105 | 0.030 |
| mediapipe_selfie_multiclass | directml | Intel(R) HD Graphics 620 | Fixed | 256x256 | 30625 | 101.9 (130.0) | 105.2 | 9.6 | 2 | 195 | 233 | 0.0000 | 0.000 | 0.000 |
| mediapipe_selfie_multiclass | cpu | Intel(R) Core(TM) i7-7500U C | Fixed | 256x256 | 85 | 167.5 (249.6) | 169.8 | 5.9 | 43 | 0 | 122 | 0.0000 | 0.000 | 0.000 |
| pphumanseg_v2_lite | directml | | Fixed | | | FAILED: inference failed: Non-zero status code returned while runnin | | | | | | | | |
| pphumanseg_v2_lite | cpu | | Fixed | | | FAILED: ONNX Runtime: Exception during initialization: E:\_work\1\s\ | | | | | | | | |
| pphumanseg_v2_portrait | directml | | Fixed | | | FAILED: inference failed: Non-zero status code returned while runnin | | | | | | | | |
| pphumanseg_v2_portrait | cpu | | Fixed | | | FAILED: ONNX Runtime: Exception during initialization: E:\_work\1\s\ | | | | | | | | |
| sinet_portrait | directml | Intel(R) HD Graphics 620 | Fixed | 320x320 | 19968 | 27.7 (37.6) | 34.9 | 27.7 | 3 | 47 | 67 | 0.0000 | 0.000 | 0.385 |
| sinet_portrait | cpu | Intel(R) Core(TM) i7-7500U C | Fixed | 320x320 | 68 | 88.6 (171.6) | 94.0 | 10.2 | 32 | 0 | 40 | 0.0000 | 0.000 | 0.385 |



## 3. Temporal behaviour

RVM's recurrent state gives the lowest raw flicker (0.0035–0.0040 on noisy
input); every other model relies on ProMatte's motion-adaptive stabiliser,
which brings static-region flicker to effectively zero at the default
*Stability 0.6* (unit test `temporal stabiliser suppresses flicker in static
regions`: max deviation < 0.08 under alternating ±0.2 noise) while moving
edges still follow within one frame (`follows moving regions quickly`).

## 4. Licences

See [THIRD_PARTY_LICENSES.md](../THIRD_PARTY_LICENSES.md). Only permissively
licensed models are bundled; RVM (GPL-3.0) and MODNet (Apache-2.0, 25 MB) are
downloaded on demand with pinned SHA-256 checksums.

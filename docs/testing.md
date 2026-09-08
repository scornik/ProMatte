# ProMatte — Testing

| Suite | Command | What it covers |
| ----- | ------- | -------------- |
| Unit | `build\bin\Release\promatte-unit-tests.exe` | matte refinement, alpha maths, temporal smoothing, pre-processing, model descriptors and output decoding, model manager + downloader, capability store, performance controller, inference worker lifecycle, SHA-256 |
| Integration | `build\bin\Release\promatte-integration-tests.exe` | boots libobs headless with the D3D11 renderer, loads the built module, creates/destroys the filter on a synthetic source, every background mode and debug view, resolution changes, enable/disable, backend and model switching, render-cost budgets, 30 s memory check |
| Live (needs OBS + a webcam) | `py -3.12 tests\integration\obs_live_test.py --password <obs-websocket password> --soak 10` | drives real OBS through obs-websocket: presets, modes, quality levels, backends, models, filter toggling, scene switching, camera resolution changes, optional recording, soak; captures a screenshot per step and a JSON report |
| Benchmark | `build\bin\Release\promatte-bench.exe --backend all --input tests\visual\frames` | per-model/backend/tier latency, throughput, CPU, VRAM, flicker and softness; JSON + optional matte/composite dumps |

`ctest --test-dir build -C Release` runs the unit and integration suites.

## Scenario coverage

The specification lists 20 camera scenarios. Those that can be exercised
deterministically are covered by the benchmark's frame sets and the harness'
synthetic source; the rest need a human in front of the camera and are recorded
in [final-verification.md](final-verification.md).

| Scenario | How it is covered |
| -------- | ----------------- |
| Plain wall, complex office, bookshelf, window/backlight | live webcam runs (author's room, window behind and beside the subject) + portrait frames with a flag background |
| Low light / bright light | live runs with room lights on/off; the camera's auto-exposure changes are what the temporal stabiliser has to absorb |
| Dark / white clothing | live runs (dark shirt, light shirt) |
| Curly / long hair, glasses, headphones, microphone | live runs; RVM keeps strands and glasses, the confidence curve keeps semi-transparent hair |
| Fast hand and head movement | live runs; the motion-adaptive stabiliser unlocks moving regions within a frame |
| Sitting / standing | live runs |
| Two people | single-person models: the largest connected component is always kept, a second person is kept only while the model segments them; documented as a limitation |
| Similar-colour background, shadows | benchmark frames + live runs; edge decontamination uses the estimated background colour |

## Stress testing

* Headless: `promatte-integration-tests.exe -tc="memory stays bounded*"` renders
  for 30 s and asserts RSS and process VRAM growth stay under 64 MB.
* Live: `obs_live_test.py --soak 30` samples OBS render FPS, skipped frames and
  memory every 30 s.
* Longer runs (2 h / 8 h) are done by raising `--soak`; results are recorded in
  the verification report.

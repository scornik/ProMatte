# ProMatte — Troubleshooting

Start with **Filters → ProMatte → Performance → Refresh statistics**: the
status text names the backend, model, AI resolution and any error. The OBS log
(`Help → Log Files`) contains every ProMatte message prefixed with `[promatte]`.

## "Remove (transparent)" looks like it does nothing

Transparent is not a preview mode — it makes the background *see-through*, and
you only notice that when there is something behind the camera to see. On its
own, in a scene with nothing underneath it, the removed area shows the canvas
(black) or, in the filter properties dialog, the dialog's own background.

**If you just want the person on their own, pick the mode that draws something
in place of the background:**

| You want | Choose |
| -------- | ------ |
| Person over a colour | **Replace with solid color** |
| Person over a photo | **Replace with image** |
| Person over your game, slides, or another source | **Remove (transparent)**, then put that source *below* the camera in the scene |
| Keep the room but push it back | **Blur** or **Dim** |

Blur is the quickest way to confirm the AI is working at all, because the effect
is visible with nothing else in the scene.

To check the matte itself, set *Advanced → Debug view → Show matte*: you should
see a white silhouette of yourself on black.

## Two copies of the plugin installed

If an update seems to change nothing, look in the OBS log for:

```
obs_register_source: Source 'promatte_filter' already exists!  Duplicate library?
```

That means ProMatte exists both inside the OBS folder
(`obs-plugins\64bit`) and in `%ProgramData%\obs-studio\plugins\promatte`. OBS
loads both, rejects the second registration and keeps whichever loaded first —
usually the older one — so your update never runs. ProMatte also logs this
itself as `ANOTHER COPY OF PROMATTE IS ALREADY LOADED`, naming the file being
ignored. Delete one of the two copies; the installer removes the ProgramData
copy for you.

## The filter does nothing (video looks unchanged)

* **"No AI model installed"** in the status: the bundled models are missing
  (reinstall) or the download has not been done yet. Open *Model manager*,
  select a model and click *Download / install*.
* **"Loading model…"** for a long time: the first DirectML session on a GPU
  compiles shaders; on integrated Intel GPUs this can take 30–90 s the first
  time. Subsequent starts are faster. Video passes through unchanged meanwhile.
* **"AI processing error"**: read the message. Typical causes: corrupt model
  file (delete it in the Model manager and download again), out of video
  memory (choose *Performance* quality or the CPU backend), driver problems
  (update the GPU driver).
* The filter is **disabled** (eye icon) — ProMatte stops all AI work when the
  filter is disabled or hidden.

## Wrong GPU is used / very slow on a laptop

On laptops with an integrated and a discrete GPU, ProMatte uses the adapter OBS
renders on. Some old discrete GPUs are slower with DirectML than the iGPU or
even the CPU. Set *Advanced → AI GPU* explicitly, or *AI backend → CPU*, and
compare the *AI processing* FPS in the status text. `tools/benchmark`
(`promatte-bench --backend all`) measures every option on your machine.

## OBS render FPS drops

ProMatte never blocks the render thread, but the GPU is shared. If the *Render*
time in the status text is above ~3 ms:

* set *Edge upsampling* to *Fast (3×3)*,
* reduce *Feather*,
* use *Blur quality → Fast* or a lower *Blur amount*,
* set *Quality → Performance* (fewer pixels to upsample from a bigger matte).

If OBS' own stats show *rendering lag* while ProMatte's render time is small,
another source or the encoder is the bottleneck.

## Flickering or crawling edges

* Increase *Temporal → Stability*; decrease *Motion response* slightly.
* Use the *Robust Video Matting* model (Model manager) — it is temporally
  stable by design.
* Lock the camera's exposure/white balance; automatic exposure changes look
  like motion to any segmentation model.

## Halo / fringe around hair or shoulders

* Increase *Edge decontamination* and *Halo removal*.
* Move *Edge shift* slightly negative (erodes the matte by 1–2 px).
* Lower *Smoothness* for a crisper edge, or raise it for softer hair.
* Light the subject separately from the background; a bright wall directly
  behind the head is the hardest case for every matting model.

## Parts of the person disappear (hands, headset, microphone)

* Lower *Foreground / background separation* (e.g. 0.35).
* Increase *Edge shift* (dilates the matte).
* Disable *Remove stray background blobs* if a detached hand is being removed.
* Switch to the RVM model; segmentation models trained on "person" classes
  sometimes drop held objects.

## Background image / video not shown

* Image: PNG/JPG/WebP/BMP/GIF/TGA/PSD are supported via OBS' image loader; the
  path must be accessible to OBS.
* Video: uses OBS' Media Source internally (any format FFmpeg decodes). Audio
  is muted. If the video does not play, test the file in a normal Media Source.
* OBS source: the selected source must be visible somewhere or it may not
  render; it cannot be the camera itself.

## Recording or stream shows a different result than the preview

The filter output is what OBS composes — preview, recording and stream see the
same frames. Check that the *debug view* is off and that no second filter is
placed after ProMatte that ignores alpha.

## Crash or freeze

ProMatte isolates AI failures and falls back to pass-through. If OBS still
crashes, collect `%APPDATA%\obs-studio\logs` and `crashes` and open an issue;
include the *Performance* status text and the GPU/driver version. Run OBS with
the environment variable `PROMATTE_DEBUG=1` for verbose logging and the on-video
developer overlay.

## Uninstall / clean-up

Uninstalling removes the plugin from the OBS folder. Downloaded models remain in
`%APPDATA%\obs-studio\plugin_config\promatte\models\` (delete the folder to
remove them). Filter settings live in your scene collections.

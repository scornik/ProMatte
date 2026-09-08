#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <graphics/graphics.h>

#include "inference/frame_types.h"
#include "obs/settings.h"

namespace promatte {

struct RenderParams {
	BackgroundMode mode = BackgroundMode::Transparent;
	BackgroundFit fit = BackgroundFit::Cover;
	float color[4] = {0.f, 1.f, 0.f, 1.f};
	float colorOpacity = 1.f;
	float blurAmount = 0.5f;
	int blurQuality = 1;
	float dimBrightness = 0.35f;
	float dimOpacity = 1.f;
	float featherPx = 0.f;
	float decontaminate = 0.5f;
	float haloRemoval = 0.3f;
	int upsampleQuality = 1;
	float rangeSigma = 0.12f;
	DebugView debugView = DebugView::None;
};

// All GPU work of the filter. Every method must be called on the OBS graphics
// thread (inside video_render or obs_enter_graphics()).
//
//   captureBegin/End  -> source texture (full resolution)
//   stageAndReadback  -> AI-resolution BGRA frame for the inference worker
//                        (downscale on GPU, 1-frame-delayed staging ring, no stall)
//   uploadMatte       -> low-resolution RGBA matte texture
//   render            -> refine (JBU + feather), background estimate/blur, composite
class GpuPipeline {
public:
	GpuPipeline() = default;
	~GpuPipeline();
	GpuPipeline(const GpuPipeline &) = delete;
	GpuPipeline &operator=(const GpuPipeline &) = delete;

	bool loadEffects(const std::string &effectsDir, std::string &error);
	void free();
	bool ready() const { return downscaleFx_ && refineFx_ && blurFx_ && compositeFx_; }

	bool captureBegin(uint32_t w, uint32_t h);
	void captureEnd();
	gs_texture_t *sourceTexture() const;

	// Renders the captured source at aiW x aiH into the AI-resolution target and
	// stages it for readback (cheap, no CPU/GPU sync).
	void stageFrame(uint32_t aiW, uint32_t aiH);
	// Maps the most recently completed staging surface into `out`. This is a GPU
	// sync point, so the caller only does it when the worker can use the frame.
	bool readbackFrame(FrameBuffer &out);

	void uploadMatte(const MatteBuffer &m);
	bool hasMatte() const { return matteTex_ != nullptr; }
	void dropMatte();

	// Draws the final result to the current render target (w x h). When
	// `reuseRefined` is set the joint-bilateral upsample, feather and background
	// estimate from the previous call are reused - OBS renders a filter once per
	// output and once per preview/projector, and those passes only need doing once
	// per frame.
	void render(const RenderParams &p, gs_texture_t *background, uint32_t bgW, uint32_t bgH,
		    bool reuseRefined = false);
	// Draws the captured source unchanged (fail-safe path).
	void renderPassthrough();

	// Video memory estimate of our own allocations (for the overlay).
	uint64_t textureBytes() const { return textureBytes_; }

private:
	gs_texture_t *refineAlpha(const RenderParams &p);
	gs_texture_t *estimateBackground(const RenderParams &p, gs_texture_t *alphaTex);
	void drawEffect(gs_effect_t *fx, const char *technique, gs_texture_t *image, gs_texrender_t *target,
			uint32_t w, uint32_t h);
	void ensureStage(uint32_t w, uint32_t h);
	void freeStage();

	gs_effect_t *downscaleFx_ = nullptr;
	gs_effect_t *refineFx_ = nullptr;
	gs_effect_t *blurFx_ = nullptr;
	gs_effect_t *compositeFx_ = nullptr;

	gs_texrender_t *srcRender_ = nullptr;
	gs_texrender_t *aiRender_ = nullptr;
	gs_texrender_t *mergeRender_ = nullptr;   // AI-res: rgb = colour, a = matte
	gs_texrender_t *alphaRender_ = nullptr;
	gs_texrender_t *featherRender_[2] = {nullptr, nullptr};
	gs_texrender_t *premulRender_ = nullptr;
	std::vector<gs_texrender_t *> blurDown_;
	std::vector<gs_texrender_t *> blurUp_;
	gs_texrender_t *bgEstRender_ = nullptr;

	gs_stagesurf_t *stage_[2] = {nullptr, nullptr};
	bool stageValid_[2] = {false, false};
	int stageIndex_ = 0;
	int stageReady_ = -1; // most recently staged surface, -1 = none pending
	uint32_t stageW_ = 0, stageH_ = 0;

	gs_texture_t *matteTex_ = nullptr;
	uint32_t matteW_ = 0, matteH_ = 0;
	bool matteHasFg_ = false;

	uint32_t w_ = 0, h_ = 0;
	bool captured_ = false;
	uint64_t textureBytes_ = 0;
	gs_texture_t *lastAlpha_ = nullptr;    // valid only within the current frame
	gs_texture_t *lastBgEstimate_ = nullptr;
};

} // namespace promatte

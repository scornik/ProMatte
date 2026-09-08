#include "rendering/gpu_pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <obs-module.h>

#include "utils/logging.h"

namespace promatte {

namespace {

gs_effect_t *loadEffect(const std::string &dir, const char *name, std::string &error)
{
	std::string path = dir + "/" + name;
	char *errors = nullptr;
	gs_effect_t *fx = gs_effect_create_from_file(path.c_str(), &errors);
	if (!fx) {
		error += std::string("failed to load effect ") + name + (errors ? std::string(": ") + errors : "") + "\n";
	}
	bfree(errors);
	return fx;
}

void setTex(gs_effect_t *fx, const char *name, gs_texture_t *tex)
{
	gs_eparam_t *p = gs_effect_get_param_by_name(fx, name);
	if (p)
		gs_effect_set_texture(p, tex);
}
void setFloat(gs_effect_t *fx, const char *name, float v)
{
	gs_eparam_t *p = gs_effect_get_param_by_name(fx, name);
	if (p)
		gs_effect_set_float(p, v);
}
void setInt(gs_effect_t *fx, const char *name, int v)
{
	gs_eparam_t *p = gs_effect_get_param_by_name(fx, name);
	if (p)
		gs_effect_set_int(p, v);
}
void setVec2(gs_effect_t *fx, const char *name, float x, float y)
{
	gs_eparam_t *p = gs_effect_get_param_by_name(fx, name);
	if (p) {
		struct vec2 v;
		vec2_set(&v, x, y);
		gs_effect_set_vec2(p, &v);
	}
}
void setVec4(gs_effect_t *fx, const char *name, const float *c)
{
	gs_eparam_t *p = gs_effect_get_param_by_name(fx, name);
	if (p) {
		struct vec4 v;
		vec4_set(&v, c[0], c[1], c[2], c[3]);
		gs_effect_set_vec4(p, &v);
	}
}

void destroyRender(gs_texrender_t *&r)
{
	if (r) {
		gs_texrender_destroy(r);
		r = nullptr;
	}
}

} // namespace

GpuPipeline::~GpuPipeline()
{
	free();
}

bool GpuPipeline::loadEffects(const std::string &dir, std::string &error)
{
	error.clear();
	downscaleFx_ = loadEffect(dir, "promatte_downscale.effect", error);
	refineFx_ = loadEffect(dir, "promatte_refine.effect", error);
	blurFx_ = loadEffect(dir, "promatte_blur.effect", error);
	compositeFx_ = loadEffect(dir, "promatte_composite.effect", error);
	if (!ready()) {
		free();
		return false;
	}
	srcRender_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	aiRender_ = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	mergeRender_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	alphaRender_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	featherRender_[0] = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	featherRender_[1] = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	premulRender_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	bgEstRender_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	for (int i = 0; i < 5; ++i) {
		blurDown_.push_back(gs_texrender_create(GS_RGBA, GS_ZS_NONE));
		blurUp_.push_back(gs_texrender_create(GS_RGBA, GS_ZS_NONE));
	}
	return true;
}

void GpuPipeline::free()
{
	destroyRender(srcRender_);
	destroyRender(aiRender_);
	destroyRender(mergeRender_);
	destroyRender(alphaRender_);
	destroyRender(featherRender_[0]);
	destroyRender(featherRender_[1]);
	destroyRender(premulRender_);
	destroyRender(bgEstRender_);
	for (auto &r : blurDown_)
		destroyRender(r);
	for (auto &r : blurUp_)
		destroyRender(r);
	blurDown_.clear();
	blurUp_.clear();
	freeStage();
	dropMatte();
	lastAlpha_ = nullptr;
	lastBgEstimate_ = nullptr;
	auto destroyFx = [](gs_effect_t *&fx) {
		if (fx) {
			gs_effect_destroy(fx);
			fx = nullptr;
		}
	};
	destroyFx(downscaleFx_);
	destroyFx(refineFx_);
	destroyFx(blurFx_);
	destroyFx(compositeFx_);
	captured_ = false;
	textureBytes_ = 0;
}

bool GpuPipeline::captureBegin(uint32_t w, uint32_t h)
{
	captured_ = false;
	if (w != w_ || h != h_) {
		lastAlpha_ = nullptr;
		lastBgEstimate_ = nullptr;
	}
	if (!srcRender_ || w == 0 || h == 0)
		return false;
	w_ = w;
	h_ = h;
	gs_texrender_reset(srcRender_);
	if (!gs_texrender_begin(srcRender_, w, h))
		return false;
	struct vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, float(w), 0.0f, float(h), -100.0f, 100.0f);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	return true;
}

void GpuPipeline::captureEnd()
{
	gs_blend_state_pop();
	gs_texrender_end(srcRender_);
	captured_ = true;
}

gs_texture_t *GpuPipeline::sourceTexture() const
{
	return (captured_ && srcRender_) ? gs_texrender_get_texture(srcRender_) : nullptr;
}

void GpuPipeline::ensureStage(uint32_t w, uint32_t h)
{
	if (stage_[0] && stageW_ == w && stageH_ == h)
		return;
	freeStage();
	for (int i = 0; i < 2; ++i)
		stage_[i] = gs_stagesurface_create(w, h, GS_BGRA);
	stageW_ = w;
	stageH_ = h;
	stageIndex_ = 0;
	textureBytes_ += uint64_t(w) * h * 4 * 2;
}

void GpuPipeline::freeStage()
{
	for (int i = 0; i < 2; ++i) {
		if (stage_[i]) {
			gs_stagesurface_destroy(stage_[i]);
			stage_[i] = nullptr;
		}
		stageValid_[i] = false;
	}
	textureBytes_ -= std::min<uint64_t>(textureBytes_, uint64_t(stageW_) * stageH_ * 4 * 2);
	stageW_ = stageH_ = 0;
}

void GpuPipeline::drawEffect(gs_effect_t *fx, const char *technique, gs_texture_t *image, gs_texrender_t *target,
			     uint32_t w, uint32_t h)
{
	gs_texrender_reset(target);
	if (!gs_texrender_begin(target, w, h))
		return;
	gs_ortho(0.0f, float(w), 0.0f, float(h), -100.0f, 100.0f);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	if (image)
		setTex(fx, "image", image);
	while (gs_effect_loop(fx, technique))
		gs_draw_sprite(image, 0, w, h);
	gs_blend_state_pop();
	gs_texrender_end(target);
}

void GpuPipeline::stageFrame(uint32_t aiW, uint32_t aiH)
{
	gs_texture_t *src = sourceTexture();
	if (!src || aiW == 0 || aiH == 0)
		return;
	ensureStage(aiW, aiH);

	// Downscale into the AI-resolution render target.
	setVec2(downscaleFx_, "offset", (float(w_) / float(aiW)) * 0.25f / float(w_),
		(float(h_) / float(aiH)) * 0.25f / float(h_));
	drawEffect(downscaleFx_, "Draw", src, aiRender_, aiW, aiH);
	gs_texture_t *aiTex = gs_texrender_get_texture(aiRender_);
	if (!aiTex)
		return;
	// Copy into the staging surface that is not waiting to be read. This is an
	// asynchronous GPU copy; nothing blocks here.
	gs_stage_texture(stage_[stageIndex_], aiTex);
	stageValid_[stageIndex_] = true;
	stageReady_ = stageIndex_;
	stageIndex_ = (stageIndex_ + 1) % 2;
}

bool GpuPipeline::readbackFrame(FrameBuffer &out)
{
	if (stageReady_ < 0 || !stage_[stageReady_] || !stageValid_[stageReady_])
		return false;
	const int idx = stageReady_;
	uint8_t *data = nullptr;
	uint32_t linesize = 0;
	if (!gs_stagesurface_map(stage_[idx], &data, &linesize))
		return false;
	out.resize(stageW_, stageH_);
	const size_t rowBytes = size_t(stageW_) * 4;
	for (uint32_t y = 0; y < stageH_; ++y)
		std::memcpy(out.bgra.data() + size_t(y) * rowBytes, data + size_t(y) * linesize, rowBytes);
	gs_stagesurface_unmap(stage_[idx]);
	stageValid_[idx] = false;
	stageReady_ = -1;
	return true;
}

void GpuPipeline::uploadMatte(const MatteBuffer &m)
{
	if (m.empty())
		return;
	if (!matteTex_ || matteW_ != m.width || matteH_ != m.height) {
		dropMatte();
		matteTex_ = gs_texture_create(m.width, m.height, GS_RGBA, 1, nullptr, GS_DYNAMIC);
		if (!matteTex_)
			return;
		matteW_ = m.width;
		matteH_ = m.height;
		textureBytes_ += uint64_t(m.width) * m.height * 4;
	}
	gs_texture_set_image(matteTex_, m.rgba.data(), m.width * 4, false);
	matteHasFg_ = m.hasForeground;
}

void GpuPipeline::dropMatte()
{
	if (matteTex_) {
		gs_texture_destroy(matteTex_);
		matteTex_ = nullptr;
		textureBytes_ -= std::min<uint64_t>(textureBytes_, uint64_t(matteW_) * matteH_ * 4);
	}
	matteW_ = matteH_ = 0;
	matteHasFg_ = false;
}

gs_texture_t *GpuPipeline::refineAlpha(const RenderParams &p)
{
	gs_texture_t *src = sourceTexture();
	gs_texture_t *guideLow = gs_texrender_get_texture(aiRender_);
	if (!src || !matteTex_)
		return nullptr;
	setVec2(refineFx_, "lowTexel", 1.f / float(matteW_), 1.f / float(matteH_));
	// Pack the AI-resolution colour and the matte into one texture so the
	// upsampling loop needs a single fetch per neighbour (AI resolution: cheap).
	gs_texture_t *lowGuide = matteTex_;
	if (guideLow) {
		setTex(refineFx_, "matte", matteTex_);
		setTex(refineFx_, "guideLow", guideLow);
		drawEffect(refineFx_, "Merge", guideLow, mergeRender_, matteW_, matteH_);
		if (gs_texture_t *merged = gs_texrender_get_texture(mergeRender_))
			lowGuide = merged;
	}
	// Joint bilateral upsampling to full resolution.
	setTex(refineFx_, "matte", lowGuide);
	setVec2(refineFx_, "lowSize", float(matteW_), float(matteH_));
	setFloat(refineFx_, "rangeSigma", p.rangeSigma);
	drawEffect(refineFx_, p.upsampleQuality > 0 ? "Upsample5" : "Upsample3", src, alphaRender_, w_, h_);
	gs_texture_t *alpha = gs_texrender_get_texture(alphaRender_);
	if (!alpha)
		return nullptr;
	if (p.featherPx >= 0.5f) {
		setVec2(refineFx_, "texel", 1.f / float(w_), 1.f / float(h_));
		setFloat(refineFx_, "radius", p.featherPx);
		setVec2(refineFx_, "direction", 1.f, 0.f);
		drawEffect(refineFx_, "Feather", alpha, featherRender_[0], w_, h_);
		gs_texture_t *t0 = gs_texrender_get_texture(featherRender_[0]);
		setVec2(refineFx_, "direction", 0.f, 1.f);
		drawEffect(refineFx_, "Feather", t0, featherRender_[1], w_, h_);
		gs_texture_t *t1 = gs_texrender_get_texture(featherRender_[1]);
		if (t1)
			alpha = t1;
	}
	return alpha;
}

gs_texture_t *GpuPipeline::estimateBackground(const RenderParams &p, gs_texture_t *alphaTex)
{
	gs_texture_t *src = sourceTexture();
	if (!src || !alphaTex)
		return nullptr;
	const bool blurMode = p.mode == BackgroundMode::Blur;
	// Half resolution is plenty for a blurred background and keeps the cost low.
	// A blurred background needs no detail: work at 1/2 resolution for the highest
	// blur quality and 1/4 otherwise (4x less bandwidth on weak integrated GPUs).
	const uint32_t div = (blurMode && p.blurQuality >= 2) ? 2u : 4u;
	uint32_t hw = std::max<uint32_t>(w_ / div, 8), hh = std::max<uint32_t>(h_ / div, 8);
	setTex(blurFx_, "alphaTex", alphaTex);
	drawEffect(blurFx_, "Premultiply", src, premulRender_, hw, hh);
	gs_texture_t *cur = gs_texrender_get_texture(premulRender_);
	if (!cur)
		return nullptr;

	// Blur strength: more levels + larger offsets as the amount grows. For the
	// decontamination estimate (non-blur modes) a moderate blur is enough.
	float amount = blurMode ? p.blurAmount : 0.45f;
	int levels = blurMode ? (1 + int(std::lround(amount * 3.f))) : 3; // 1..4
	levels = std::min<int>(levels, blurMode ? (p.blurQuality == 0 ? 3 : p.blurQuality == 1 ? 4 : 5) : 3);
	float offset = 0.6f + 2.2f * amount;

	std::vector<std::pair<uint32_t, uint32_t>> sizes;
	uint32_t cw = hw, ch = hh;
	for (int i = 0; i < levels; ++i) {
		uint32_t nw = std::max<uint32_t>(cw / 2, 4), nh = std::max<uint32_t>(ch / 2, 4);
		setVec2(blurFx_, "texel", 1.f / float(cw), 1.f / float(ch));
		setFloat(blurFx_, "offset", offset);
		drawEffect(blurFx_, "Down", cur, blurDown_[size_t(i)], nw, nh);
		cur = gs_texrender_get_texture(blurDown_[size_t(i)]);
		sizes.emplace_back(cw, ch);
		cw = nw;
		ch = nh;
	}
	for (int i = levels - 1; i >= 0; --i) {
		auto [uw, uh] = sizes[size_t(i)];
		setVec2(blurFx_, "texel", 1.f / float(cw), 1.f / float(ch));
		setFloat(blurFx_, "offset", offset);
		drawEffect(blurFx_, "Up", cur, blurUp_[size_t(i)], uw, uh);
		cur = gs_texrender_get_texture(blurUp_[size_t(i)]);
		cw = uw;
		ch = uh;
	}
	drawEffect(blurFx_, "Normalize", cur, bgEstRender_, hw, hh);
	return gs_texrender_get_texture(bgEstRender_);
}

void GpuPipeline::render(const RenderParams &p, gs_texture_t *background, uint32_t bgW, uint32_t bgH, bool reuseRefined)
{
	gs_texture_t *src = sourceTexture();
	if (!src) {
		return;
	}
	const bool needEstimate = p.mode == BackgroundMode::Blur || p.decontaminate > 0.001f || p.haloRemoval > 0.001f;
	gs_texture_t *alpha = nullptr;
	gs_texture_t *bgEst = nullptr;
	if (reuseRefined && lastAlpha_) {
		alpha = lastAlpha_;
		bgEst = needEstimate ? lastBgEstimate_ : nullptr;
	} else {
		alpha = refineAlpha(p);
		bgEst = (alpha && needEstimate) ? estimateBackground(p, alpha) : nullptr;
		lastAlpha_ = alpha;
		lastBgEstimate_ = bgEst;
	}
	if (!alpha) {
		renderPassthrough();
		return;
	}

	// Background fit mapping.
	float uvScaleX = 1.f, uvScaleY = 1.f, uvOffX = 0.f, uvOffY = 0.f, clampOutside = 0.f;
	if (background && bgW && bgH && w_ && h_) {
		float sx = float(w_) / float(bgW), sy = float(h_) / float(bgH);
		float s = p.fit == BackgroundFit::Cover ? std::max(sx, sy) : p.fit == BackgroundFit::Contain ? std::min(sx, sy) : 0.f;
		if (p.fit == BackgroundFit::Stretch) {
			uvScaleX = uvScaleY = 1.f;
		} else {
			uvScaleX = sx / s;
			uvScaleY = sy / s;
			uvOffX = (1.f - uvScaleX) * 0.5f;
			uvOffY = (1.f - uvScaleY) * 0.5f;
			clampOutside = p.fit == BackgroundFit::Contain ? 1.f : 0.f;
		}
	}

	int mode = 0;
	switch (p.mode) {
	case BackgroundMode::Transparent:
		mode = 0;
		break;
	case BackgroundMode::Color:
		mode = 1;
		break;
	case BackgroundMode::Image:
	case BackgroundMode::Video:
	case BackgroundMode::Source:
		mode = background ? 2 : 1; // no background available -> fall back to colour
		break;
	case BackgroundMode::Blur:
		mode = bgEst ? 3 : 0;
		break;
	case BackgroundMode::Dim:
		mode = 4;
		break;
	}

	gs_effect_t *fx = compositeFx_;
	setTex(fx, "image", src);
	setTex(fx, "alphaTex", alpha);
	setTex(fx, "matteTex", matteTex_);
	setTex(fx, "bgEstimate", bgEst ? bgEst : src);
	setTex(fx, "background", background ? background : src);
	setVec2(fx, "bgUvScale", uvScaleX, uvScaleY);
	setVec2(fx, "bgUvOffset", uvOffX, uvOffY);
	setFloat(fx, "bgClampOutside", clampOutside);
	setInt(fx, "mode", mode);
	setVec4(fx, "color", p.color);
	setFloat(fx, "colorOpacity", p.colorOpacity);
	setFloat(fx, "dimBrightness", p.dimBrightness);
	setFloat(fx, "dimOpacity", p.dimOpacity);
	setFloat(fx, "decontaminate", bgEst || matteHasFg_ ? p.decontaminate : 0.f);
	setFloat(fx, "haloRemoval", bgEst ? p.haloRemoval : 0.f);
	setFloat(fx, "hasForeground", matteHasFg_ ? 1.f : 0.f);
	setInt(fx, "debugView", int(p.debugView) > 4 ? 0 : int(p.debugView));

	const bool srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	gs_enable_blending(false);
	while (gs_effect_loop(fx, "Draw"))
		gs_draw_sprite(src, 0, w_, h_);
	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(srgb);
}

void GpuPipeline::renderPassthrough()
{
	gs_texture_t *src = sourceTexture();
	if (!src)
		return;
	gs_effect_t *fx = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	const bool srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);
	gs_blend_state_push();
	gs_enable_blending(false);
	setTex(fx, "image", src);
	while (gs_effect_loop(fx, "Draw"))
		gs_draw_sprite(src, 0, w_, h_);
	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(srgb);
}

} // namespace promatte

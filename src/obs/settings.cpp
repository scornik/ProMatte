#include "obs/settings.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace promatte {

namespace {
float clamp01(double v)
{
	return float(std::clamp(v, 0.0, 1.0));
}
} // namespace

const char *backgroundModeKey(BackgroundMode m)
{
	switch (m) {
	case BackgroundMode::Transparent:
		return "transparent";
	case BackgroundMode::Color:
		return "color";
	case BackgroundMode::Image:
		return "image";
	case BackgroundMode::Video:
		return "video";
	case BackgroundMode::Blur:
		return "blur";
	case BackgroundMode::Dim:
		return "dim";
	case BackgroundMode::Source:
		return "source";
	}
	return "transparent";
}

BackgroundMode backgroundModeFromKey(const char *k)
{
	if (!k)
		return BackgroundMode::Transparent;
	if (!std::strcmp(k, "color"))
		return BackgroundMode::Color;
	if (!std::strcmp(k, "image"))
		return BackgroundMode::Image;
	if (!std::strcmp(k, "video"))
		return BackgroundMode::Video;
	if (!std::strcmp(k, "blur"))
		return BackgroundMode::Blur;
	if (!std::strcmp(k, "dim"))
		return BackgroundMode::Dim;
	if (!std::strcmp(k, "source"))
		return BackgroundMode::Source;
	return BackgroundMode::Transparent;
}

const char *debugViewKey(DebugView v)
{
	switch (v) {
	case DebugView::None:
		return "none";
	case DebugView::Matte:
		return "matte";
	case DebugView::Edges:
		return "edges";
	case DebugView::Confidence:
		return "confidence";
	case DebugView::Foreground:
		return "foreground";
	case DebugView::Motion:
		return "motion";
	}
	return "none";
}

DebugView debugViewFromKey(const char *k)
{
	if (!k)
		return DebugView::None;
	if (!std::strcmp(k, "matte"))
		return DebugView::Matte;
	if (!std::strcmp(k, "edges"))
		return DebugView::Edges;
	if (!std::strcmp(k, "confidence"))
		return DebugView::Confidence;
	if (!std::strcmp(k, "foreground"))
		return DebugView::Foreground;
	if (!std::strcmp(k, "motion"))
		return DebugView::Motion;
	return DebugView::None;
}

const char *fitKey(BackgroundFit f)
{
	switch (f) {
	case BackgroundFit::Cover:
		return "cover";
	case BackgroundFit::Contain:
		return "contain";
	case BackgroundFit::Stretch:
		return "stretch";
	}
	return "cover";
}

BackgroundFit fitFromKey(const char *k)
{
	if (!k)
		return BackgroundFit::Cover;
	if (!std::strcmp(k, "contain"))
		return BackgroundFit::Contain;
	if (!std::strcmp(k, "stretch"))
		return BackgroundFit::Stretch;
	return BackgroundFit::Cover;
}

void settingsDefaults(obs_data_t *d)
{
	obs_data_set_default_string(d, keys::Preset, "webcam");
	obs_data_set_default_string(d, keys::Mode, "transparent");
	obs_data_set_default_int(d, keys::Color, 0xFF00FF00);
	obs_data_set_default_double(d, keys::ColorOpacity, 1.0);
	obs_data_set_default_string(d, keys::ImagePath, "");
	obs_data_set_default_string(d, keys::VideoPath, "");
	obs_data_set_default_string(d, keys::SourceName, "");
	obs_data_set_default_string(d, keys::Fit, "cover");
	obs_data_set_default_double(d, keys::BlurAmount, 0.5);
	obs_data_set_default_int(d, keys::BlurQuality, 1);
	obs_data_set_default_double(d, keys::DimBrightness, 0.35);
	obs_data_set_default_double(d, keys::DimOpacity, 1.0);
	obs_data_set_default_string(d, keys::Quality, "auto");
	obs_data_set_default_string(d, keys::Model, "auto");
	obs_data_set_default_string(d, keys::Backend, "auto");
	obs_data_set_default_int(d, keys::CpuThreads, 0);
	obs_data_set_default_int(d, keys::GpuIndex, -1);
	obs_data_set_default_int(d, keys::ManualTier, -1);
	obs_data_set_default_int(d, keys::MaxAiFps, 0);
	obs_data_set_default_double(d, keys::Separation, 0.5);
	obs_data_set_default_double(d, keys::Smoothness, 0.35);
	obs_data_set_default_double(d, keys::Feather, 0.25);
	obs_data_set_default_double(d, keys::Shift, 0.0);
	obs_data_set_default_double(d, keys::Decontaminate, 0.5);
	obs_data_set_default_double(d, keys::HaloRemoval, 0.3);
	obs_data_set_default_bool(d, keys::RemoveBlobs, true);
	obs_data_set_default_bool(d, keys::FillHoles, true);
	obs_data_set_default_int(d, keys::UpsampleQuality, 1);
	obs_data_set_default_double(d, keys::Stability, 0.6);
	obs_data_set_default_double(d, keys::MotionResponse, 0.6);
	obs_data_set_default_string(d, keys::DebugView, "none");
	obs_data_set_default_bool(d, keys::Overlay, false);
	obs_data_set_default_string(d, keys::ModelToManage, "rvm_mobilenetv3");
	obs_data_set_default_string(d, keys::CustomModelPath, "");
}

FilterSettings settingsLoad(obs_data_t *d)
{
	FilterSettings s;
	s.preset = obs_data_get_string(d, keys::Preset);
	s.mode = backgroundModeFromKey(obs_data_get_string(d, keys::Mode));
	s.color = uint32_t(obs_data_get_int(d, keys::Color));
	s.colorOpacity = clamp01(obs_data_get_double(d, keys::ColorOpacity));
	s.imagePath = obs_data_get_string(d, keys::ImagePath);
	s.videoPath = obs_data_get_string(d, keys::VideoPath);
	s.sourceName = obs_data_get_string(d, keys::SourceName);
	s.fit = fitFromKey(obs_data_get_string(d, keys::Fit));
	s.blurAmount = clamp01(obs_data_get_double(d, keys::BlurAmount));
	s.blurQuality = int(std::clamp<long long>(obs_data_get_int(d, keys::BlurQuality), 0, 2));
	s.dimBrightness = clamp01(obs_data_get_double(d, keys::DimBrightness));
	s.dimOpacity = clamp01(obs_data_get_double(d, keys::DimOpacity));
	s.quality = qualityModeFromString(obs_data_get_string(d, keys::Quality));
	s.modelId = obs_data_get_string(d, keys::Model);
	s.backend = obs_data_get_string(d, keys::Backend);
	s.cpuThreads = int(std::clamp<long long>(obs_data_get_int(d, keys::CpuThreads), 0, 32));
	s.gpuIndex = int(std::clamp<long long>(obs_data_get_int(d, keys::GpuIndex), -1, 15));
	s.manualTier = int(std::clamp<long long>(obs_data_get_int(d, keys::ManualTier), -1, 16));
	s.maxAiFps = int(std::clamp<long long>(obs_data_get_int(d, keys::MaxAiFps), 0, 120));
	s.separation = float(std::clamp(obs_data_get_double(d, keys::Separation), 0.05, 0.95));
	s.smoothness = clamp01(obs_data_get_double(d, keys::Smoothness));
	s.feather = clamp01(obs_data_get_double(d, keys::Feather));
	s.shift = float(std::clamp(obs_data_get_double(d, keys::Shift), -1.0, 1.0));
	s.decontaminate = clamp01(obs_data_get_double(d, keys::Decontaminate));
	s.haloRemoval = clamp01(obs_data_get_double(d, keys::HaloRemoval));
	s.removeBlobs = obs_data_get_bool(d, keys::RemoveBlobs);
	s.fillHoles = obs_data_get_bool(d, keys::FillHoles);
	s.upsampleQuality = int(std::clamp<long long>(obs_data_get_int(d, keys::UpsampleQuality), 0, 1));
	s.stability = clamp01(obs_data_get_double(d, keys::Stability));
	s.motionResponse = clamp01(obs_data_get_double(d, keys::MotionResponse));
	s.debugView = debugViewFromKey(obs_data_get_string(d, keys::DebugView));
	s.overlay = obs_data_get_bool(d, keys::Overlay);
	if (s.modelId.empty())
		s.modelId = "auto";
	if (s.backend.empty())
		s.backend = "auto";
	return s;
}

RefineParams FilterSettings::refineParams() const
{
	RefineParams p;
	p.threshold = separation;
	// smoothness 0 -> hard 0.02 band, 1 -> wide 0.45 band (keeps hair semi-transparency)
	p.softness = 0.02f + 0.43f * smoothness;
	p.shiftPixels = int(std::lround(shift * 4.0f));
	p.minComponentArea = removeBlobs ? 0.004f : 0.f;
	p.maxHoleArea = fillHoles ? 0.002f : 0.f;
	return p;
}

TemporalParams FilterSettings::temporalParams() const
{
	TemporalParams t;
	t.enabled = stability > 0.001f;
	t.stability = stability;
	t.motionResponse = motionResponse;
	return t;
}

float FilterSettings::featherPixels(uint32_t outputHeight) const
{
	// 0 -> 0 px, 1 -> ~1.2% of the frame height (13 px at 1080p)
	return feather * 0.012f * float(outputHeight ? outputHeight : 1080);
}

} // namespace promatte

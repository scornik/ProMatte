#pragma once
#include <cstdint>
#include <string>

#include <obs-module.h>

#include "performance/performance_controller.h"
#include "postprocessing/matte_refine.h"
#include "temporal/temporal_stabilizer.h"

namespace promatte {

enum class BackgroundMode { Transparent = 0, Color, Image, Video, Blur, Dim, Source };
enum class BackgroundFit { Cover = 0, Contain, Stretch };
enum class DebugView { None = 0, Matte, Edges, Confidence, Foreground, RawMatte };

// Property keys (also used by presets and tests).
namespace keys {
constexpr const char *Preset = "preset";
constexpr const char *Mode = "bg_mode";
constexpr const char *Color = "bg_color";
constexpr const char *ColorOpacity = "bg_color_opacity";
constexpr const char *ImagePath = "bg_image";
constexpr const char *VideoPath = "bg_video";
constexpr const char *SourceName = "bg_source";
constexpr const char *Fit = "bg_fit";
constexpr const char *BlurAmount = "blur_amount";
constexpr const char *BlurQuality = "blur_quality";
constexpr const char *DimBrightness = "dim_brightness";
constexpr const char *DimOpacity = "dim_opacity";
constexpr const char *Quality = "quality";
constexpr const char *Model = "model";
constexpr const char *Backend = "backend";
constexpr const char *CpuThreads = "cpu_threads";
constexpr const char *GpuIndex = "gpu_index";
constexpr const char *ManualTier = "manual_tier";
constexpr const char *MaxAiFps = "max_ai_fps";
constexpr const char *Separation = "edge_separation";
constexpr const char *Smoothness = "edge_smoothness";
constexpr const char *Feather = "edge_feather";
constexpr const char *Shift = "edge_shift";
constexpr const char *Decontaminate = "edge_decontaminate";
constexpr const char *HaloRemoval = "edge_halo";
constexpr const char *RemoveBlobs = "edge_remove_blobs";
constexpr const char *FillHoles = "edge_fill_holes";
constexpr const char *UpsampleQuality = "edge_upsample_quality";
constexpr const char *Stability = "temporal_stability";
constexpr const char *MotionResponse = "temporal_motion";
constexpr const char *DebugView = "debug_view";
constexpr const char *Overlay = "debug_overlay";
constexpr const char *ModelToManage = "mm_model";
constexpr const char *CustomModelPath = "mm_custom_path";
} // namespace keys

struct FilterSettings {
	std::string preset = "webcam";
	BackgroundMode mode = BackgroundMode::Transparent;
	uint32_t color = 0xFF00FF00; // OBS colour format 0xAABBGGRR
	float colorOpacity = 1.0f;
	std::string imagePath;
	std::string videoPath;
	std::string sourceName;
	BackgroundFit fit = BackgroundFit::Cover;
	float blurAmount = 0.5f; // 0..1
	int blurQuality = 1;     // 0 fast, 1 balanced, 2 high
	float dimBrightness = 0.35f;
	float dimOpacity = 1.0f;

	QualityMode quality = QualityMode::Auto;
	std::string modelId = "auto";
	std::string backend = "auto";
	int cpuThreads = 0;
	int gpuIndex = -1; // -1 = the adapter OBS renders on (or the strongest GPU)
	int manualTier = -1;
	int maxAiFps = 0;

	float separation = 0.5f;    // matte threshold
	float smoothness = 0.35f;   // 0..1 -> softness band
	float feather = 0.25f;      // 0..1 -> blur radius in px at output res
	float shift = 0.0f;         // -1..1 -> erode/dilate pixels
	float decontaminate = 0.5f; // 0..1
	float haloRemoval = 0.3f;   // 0..1
	bool removeBlobs = true;
	bool fillHoles = true;
	int upsampleQuality = 1; // 0 = 3x3 JBU, 1 = 5x5 JBU

	float stability = 0.6f;
	float motionResponse = 0.6f;

	DebugView debugView = DebugView::None;
	bool overlay = false;

	// Derived parameter sets for the core layers.
	RefineParams refineParams() const;
	TemporalParams temporalParams() const;
	float featherPixels(uint32_t outputHeight) const;
};

void settingsDefaults(obs_data_t *data);
FilterSettings settingsLoad(obs_data_t *data);

const char *backgroundModeKey(BackgroundMode m);
BackgroundMode backgroundModeFromKey(const char *k);
const char *debugViewKey(DebugView v);
DebugView debugViewFromKey(const char *k);
const char *fitKey(BackgroundFit f);
BackgroundFit fitFromKey(const char *k);

} // namespace promatte

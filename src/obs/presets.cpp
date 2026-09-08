#include "obs/presets.h"

#include "obs/settings.h"

namespace promatte {

const std::vector<PresetInfo> &presetList()
{
	static const std::vector<PresetInfo> presets = {
		{"webcam", "Preset.Webcam", "Preset.Webcam.Desc"},
		{"talking_head", "Preset.TalkingHead", "Preset.TalkingHead.Desc"},
		{"gaming", "Preset.Gaming", "Preset.Gaming.Desc"},
		{"high_quality", "Preset.HighQuality", "Preset.HighQuality.Desc"},
		{"low_end", "Preset.LowEnd", "Preset.LowEnd.Desc"},
		{"green_screen", "Preset.GreenScreen", "Preset.GreenScreen.Desc"},
		{"custom", "Preset.Custom", "Preset.Custom.Desc"},
	};
	return presets;
}

const char *presetDescriptionKey(const std::string &id)
{
	for (const auto &p : presetList())
		if (id == p.id)
			return p.descriptionKey;
	return "Preset.Custom.Desc";
}

bool applyPreset(const std::string &id, obs_data_t *d)
{
	using namespace keys;
	auto common = [&] {
		obs_data_set_string(d, Backend, "auto");
		obs_data_set_string(d, Model, "auto");
		obs_data_set_int(d, ManualTier, -1);
		obs_data_set_bool(d, RemoveBlobs, true);
		obs_data_set_bool(d, FillHoles, true);
	};
	if (id == "webcam") {
		common();
		obs_data_set_string(d, Quality, "auto");
		obs_data_set_double(d, Separation, 0.5);
		obs_data_set_double(d, Smoothness, 0.35);
		obs_data_set_double(d, Feather, 0.25);
		obs_data_set_double(d, Shift, 0.0);
		obs_data_set_double(d, Decontaminate, 0.5);
		obs_data_set_double(d, HaloRemoval, 0.3);
		obs_data_set_double(d, Stability, 0.6);
		obs_data_set_double(d, MotionResponse, 0.6);
		obs_data_set_int(d, UpsampleQuality, 1);
		obs_data_set_int(d, MaxAiFps, 0);
		return true;
	}
	if (id == "talking_head") {
		// Prioritise face/hair/shoulders: softer band, wider feather, strong stability.
		common();
		obs_data_set_string(d, Quality, "quality");
		obs_data_set_double(d, Separation, 0.45);
		obs_data_set_double(d, Smoothness, 0.5);
		obs_data_set_double(d, Feather, 0.35);
		obs_data_set_double(d, Shift, 0.1);
		obs_data_set_double(d, Decontaminate, 0.6);
		obs_data_set_double(d, HaloRemoval, 0.35);
		obs_data_set_double(d, Stability, 0.75);
		obs_data_set_double(d, MotionResponse, 0.5);
		obs_data_set_int(d, UpsampleQuality, 1);
		obs_data_set_int(d, MaxAiFps, 0);
		return true;
	}
	if (id == "gaming") {
		// Low latency: performance tier, minimal temporal lag, cheap upsampling.
		common();
		obs_data_set_string(d, Quality, "performance");
		obs_data_set_double(d, Separation, 0.5);
		obs_data_set_double(d, Smoothness, 0.25);
		obs_data_set_double(d, Feather, 0.2);
		obs_data_set_double(d, Shift, 0.0);
		obs_data_set_double(d, Decontaminate, 0.3);
		obs_data_set_double(d, HaloRemoval, 0.2);
		obs_data_set_double(d, Stability, 0.4);
		obs_data_set_double(d, MotionResponse, 0.9);
		obs_data_set_int(d, UpsampleQuality, 0);
		obs_data_set_int(d, MaxAiFps, 0);
		return true;
	}
	if (id == "high_quality") {
		common();
		obs_data_set_string(d, Quality, "ultra");
		obs_data_set_double(d, Separation, 0.5);
		obs_data_set_double(d, Smoothness, 0.45);
		obs_data_set_double(d, Feather, 0.25);
		obs_data_set_double(d, Shift, 0.0);
		obs_data_set_double(d, Decontaminate, 0.7);
		obs_data_set_double(d, HaloRemoval, 0.4);
		obs_data_set_double(d, Stability, 0.65);
		obs_data_set_double(d, MotionResponse, 0.6);
		obs_data_set_int(d, UpsampleQuality, 1);
		obs_data_set_int(d, MaxAiFps, 0);
		return true;
	}
	if (id == "low_end") {
		// Minimum resource use: performance tier, 15 fps AI, 3x3 upsampling.
		common();
		obs_data_set_string(d, Quality, "performance");
		obs_data_set_double(d, Separation, 0.5);
		obs_data_set_double(d, Smoothness, 0.3);
		obs_data_set_double(d, Feather, 0.3);
		obs_data_set_double(d, Shift, 0.0);
		obs_data_set_double(d, Decontaminate, 0.0);
		obs_data_set_double(d, HaloRemoval, 0.0);
		obs_data_set_double(d, Stability, 0.7);
		obs_data_set_double(d, MotionResponse, 0.6);
		obs_data_set_int(d, UpsampleQuality, 0);
		obs_data_set_int(d, MaxAiFps, 15);
		return true;
	}
	if (id == "green_screen") {
		// Virtual background: crisp edges, slight erosion, strong decontamination.
		common();
		obs_data_set_string(d, Quality, "auto");
		obs_data_set_double(d, Separation, 0.55);
		obs_data_set_double(d, Smoothness, 0.3);
		obs_data_set_double(d, Feather, 0.2);
		obs_data_set_double(d, Shift, -0.15);
		obs_data_set_double(d, Decontaminate, 0.8);
		obs_data_set_double(d, HaloRemoval, 0.5);
		obs_data_set_double(d, Stability, 0.6);
		obs_data_set_double(d, MotionResponse, 0.6);
		obs_data_set_int(d, UpsampleQuality, 1);
		obs_data_set_int(d, MaxAiFps, 0);
		if (obs_data_get_string(d, Mode) == std::string("transparent"))
			obs_data_set_string(d, Mode, "image");
		return true;
	}
	return false;
}

} // namespace promatte

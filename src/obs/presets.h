#pragma once
#include <obs-module.h>

#include <string>
#include <vector>

namespace promatte {

struct PresetInfo {
	const char *id;
	const char *localeKey;
	// One line saying what this preset actually changes. Presets only tune matte
	// quality and cost; none of them crop, zoom or change framing, and saying so
	// in the panel stops "Talking Head" from reading like a framing option.
	const char *descriptionKey;
};

const std::vector<PresetInfo> &presetList();

// Locale key for the description of `id`, or the generic line for an unknown id.
const char *presetDescriptionKey(const std::string &id);

// Writes the preset's values into `settings`. Returns false for unknown ids
// (including "custom", which leaves everything untouched).
bool applyPreset(const std::string &id, obs_data_t *settings);

} // namespace promatte

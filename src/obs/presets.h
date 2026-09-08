#pragma once
#include <obs-module.h>

#include <string>
#include <vector>

namespace promatte {

struct PresetInfo {
	const char *id;
	const char *localeKey;
};

const std::vector<PresetInfo> &presetList();

// Writes the preset's values into `settings`. Returns false for unknown ids
// (including "custom", which leaves everything untouched).
bool applyPreset(const std::string &id, obs_data_t *settings);

} // namespace promatte

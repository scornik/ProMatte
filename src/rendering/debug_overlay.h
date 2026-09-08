#pragma once
#include <string>

#include <obs-module.h>

namespace promatte {

// Developer overlay: renders a small stats block on top of the video using a
// private OBS text source (text_gdiplus on Windows, text_ft2_source elsewhere).
class DebugOverlay {
public:
	~DebugOverlay();
	void setText(const std::string &text);
	// Graphics thread: draws the overlay at the top-left of a w x h frame.
	void render(uint32_t w, uint32_t h);
	void release();

private:
	void ensureSource();
	obs_source_t *text_ = nullptr;
	std::string current_;
	bool failed_ = false;
};

} // namespace promatte

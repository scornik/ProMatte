#pragma once
#include <cstdint>
#include <string>

#include <obs-module.h>
#include <graphics/image-file.h>

#include "obs/settings.h"

namespace promatte {

// Provides the replacement background texture for Image / Video / Source modes.
//   Image  : OBS image-file API (animated GIF supported)
//   Video  : private ffmpeg_source (looping, muted) rendered into a texrender
//   Source : any other OBS source (by name) rendered into a texrender
// All methods run on the graphics thread except update(), which only records
// the desired configuration; the actual (re)load happens lazily in texture().
class BackgroundSource {
public:
	BackgroundSource() = default;
	~BackgroundSource();
	BackgroundSource(const BackgroundSource &) = delete;
	BackgroundSource &operator=(const BackgroundSource &) = delete;

	void update(const FilterSettings &s, obs_source_t *owner);
	void tick(float seconds);
	// Returns the texture for this frame (may be null) and its size.
	gs_texture_t *texture(uint32_t &w, uint32_t &h);
	void release(); // graphics thread

private:
	void loadImage();
	void loadVideo();
	void resolveSource();
	void releaseImage();
	void releaseVideo();
	void releaseSource();

	BackgroundMode mode_ = BackgroundMode::Transparent;
	std::string imagePath_, loadedImagePath_;
	std::string videoPath_, loadedVideoPath_;
	std::string sourceName_, resolvedSourceName_;
	obs_source_t *owner_ = nullptr;

	// OBS 33 removed the versioned image-file API and changed its layout.
#if LIBOBS_API_MAJOR_VER >= 33
	gs_image_file_ex_t image_{};
#else
	gs_image_file4_t image_{};
#endif
	bool imageLoaded_ = false;
	uint64_t lastTickNs_ = 0;

	obs_source_t *video_ = nullptr;
	obs_weak_source_t *source_ = nullptr;
	gs_texrender_t *render_ = nullptr;
	bool dirty_ = true;
};

} // namespace promatte

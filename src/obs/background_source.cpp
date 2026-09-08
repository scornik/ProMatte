#include "obs/background_source.h"

#include <util/platform.h>

#include "utils/logging.h"

namespace promatte {

BackgroundSource::~BackgroundSource()
{
	// release() must have been called on the graphics thread; guard anyway.
	releaseVideo();
	releaseSource();
}

void BackgroundSource::update(const FilterSettings &s, obs_source_t *owner)
{
	owner_ = owner;
	if (s.mode != mode_ || s.imagePath != imagePath_ || s.videoPath != videoPath_ || s.sourceName != sourceName_)
		dirty_ = true;
	mode_ = s.mode;
	imagePath_ = s.imagePath;
	videoPath_ = s.videoPath;
	sourceName_ = s.sourceName;
}

void BackgroundSource::tick(float)
{
	if (mode_ == BackgroundMode::Image && imageLoaded_) {
		uint64_t now = os_gettime_ns();
		if (lastTickNs_ && image_.image3.image2.image.is_animated_gif) {
			if (gs_image_file4_tick(&image_, now - lastTickNs_)) {
				obs_enter_graphics();
				gs_image_file4_update_texture(&image_);
				obs_leave_graphics();
			}
		}
		lastTickNs_ = now;
	}
}

void BackgroundSource::loadImage()
{
	releaseImage();
	if (imagePath_.empty())
		return;
	gs_image_file4_init(&image_, imagePath_.c_str(), GS_IMAGE_ALPHA_PREMULTIPLY_SRGB);
	gs_image_file4_init_texture(&image_);
	imageLoaded_ = image_.image3.image2.image.loaded;
	loadedImagePath_ = imagePath_;
	if (!imageLoaded_)
		PM_LOG_WARN("could not load background image '%s'", imagePath_.c_str());
	else
		PM_LOG_INFO("background image loaded: %s (%ux%u)", imagePath_.c_str(), image_.image3.image2.image.cx,
			    image_.image3.image2.image.cy);
}

void BackgroundSource::releaseImage()
{
	if (imageLoaded_ || !loadedImagePath_.empty()) {
		gs_image_file4_free(&image_);
		image_ = gs_image_file4_t{};
	}
	imageLoaded_ = false;
	loadedImagePath_.clear();
}

void BackgroundSource::loadVideo()
{
	releaseVideo();
	if (videoPath_.empty())
		return;
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "local_file", videoPath_.c_str());
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_bool(settings, "looping", true);
	obs_data_set_bool(settings, "restart_on_activate", false);
	obs_data_set_bool(settings, "clear_on_media_end", false);
	obs_data_set_bool(settings, "hw_decode", true);
	obs_data_set_bool(settings, "close_when_inactive", false);
	video_ = obs_source_create_private("ffmpeg_source", "promatte-background-video", settings);
	obs_data_release(settings);
	loadedVideoPath_ = videoPath_;
	if (!video_) {
		PM_LOG_WARN("could not create media source for '%s'", videoPath_.c_str());
		return;
	}
	obs_source_set_muted(video_, true);
	obs_source_set_monitoring_type(video_, OBS_MONITORING_TYPE_NONE);
	obs_source_inc_active(video_);
	obs_source_inc_showing(video_);
	PM_LOG_INFO("background video source created: %s", videoPath_.c_str());
}

void BackgroundSource::releaseVideo()
{
	if (video_) {
		obs_source_dec_showing(video_);
		obs_source_dec_active(video_);
		obs_source_release(video_);
		video_ = nullptr;
	}
	loadedVideoPath_.clear();
}

void BackgroundSource::resolveSource()
{
	releaseSource();
	resolvedSourceName_ = sourceName_;
	if (sourceName_.empty())
		return;
	obs_source_t *src = obs_get_source_by_name(sourceName_.c_str());
	if (!src) {
		PM_LOG_WARN("background source '%s' not found", sourceName_.c_str());
		return;
	}
	// Never render our own parent (would recurse).
	obs_source_t *parent = owner_ ? obs_filter_get_parent(owner_) : nullptr;
	if (parent == src) {
		PM_LOG_WARN("background source cannot be the filtered source itself");
		obs_source_release(src);
		return;
	}
	source_ = obs_source_get_weak_source(src);
	obs_source_inc_showing(src);
	obs_source_release(src);
}

void BackgroundSource::releaseSource()
{
	if (source_) {
		obs_source_t *src = obs_weak_source_get_source(source_);
		if (src) {
			obs_source_dec_showing(src);
			obs_source_release(src);
		}
		obs_weak_source_release(source_);
		source_ = nullptr;
	}
	resolvedSourceName_.clear();
}

gs_texture_t *BackgroundSource::texture(uint32_t &w, uint32_t &h)
{
	w = h = 0;
	if (dirty_) {
		dirty_ = false;
		if (mode_ != BackgroundMode::Image || imagePath_ != loadedImagePath_)
			releaseImage();
		if (mode_ != BackgroundMode::Video || videoPath_ != loadedVideoPath_)
			releaseVideo();
		if (mode_ != BackgroundMode::Source || sourceName_ != resolvedSourceName_)
			releaseSource();
		if (mode_ == BackgroundMode::Image && !imageLoaded_)
			loadImage();
		if (mode_ == BackgroundMode::Video && !video_)
			loadVideo();
		if (mode_ == BackgroundMode::Source && !source_)
			resolveSource();
	}
	if (mode_ == BackgroundMode::Image) {
		if (!imageLoaded_ || !image_.image3.image2.image.texture)
			return nullptr;
		w = image_.image3.image2.image.cx;
		h = image_.image3.image2.image.cy;
		return image_.image3.image2.image.texture;
	}
	obs_source_t *src = nullptr;
	if (mode_ == BackgroundMode::Video)
		src = video_ ? obs_source_get_ref(video_) : nullptr;
	else if (mode_ == BackgroundMode::Source)
		src = source_ ? obs_weak_source_get_source(source_) : nullptr;
	if (!src)
		return nullptr;
	uint32_t sw = obs_source_get_width(src), sh = obs_source_get_height(src);
	if (sw == 0 || sh == 0) {
		obs_source_release(src);
		return nullptr;
	}
	if (!render_)
		render_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_texrender_reset(render_);
	gs_texture_t *tex = nullptr;
	if (gs_texrender_begin(render_, sw, sh)) {
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, float(sw), 0.0f, float(sh), -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		obs_source_video_render(src);
		gs_blend_state_pop();
		gs_texrender_end(render_);
		tex = gs_texrender_get_texture(render_);
	}
	obs_source_release(src);
	if (tex) {
		w = sw;
		h = sh;
	}
	return tex;
}

void BackgroundSource::release()
{
	releaseImage();
	releaseVideo();
	releaseSource();
	if (render_) {
		gs_texrender_destroy(render_);
		render_ = nullptr;
	}
	dirty_ = true;
}

} // namespace promatte

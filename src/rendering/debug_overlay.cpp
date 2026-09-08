#include "rendering/debug_overlay.h"

#include "utils/logging.h"

namespace promatte {

DebugOverlay::~DebugOverlay()
{
	release();
}

void DebugOverlay::ensureSource()
{
	if (text_ || failed_)
		return;
	obs_data_t *settings = obs_data_create();
	obs_data_t *font = obs_data_create();
	obs_data_set_string(font, "face", "Consolas");
	obs_data_set_int(font, "size", 18);
	obs_data_set_int(font, "flags", 0);
	obs_data_set_obj(settings, "font", font);
	obs_data_release(font);
	obs_data_set_string(settings, "text", current_.c_str());
	obs_data_set_bool(settings, "outline", true);
	obs_data_set_int(settings, "outline_size", 2);
	obs_data_set_int(settings, "outline_color", 0xFF000000);
	obs_data_set_int(settings, "color", 0xFF80FF80);
	obs_data_set_int(settings, "color1", 0xFF80FF80);
	obs_data_set_int(settings, "color2", 0xFF80FF80);
	obs_data_set_bool(settings, "bk_opacity", true);
	obs_data_set_int(settings, "bk_color", 0xFF000000);
	obs_data_set_int(settings, "bk_opacity", 60);
	const char *ids[] = {"text_gdiplus_v3", "text_gdiplus_v2", "text_gdiplus", "text_ft2_source_v2", "text_ft2_source"};
	for (const char *id : ids) {
		text_ = obs_source_create_private(id, "promatte-overlay", settings);
		if (text_)
			break;
	}
	obs_data_release(settings);
	if (!text_) {
		failed_ = true;
		PM_LOG_WARN("developer overlay unavailable (no text source)");
	}
}

void DebugOverlay::setText(const std::string &text)
{
	if (text == current_)
		return;
	current_ = text;
	if (!text_)
		return;
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "text", current_.c_str());
	obs_source_update(text_, settings);
	obs_data_release(settings);
}

void DebugOverlay::render(uint32_t, uint32_t)
{
	ensureSource();
	if (!text_)
		return;
	gs_matrix_push();
	gs_matrix_translate3f(12.0f, 12.0f, 0.0f);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
	obs_source_video_render(text_);
	gs_blend_state_pop();
	gs_matrix_pop();
}

void DebugOverlay::release()
{
	if (text_) {
		obs_source_release(text_);
		text_ = nullptr;
	}
}

} // namespace promatte

// Headless libobs integration harness.
//
// Boots libobs with the D3D11 renderer (no UI), loads the staged ProMatte module,
// creates a synthetic colour source with the ProMatte filter attached and drives
// the render loop while exercising: filter creation/destruction, enable/disable,
// settings updates (every background mode), resolution changes, backend switching,
// and a bounded memory check over many frames.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <thread>

#include <obs.h>
#include <obs-module.h>
#include <graphics/image-file.h>

#include "utils/file_utils.h"
#include "utils/system_info.h"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace promatte;

namespace {

void registerTestSource();

struct ObsRuntime {
	bool ok = false;
	std::string error;
	obs_module_t *module = nullptr;

	bool start(uint32_t w, uint32_t h)
	{
#ifdef _WIN32
		// libobs looks for libobs-d3d11.dll and the plugin next to obs.dll; add the
		// SDK/build dirs to the DLL search path.
		std::wstring sdkBin = fs::toWide(std::string(PROMATTE_OBS_SDK_DIR) + "/bin/64bit");
		SetDllDirectoryW(sdkBin.c_str());
		AddDllDirectory(sdkBin.c_str());
		std::wstring d3d = fs::toWide(std::string(PROMATTE_OBS_BUILD_DIR) + "/libobs-d3d11/Release");
		AddDllDirectory(d3d.c_str());
		std::wstring stage = fs::toWide(std::string(PROMATTE_STAGE_DIR) + "/obs-plugins/64bit");
		AddDllDirectory(stage.c_str());
		SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
#endif
		if (!obs_startup("en-US", nullptr, nullptr)) {
			error = "obs_startup failed";
			return false;
		}
		// libobs' own effect files (default.effect etc.) live in the source tree.
		std::string libobsData = std::string(PROMATTE_OBS_SDK_DIR) + "/../obs-studio-31.1.1/libobs/data/";
		obs_add_data_path(libobsData.c_str());
		registerTestSource();
		obs_video_info ovi{};
		ovi.adapter = 0;
		ovi.graphics_module = "libobs-d3d11";
		ovi.fps_num = 30;
		ovi.fps_den = 1;
		ovi.base_width = w;
		ovi.base_height = h;
		ovi.output_width = w;
		ovi.output_height = h;
		ovi.output_format = VIDEO_FORMAT_NV12;
		ovi.colorspace = VIDEO_CS_709;
		ovi.range = VIDEO_RANGE_PARTIAL;
		ovi.gpu_conversion = true;
		ovi.scale_type = OBS_SCALE_BICUBIC;
		int r = obs_reset_video(&ovi);
		if (r != OBS_VIDEO_SUCCESS) {
			error = "obs_reset_video failed: " + std::to_string(r);
			return false;
		}
		// Load the staged plugin module explicitly.
		std::string bin = std::string(PROMATTE_STAGE_DIR) + "/obs-plugins/64bit/promatte.dll";
		std::string data = std::string(PROMATTE_STAGE_DIR) + "/data/obs-plugins/promatte";
		int mr = obs_open_module(&module, bin.c_str(), data.c_str());
		if (mr != MODULE_SUCCESS) {
			error = "obs_open_module failed: " + std::to_string(mr) + " (" + bin + ")";
			return false;
		}
		if (!obs_init_module(module)) {
			error = "obs_init_module failed";
			return false;
		}
		obs_post_load_modules();
		ok = true;
		return true;
	}
	~ObsRuntime()
	{
		if (ok)
			obs_shutdown();
	}
};

ObsRuntime *g_obs = nullptr;

// Synthetic video source: gradient background with a moving "person" ellipse
// (drawn with libobs' solid effect). libobs itself ships no video sources, so
// the harness registers its own instead of depending on other OBS plugins.
struct TestSource {
	uint32_t w = 1280, h = 720;
	float t = 0.f;
	// A real portrait makes the segmentation model produce a real matte, which the
	// matte-integrity test needs; without it the source is drawn procedurally.
	gs_image_file_t image{};
	bool imageLoaded = false;
};

void *testSourceCreate(obs_data_t *settings, obs_source_t *)
{
	auto *s = new TestSource();
	s->w = uint32_t(obs_data_get_int(settings, "width"));
	s->h = uint32_t(obs_data_get_int(settings, "height"));
	if (!s->w || !s->h) {
		s->w = 1280;
		s->h = 720;
	}
	const std::string portrait = std::string(PROMATTE_SOURCE_DIR) + "/tests/visual/assets/portrait_obama.jpg";
	if (fs::exists(portrait)) {
		obs_enter_graphics();
		gs_image_file_init(&s->image, portrait.c_str());
		gs_image_file_init_texture(&s->image);
		obs_leave_graphics();
		s->imageLoaded = s->image.loaded;
	}
	return s;
}
void testSourceDestroy(void *d)
{
	auto *s = static_cast<TestSource *>(d);
	if (s->imageLoaded) {
		obs_enter_graphics();
		gs_image_file_free(&s->image);
		obs_leave_graphics();
	}
	delete s;
}
void testSourceUpdate(void *d, obs_data_t *settings)
{
	auto *s = static_cast<TestSource *>(d);
	uint32_t w = uint32_t(obs_data_get_int(settings, "width")), h = uint32_t(obs_data_get_int(settings, "height"));
	if (w && h) {
		s->w = w;
		s->h = h;
	}
}
void testSourceTick(void *d, float seconds)
{
	static_cast<TestSource *>(d)->t += seconds;
}
uint32_t testSourceWidth(void *d)
{
	return static_cast<TestSource *>(d)->w;
}
uint32_t testSourceHeight(void *d)
{
	return static_cast<TestSource *>(d)->h;
}
void testSourceRender(void *d, gs_effect_t *)
{
	auto *s = static_cast<TestSource *>(d);
	if (s->imageLoaded && s->image.texture) {
		gs_effect_t *def = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_eparam_t *img = gs_effect_get_param_by_name(def, "image");
		gs_effect_set_texture(img, s->image.texture);
		gs_matrix_push();
		gs_matrix_scale3f(float(s->w) / float(s->image.cx), float(s->h) / float(s->image.cy), 1.0f);
		while (gs_effect_loop(def, "Draw"))
			gs_draw_sprite(s->image.texture, 0, s->image.cx, s->image.cy);
		gs_matrix_pop();
		return;
	}
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
	struct vec4 c;
	// background: two bands
	vec4_set(&c, 0.55f, 0.6f, 0.7f, 1.f);
	gs_effect_set_vec4(color, &c);
	while (gs_effect_loop(solid, "Solid"))
		gs_draw_sprite(nullptr, 0, s->w, s->h);
	vec4_set(&c, 0.35f, 0.4f, 0.5f, 1.f);
	gs_effect_set_vec4(color, &c);
	gs_matrix_push();
	gs_matrix_translate3f(0.f, float(s->h) * 0.6f, 0.f);
	while (gs_effect_loop(solid, "Solid"))
		gs_draw_sprite(nullptr, 0, s->w, s->h * 2 / 5);
	gs_matrix_pop();
	// "person": a moving rectangle block (skin-ish colour)
	vec4_set(&c, 0.85f, 0.65f, 0.55f, 1.f);
	gs_effect_set_vec4(color, &c);
	gs_matrix_push();
	float x = float(s->w) * (0.4f + 0.1f * std::sin(s->t));
	gs_matrix_translate3f(x, float(s->h) * 0.25f, 0.f);
	while (gs_effect_loop(solid, "Solid"))
		gs_draw_sprite(nullptr, 0, s->w / 5, s->h * 3 / 4);
	gs_matrix_pop();
}

void registerTestSource()
{
	static obs_source_info info = {};
	info.id = "promatte_test_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.get_name = [](void *) -> const char * { return "ProMatte Test Source"; };
	info.create = testSourceCreate;
	info.destroy = testSourceDestroy;
	info.update = testSourceUpdate;
	info.video_tick = testSourceTick;
	info.video_render = testSourceRender;
	info.get_width = testSourceWidth;
	info.get_height = testSourceHeight;
	obs_register_source(&info);
}

void pump(int frames, int msPerFrame = 33)
{
	// libobs renders on its own video thread; we just wait.
	std::this_thread::sleep_for(std::chrono::milliseconds(frames * msPerFrame));
}

struct Scene {
	obs_source_t *color = nullptr;
	obs_source_t *filter = nullptr;
	obs_scene_t *scene = nullptr;

	void create(uint32_t w, uint32_t h, const char *filterSettingsJson = nullptr)
	{
		obs_data_t *cs = obs_data_create();
		obs_data_set_int(cs, "width", w);
		obs_data_set_int(cs, "height", h);
		color = obs_source_create("promatte_test_source", "test-source", cs, nullptr);
		obs_data_release(cs);
		REQUIRE(color != nullptr);
		obs_data_t *fs = filterSettingsJson ? obs_data_create_from_json(filterSettingsJson) : obs_data_create();
		filter = obs_source_create("promatte_filter", "promatte", fs, nullptr);
		obs_data_release(fs);
		REQUIRE(filter != nullptr);
		obs_source_filter_add(color, filter);
		scene = obs_scene_create("test-scene");
		obs_scene_add(scene, color);
		obs_set_output_source(0, obs_scene_get_source(scene));
	}
	void setSize(uint32_t w, uint32_t h)
	{
		obs_data_t *cs = obs_data_create();
		obs_data_set_int(cs, "width", w);
		obs_data_set_int(cs, "height", h);
		obs_source_update(color, cs);
		obs_data_release(cs);
	}
	void update(const char *json)
	{
		obs_data_t *d = obs_data_create_from_json(json);
		obs_source_update(filter, d);
		obs_data_release(d);
	}
	void destroy()
	{
		obs_set_output_source(0, nullptr);
		if (filter) {
			obs_source_filter_remove(color, filter);
			obs_source_release(filter);
			filter = nullptr;
		}
		if (scene) {
			obs_scene_release(scene);
			scene = nullptr;
		}
		if (color) {
			obs_source_release(color);
			color = nullptr;
		}
	}
};

} // namespace

TEST_CASE("libobs boots and the ProMatte module loads")
{
	REQUIRE(g_obs);
	REQUIRE_MESSAGE(g_obs->ok, g_obs->error);
	// The filter type must be registered.
	const char *id = nullptr;
	bool found = false;
	for (size_t i = 0; obs_enum_filter_types(i, &id); ++i)
		if (std::string(id) == "promatte_filter")
			found = true;
	CHECK(found);
}

TEST_CASE("filter create / render / destroy on a colour source")
{
	Scene s;
	s.create(1280, 720);
	pump(45);
	CHECK(obs_source_get_width(s.filter) == 1280);
	CHECK(obs_source_get_height(s.filter) == 720);
	s.destroy();
	pump(5);
}

TEST_CASE("every background mode renders without errors")
{
	Scene s;
	s.create(1280, 720, R"({"bg_mode":"transparent","backend":"cpu","quality":"performance"})");
	pump(60);
	const char *modes[] = {"blur", "color", "dim", "image", "video", "source", "transparent"};
	for (const char *m : modes) {
		std::string json = std::string(R"({"bg_mode":")") + m + R"("})";
		s.update(json.c_str());
		pump(20);
	}
	// debug views
	const char *views[] = {"matte", "edges", "confidence", "foreground", "none"};
	for (const char *v : views) {
		std::string json = std::string(R"({"debug_view":")") + v + R"("})";
		s.update(json.c_str());
		pump(10);
	}
	s.destroy();
}

TEST_CASE("resolution changes and enable/disable toggles")
{
	Scene s;
	s.create(1280, 720, R"({"backend":"cpu","quality":"performance"})");
	pump(40);
	s.setSize(640, 360);
	pump(30);
	CHECK(obs_source_get_width(s.filter) == 640);
	s.setSize(1920, 1080);
	pump(30);
	CHECK(obs_source_get_width(s.filter) == 1920);
	for (int i = 0; i < 5; ++i) {
		obs_source_set_enabled(s.filter, false);
		pump(15);
		obs_source_set_enabled(s.filter, true);
		pump(15);
	}
	s.destroy();
}

TEST_CASE("backend and quality switching while rendering")
{
	Scene s;
	s.create(1280, 720, R"({"backend":"cpu","quality":"auto"})");
	pump(40);
	s.update(R"({"backend":"auto","quality":"balanced"})");
	pump(60);
	s.update(R"({"backend":"directml","quality":"quality"})");
	pump(60);
	s.update(R"({"backend":"cpu","quality":"performance","model":"mediapipe_selfie_landscape"})");
	pump(60);
	s.update(R"({"model":"does_not_exist"})"); // falls back to auto selection
	pump(30);
	s.destroy();
}

TEST_CASE("rapid create/destroy does not leak threads or crash")
{
	for (int i = 0; i < 8; ++i) {
		Scene s;
		s.create(640, 360, R"({"backend":"cpu","quality":"performance"})");
		pump(i % 3 == 0 ? 2 : 12); // sometimes destroy while the model is still loading
		s.destroy();
	}
	pump(10);
}

namespace {
// Low percentile of libobs render times (ms) after a settle period. The low
// percentile is deliberate: this box shares one GPU between the compositor, the
// desktop and the inference backend, so the upper tail measures system noise
// rather than the filter's own cost.
double measureRenderMs(int settleFrames = 90, int samples = 40)
{
	pump(settleFrames);
	std::vector<double> v;
	for (int i = 0; i < samples; ++i) {
		pump(4);
		double ms = double(obs_get_average_frame_time_ns()) / 1e6;
		if (ms > 0)
			v.push_back(ms);
	}
	if (v.empty())
		return 0;
	std::sort(v.begin(), v.end());
	return v[v.size() / 5]; // 20th percentile
}
} // namespace

namespace {
// Renders one source (with its filters) into an off-screen target and reads the
// pixels back, so tests can inspect what the filter actually produces.
bool captureSource(obs_source_t *src, uint32_t w, uint32_t h, std::vector<uint8_t> &bgra)
{
	bool ok = false;
	obs_enter_graphics();
	gs_texrender_t *tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *stage = gs_stagesurface_create(w, h, GS_RGBA);
	if (tr && stage && gs_texrender_begin(tr, w, h)) {
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, float(w), 0.0f, float(h), -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		obs_source_video_render(src);
		gs_blend_state_pop();
		gs_texrender_end(tr);
		gs_stage_texture(stage, gs_texrender_get_texture(tr));
		uint8_t *data = nullptr;
		uint32_t linesize = 0;
		if (gs_stagesurface_map(stage, &data, &linesize)) {
			bgra.resize(size_t(w) * h * 4);
			for (uint32_t y = 0; y < h; ++y)
				std::memcpy(bgra.data() + size_t(y) * w * 4, data + size_t(y) * linesize, size_t(w) * 4);
			gs_stagesurface_unmap(stage);
			ok = true;
		}
	}
	if (stage)
		gs_stagesurface_destroy(stage);
	if (tr)
		gs_texrender_destroy(tr);
	obs_leave_graphics();
	return ok;
}

// Composites one source over a solid background using the same blend state the
// OBS scene compositor uses, so tests see what a viewer would actually see
// rather than the raw RGBA the filter writes.
bool captureOverBackground(obs_source_t *src, uint32_t w, uint32_t h, float r, float g, float b,
			   std::vector<uint8_t> &bgra)
{
	bool ok = false;
	obs_enter_graphics();
	gs_texrender_t *tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *stage = gs_stagesurface_create(w, h, GS_RGBA);
	if (tr && stage && gs_texrender_begin(tr, w, h)) {
		struct vec4 clear;
		vec4_set(&clear, r, g, b, 1.0f);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, float(w), 0.0f, float(h), -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE,
					   GS_BLEND_INVSRCALPHA);
		obs_source_video_render(src);
		gs_blend_state_pop();
		gs_texrender_end(tr);
		gs_stage_texture(stage, gs_texrender_get_texture(tr));
		uint8_t *data = nullptr;
		uint32_t linesize = 0;
		if (gs_stagesurface_map(stage, &data, &linesize)) {
			bgra.resize(size_t(w) * h * 4);
			for (uint32_t y = 0; y < h; ++y)
				std::memcpy(bgra.data() + size_t(y) * w * 4, data + size_t(y) * linesize, size_t(w) * 4);
			gs_stagesurface_unmap(stage);
			ok = true;
		}
	}
	if (stage)
		gs_stagesurface_destroy(stage);
	if (tr)
		gs_texrender_destroy(tr);
	obs_leave_graphics();
	return ok;
}

std::vector<double> luma(const std::vector<uint8_t> &rgba)
{
	std::vector<double> out(rgba.size() / 4);
	for (size_t i = 0; i < out.size(); ++i)
		out[i] = 0.299 * rgba[i * 4] + 0.587 * rgba[i * 4 + 1] + 0.114 * rgba[i * 4 + 2];
	return out;
}

double stddev(const std::vector<double> &v)
{
	if (v.empty())
		return 0;
	double m = 0;
	for (double x : v)
		m += x;
	m /= double(v.size());
	double s = 0;
	for (double x : v)
		s += (x - m) * (x - m);
	return std::sqrt(s / double(v.size()));
}

double correlation(const std::vector<double> &a, const std::vector<double> &b)
{
	if (a.size() != b.size() || a.empty())
		return 0;
	double ma = 0, mb = 0;
	for (size_t i = 0; i < a.size(); ++i) {
		ma += a[i];
		mb += b[i];
	}
	ma /= double(a.size());
	mb /= double(b.size());
	double num = 0, da = 0, db = 0;
	for (size_t i = 0; i < a.size(); ++i) {
		double x = a[i] - ma, y = b[i] - mb;
		num += x * y;
		da += x * x;
		db += y * y;
	}
	if (da <= 1e-9 || db <= 1e-9)
		return 0;
	return num / std::sqrt(da * db);
}
} // namespace

// Regression test for a real bug: libobs discards every effect parameter when an
// effect loop ends, so a refinement pass that consumed the upsampler's parameters
// left the joint bilateral upsample reading zeroed uniforms. The visible symptom
// was a "matte" that tracked the source image instead of the model output.
TEST_CASE("the refined matte follows the model matte, not the source image")
{
	const uint32_t w = 640, h = 360;
	Scene s;
	s.create(w, h, R"({"backend":"cpu","quality":"performance","bg_mode":"transparent","edge_feather":0.4})");
	pump(120); // let the model load and produce a matte

	std::vector<uint8_t> rawPix, refinedPix, srcPix;
	obs_source_set_enabled(s.filter, false);
	pump(20);
	REQUIRE(captureSource(s.color, w, h, srcPix));
	obs_source_set_enabled(s.filter, true);
	pump(30);

	// The filter passes video through until the model has loaded and produced its
	// first matte; wait for the debug views to stop being the source image.
	auto captureView = [&](const char *view, std::vector<uint8_t> &out) {
		s.update((std::string(R"({"debug_view":")") + view + R"("})").c_str());
		for (int attempt = 0; attempt < 25; ++attempt) {
			pump(20);
			REQUIRE(captureSource(s.color, w, h, out));
			if (correlation(luma(out), luma(srcPix)) < 0.98)
				return true;
		}
		return false;
	};
	REQUIRE_MESSAGE(captureView("raw_matte", rawPix), "the filter never produced a matte");
	REQUIRE(captureView("matte", refinedPix));

	const auto raw = luma(rawPix), refined = luma(refinedPix), source = luma(srcPix);
	const double rawSd = stddev(raw), refinedSd = stddev(refined), srcSd = stddev(source);
	const double corrRaw = correlation(refined, raw), corrSrc = correlation(refined, source);
	std::printf("matte check: std raw %.1f refined %.1f source %.1f | corr(refined,raw) %.3f "
		    "corr(refined,source) %.3f\n",
		    rawSd, refinedSd, srcSd, corrRaw, corrSrc);
	if (rawSd < 2.0) {
		// The model found nothing in the synthetic frame: the refined matte must
		// be just as flat. (With the bug it reproduced the source image instead.)
		CHECK(refinedSd < 12.0);
	} else {
		CHECK(corrRaw > 0.75);
		CHECK(corrRaw > corrSrc);
	}
	s.destroy();
}

TEST_CASE("transparent output is composited by OBS, not painted over the background")
{
	const uint32_t w = 640, h = 360;
	Scene s;
	s.create(w, h, R"({"backend":"cpu","quality":"performance","bg_mode":"transparent"})");
	pump(120); // let the model load and produce a matte

	// How much of the frame the filter actually marks transparent.
	std::vector<uint8_t> rawRgba;
	double transparentFraction = 0;
	for (int attempt = 0; attempt < 25; ++attempt) {
		pump(20);
		REQUIRE(captureSource(s.color, w, h, rawRgba));
		size_t clear = 0;
		for (size_t i = 3; i < rawRgba.size(); i += 4)
			clear += rawRgba[i] < 32 ? 1 : 0;
		transparentFraction = double(clear) / double(rawRgba.size() / 4);
		if (transparentFraction > 0.05)
			break;
	}
	std::printf("compositing check: %.1f%% of the frame is transparent\n", transparentFraction * 100.0);
	REQUIRE_MESSAGE(transparentFraction > 0.05,
			"the filter produced no transparent region, so compositing cannot be tested");

	// Composited over two different backgrounds the results must differ wherever
	// the matte is transparent. If the filter overrode the caller's blend state
	// the two captures would come out identical, because the background pixels'
	// own colour would have been written straight over both - which is exactly
	// what made "Remove (transparent)" look like it did nothing.
	std::vector<uint8_t> overBlue, overRed;
	REQUIRE(captureOverBackground(s.color, w, h, 0.f, 0.f, 1.f, overBlue));
	REQUIRE(captureOverBackground(s.color, w, h, 1.f, 0.f, 0.f, overRed));
	REQUIRE(overBlue.size() == overRed.size());
	size_t differing = 0;
	for (size_t i = 0; i + 3 < overBlue.size(); i += 4)
		if (std::abs(int(overBlue[i]) - int(overRed[i])) > 8 ||
		    std::abs(int(overBlue[i + 2]) - int(overRed[i + 2])) > 8)
			++differing;
	const double differingFraction = double(differing) / double(overBlue.size() / 4);
	std::printf("compositing check: %.1f%% of pixels take the colour of what is behind the source\n",
		    differingFraction * 100.0);
	CHECK(differingFraction > transparentFraction * 0.5);
	s.destroy();
}

TEST_CASE("render cost stays within budget at 720p and 1080p")
{
	struct Case {
		const char *name;
		const char *json;
		uint32_t w, h;
		double budgetMs;
	};
	// The filter must stay far below one frame time (33 ms at 30 fps).
	// "no AI" throttles inference to 1 fps so the number is pure GPU render cost;
	// the GPU/CPU cases show how much the inference backend steals from rendering.
	const Case cases[] = {
		{"1080p transparent, no AI", R"({"bg_mode":"transparent","max_ai_fps":1,"backend":"cpu"})", 1920, 1080, 20.0},
		{"1080p blur, no AI", R"({"bg_mode":"blur","blur_amount":0.6,"max_ai_fps":1,"backend":"cpu"})", 1920, 1080, 22.0},
		{"1080p transparent, CPU inference", R"({"bg_mode":"transparent","backend":"cpu"})", 1920, 1080, 22.0},
		{"1080p transparent, GPU inference", R"({"bg_mode":"transparent","backend":"auto"})", 1920, 1080, 22.0},
		{"1080p blur, GPU inference", R"({"bg_mode":"blur","blur_amount":0.6,"backend":"auto"})", 1920, 1080, 22.0},
		{"1080p fast upsample, GPU", R"({"bg_mode":"transparent","edge_upsample_quality":0,"backend":"auto"})", 1920, 1080, 22.0},
		{"720p transparent, GPU inference", R"({"bg_mode":"transparent","backend":"auto"})", 1280, 720, 15.0},
	};
	{
		Scene base;
		base.create(1920, 1080, nullptr);
		obs_source_set_enabled(base.filter, false);
		const double baseline = measureRenderMs();
		std::printf("render ms: %-38s %.2f\n", "baseline (filter disabled, 1080p)", baseline);
		base.destroy();
		CHECK(baseline < 8.0);
	}
	for (const auto &c : cases) {
		Scene sc;
		sc.create(c.w, c.h, c.json);
		const double ms = measureRenderMs();
		std::printf("render ms: %-38s %.2f  (budget %.0f)\n", c.name, ms, c.budgetMs);
		sc.destroy();
		CHECK_MESSAGE(ms < c.budgetMs, c.name);
	}
}

TEST_CASE("memory stays bounded over a long render run")
{
	Scene s;
	s.create(1280, 720, R"({"backend":"auto","quality":"balanced","bg_mode":"blur"})");
	pump(90); // warm up
	const uint64_t rss0 = sysinfo::processWorkingSetBytes();
	const uint64_t vram0 = sysinfo::processVideoMemoryBytes();
	pump(900); // ~30 s
	const uint64_t rss1 = sysinfo::processWorkingSetBytes();
	const uint64_t vram1 = sysinfo::processVideoMemoryBytes();
	std::printf("RSS %.1f -> %.1f MB, VRAM %.1f -> %.1f MB\n", rss0 / 1048576.0, rss1 / 1048576.0, vram0 / 1048576.0,
		    vram1 / 1048576.0);
	CHECK(double(rss1) - double(rss0) < 64.0 * 1048576.0);
	CHECK(double(vram1) - double(vram0) < 64.0 * 1048576.0);
	s.destroy();
}

int main(int argc, char **argv)
{
	ObsRuntime rt;
	g_obs = &rt;
	if (!rt.start(1280, 720))
		std::fprintf(stderr, "OBS runtime failed: %s\n", rt.error.c_str());
	doctest::Context ctx;
	ctx.applyCommandLine(argc, argv);
	int rc = ctx.run();
	g_obs = nullptr;
	return rc;
}

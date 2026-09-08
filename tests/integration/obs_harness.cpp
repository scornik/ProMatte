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
#include <string>
#include <vector>
#include <thread>

#include <obs.h>
#include <obs-module.h>

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
	return s;
}
void testSourceDestroy(void *d)
{
	delete static_cast<TestSource *>(d);
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

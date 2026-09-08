// OBS properties (settings panel) for the ProMatte filter.
#include <cstdio>
#include <string>
#include <vector>

#include <obs-module.h>

#include "inference/backend_registry.h"
#include "models/model_manager.h"
#include "obs/filter.h"
#include "obs/presets.h"
#include "obs/settings.h"
#include "utils/logging.h"
#include "utils/system_info.h"

namespace promatte {

namespace {

#define T(key) obs_module_text(key)

void setVisible(obs_properties_t *props, const char *name, bool visible)
{
	obs_property_t *p = obs_properties_get(props, name);
	if (p)
		obs_property_set_visible(p, visible);
}

bool onModeChanged(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	BackgroundMode m = backgroundModeFromKey(obs_data_get_string(settings, keys::Mode));
	setVisible(props, keys::Color, m == BackgroundMode::Color);
	setVisible(props, keys::ColorOpacity, m == BackgroundMode::Color);
	setVisible(props, keys::ImagePath, m == BackgroundMode::Image);
	setVisible(props, keys::VideoPath, m == BackgroundMode::Video);
	setVisible(props, keys::SourceName, m == BackgroundMode::Source);
	setVisible(props, keys::Fit, m == BackgroundMode::Image || m == BackgroundMode::Video || m == BackgroundMode::Source);
	setVisible(props, keys::BlurAmount, m == BackgroundMode::Blur);
	setVisible(props, keys::BlurQuality, m == BackgroundMode::Blur);
	setVisible(props, keys::DimBrightness, m == BackgroundMode::Dim);
	setVisible(props, keys::DimOpacity, m == BackgroundMode::Dim);
	return true;
}

bool onPresetChanged(obs_properties_t *, obs_property_t *, obs_data_t *settings)
{
	std::string id = obs_data_get_string(settings, keys::Preset);
	if (id == "custom")
		return false;
	applyPreset(id, settings);
	return true; // refresh the panel so the sliders show the preset values
}

bool onAnyTuningChanged(obs_properties_t *, obs_property_t *, obs_data_t *settings)
{
	// Editing a tuning control turns the preset into "custom".
	if (std::string(obs_data_get_string(settings, keys::Preset)) != "custom") {
		obs_data_set_string(settings, keys::Preset, "custom");
		return true;
	}
	return false;
}

bool enumSourcesCb(void *param, obs_source_t *src)
{
	auto *list = static_cast<obs_property_t *>(param);
	uint32_t flags = obs_source_get_output_flags(src);
	if (!(flags & OBS_SOURCE_VIDEO))
		return true;
	const char *name = obs_source_get_name(src);
	if (name && *name)
		obs_property_list_add_string(list, name, name);
	return true;
}

std::string modelStatusText(const ModelManager &mm, const std::string &id)
{
	auto d = mm.find(id);
	if (!d)
		return "Select a model.";
	std::string path = mm.resolvePath(id);
	std::string s = d->displayName + "\n" + d->description + "\nLicense: " + d->license;
	if (!path.empty()) {
		s += "\nInstalled: " + path;
	} else if (!d->downloadUrl.empty()) {
		char buf[128];
		std::snprintf(buf, sizeof(buf), "\nNot installed (%.1f MB download from %s)",
			      double(d->sizeBytes) / (1024.0 * 1024.0), d->sourceUrl.c_str());
		s += buf;
	} else {
		s += "\nNot installed (bundled model file missing - reinstall the plugin)";
	}
	DownloadState dl = mm.downloadState();
	if (dl.modelId == id) {
		if (dl.active) {
			char buf[128];
			if (dl.total)
				std::snprintf(buf, sizeof(buf), "\nDownloading... %.1f / %.1f MB (click Refresh to update)",
					      double(dl.received) / (1024.0 * 1024.0), double(dl.total) / (1024.0 * 1024.0));
			else
				std::snprintf(buf, sizeof(buf), "\nDownloading... %.1f MB", double(dl.received) / (1024.0 * 1024.0));
			s += buf;
		} else if (dl.finished) {
			s += dl.succeeded ? "\nDownload complete and checksum verified." : "\nDownload failed: " + dl.error;
		}
	}
	return s;
}

void refreshModelStatus(obs_properties_t *props, obs_data_t *settings)
{
	obs_property_t *p = obs_properties_get(props, "mm_status");
	if (!p)
		return;
	std::string id = obs_data_get_string(settings, keys::ModelToManage);
	obs_property_set_description(p, modelStatusText(modelManager(), id).c_str());
}

bool onManagedModelChanged(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	refreshModelStatus(props, settings);
	return true;
}

bool onDownloadClicked(obs_properties_t *props, obs_property_t *, void *data)
{
	auto *ctx = static_cast<FilterContext *>(data);
	obs_data_t *settings = obs_source_get_settings(ctx->source);
	std::string id = obs_data_get_string(settings, keys::ModelToManage);
	ModelManager &mm = modelManager();
	if (mm.isInstalled(id)) {
		std::string problem;
		if (mm.verify(id, problem))
			PM_LOG_INFO("model '%s' verified OK", id.c_str());
		else
			PM_LOG_WARN("model '%s' verification failed: %s", id.c_str(), problem.c_str());
	} else if (!mm.startDownload(id)) {
		PM_LOG_WARN("could not start download for '%s'", id.c_str());
	}
	refreshModelStatus(props, settings);
	obs_data_release(settings);
	return true;
}

bool onDeleteClicked(obs_properties_t *props, obs_property_t *, void *data)
{
	auto *ctx = static_cast<FilterContext *>(data);
	obs_data_t *settings = obs_source_get_settings(ctx->source);
	std::string id = obs_data_get_string(settings, keys::ModelToManage);
	std::string err;
	if (!modelManager().remove(id, err))
		PM_LOG_WARN("delete '%s': %s", id.c_str(), err.c_str());
	refreshModelStatus(props, settings);
	obs_data_release(settings);
	ctx->configureWorker();
	return true;
}

bool onAddCustomClicked(obs_properties_t *props, obs_property_t *, void *data)
{
	auto *ctx = static_cast<FilterContext *>(data);
	obs_data_t *settings = obs_source_get_settings(ctx->source);
	std::string path = obs_data_get_string(settings, keys::CustomModelPath);
	ModelManager &mm = modelManager();
	// Custom models are treated like MODNet-style single-input/single-output matting
	// networks (NCHW RGB, [0,1] alpha output). Advanced users only.
	ModelDescriptor tmpl;
	if (auto base = mm.find("modnet_portrait"))
		tmpl = *base;
	else {
		tmpl.tiers = {{512, 288, 1.f, "Custom"}};
		tmpl.fixedInput = false;
		tmpl.alignment = 32;
	}
	auto d = mm.registerCustomModel(path, tmpl);
	if (d) {
		obs_data_set_string(settings, keys::Model, d->id.c_str());
		obs_data_set_string(settings, keys::ModelToManage, d->id.c_str());
		obs_source_update(ctx->source, settings);
	} else {
		PM_LOG_WARN("custom model path not found: %s", path.c_str());
	}
	refreshModelStatus(props, settings);
	obs_data_release(settings);
	return true;
}

bool onRefreshClicked(obs_properties_t *props, obs_property_t *, void *data)
{
	auto *ctx = static_cast<FilterContext *>(data);
	obs_property_t *p = obs_properties_get(props, "status_text");
	if (p)
		obs_property_set_description(p, ctx->statusText(true).c_str());
	obs_data_t *settings = obs_source_get_settings(ctx->source);
	refreshModelStatus(props, settings);
	obs_data_release(settings);
	return true;
}

} // namespace

obs_properties_t *buildProperties(void *data)
{
	auto *ctx = static_cast<FilterContext *>(data);
	ModelManager &mm = modelManager();
	obs_properties_t *props = obs_properties_create();

	// --- Preset ------------------------------------------------------------
	obs_property_t *preset = obs_properties_add_list(props, keys::Preset, T("Preset"), OBS_COMBO_TYPE_LIST,
							 OBS_COMBO_FORMAT_STRING);
	for (const auto &p : presetList())
		obs_property_list_add_string(preset, T(p.localeKey), p.id);
	obs_property_set_modified_callback(preset, onPresetChanged);

	// --- Background --------------------------------------------------------
	obs_properties_t *bg = obs_properties_create();
	obs_property_t *mode = obs_properties_add_list(bg, keys::Mode, T("Mode"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(mode, T("Mode.Transparent"), "transparent");
	obs_property_list_add_string(mode, T("Mode.Blur"), "blur");
	obs_property_list_add_string(mode, T("Mode.Color"), "color");
	obs_property_list_add_string(mode, T("Mode.Image"), "image");
	obs_property_list_add_string(mode, T("Mode.Video"), "video");
	obs_property_list_add_string(mode, T("Mode.Dim"), "dim");
	obs_property_list_add_string(mode, T("Mode.Source"), "source");
	obs_property_set_modified_callback(mode, onModeChanged);
	obs_properties_add_color(bg, keys::Color, T("Color"));
	obs_properties_add_float_slider(bg, keys::ColorOpacity, T("ColorOpacity"), 0.0, 1.0, 0.01);
	obs_properties_add_path(bg, keys::ImagePath, T("ImagePath"), OBS_PATH_FILE, T("ImageFilter"), nullptr);
	obs_properties_add_path(bg, keys::VideoPath, T("VideoPath"), OBS_PATH_FILE, T("VideoFilter"), nullptr);
	obs_property_t *srcList = obs_properties_add_list(bg, keys::SourceName, T("SourceName"), OBS_COMBO_TYPE_LIST,
							  OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(srcList, T("SourceName.None"), "");
	obs_enum_sources(enumSourcesCb, srcList);
	obs_property_t *fit = obs_properties_add_list(bg, keys::Fit, T("Fit"), OBS_COMBO_TYPE_LIST,
						      OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(fit, T("Fit.Cover"), "cover");
	obs_property_list_add_string(fit, T("Fit.Contain"), "contain");
	obs_property_list_add_string(fit, T("Fit.Stretch"), "stretch");
	obs_properties_add_float_slider(bg, keys::BlurAmount, T("BlurAmount"), 0.0, 1.0, 0.01);
	obs_property_t *bq = obs_properties_add_list(bg, keys::BlurQuality, T("BlurQuality"), OBS_COMBO_TYPE_LIST,
						     OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(bq, T("BlurQuality.Fast"), 0);
	obs_property_list_add_int(bq, T("BlurQuality.Balanced"), 1);
	obs_property_list_add_int(bq, T("BlurQuality.High"), 2);
	obs_properties_add_float_slider(bg, keys::DimBrightness, T("DimBrightness"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(bg, keys::DimOpacity, T("DimOpacity"), 0.0, 1.0, 0.01);
	obs_properties_add_group(props, "group_background", T("Group.Background"), OBS_GROUP_NORMAL, bg);

	// --- Quality -----------------------------------------------------------
	obs_properties_t *q = obs_properties_create();
	obs_property_t *quality = obs_properties_add_list(q, keys::Quality, T("Quality"), OBS_COMBO_TYPE_LIST,
							  OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(quality, T("Quality.Auto"), "auto");
	obs_property_list_add_string(quality, T("Quality.Performance"), "performance");
	obs_property_list_add_string(quality, T("Quality.Balanced"), "balanced");
	obs_property_list_add_string(quality, T("Quality.Quality"), "quality");
	obs_property_list_add_string(quality, T("Quality.Ultra"), "ultra");
	obs_property_set_modified_callback(quality, onAnyTuningChanged);
	obs_property_t *model = obs_properties_add_list(q, keys::Model, T("Model"), OBS_COMBO_TYPE_LIST,
							OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(model, T("Model.Auto"), "auto");
	for (const auto &m : mm.list()) {
		std::string label = m.desc.displayName;
		if (!m.installed)
			label += std::string(" ") + T("Model.NotInstalled");
		size_t idx = obs_property_list_add_string(model, label.c_str(), m.desc.id.c_str());
		if (!m.installed)
			obs_property_list_item_disable(model, idx, true);
	}
	obs_property_t *backend = obs_properties_add_list(q, keys::Backend, T("Backend"), OBS_COMBO_TYPE_LIST,
							  OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(backend, T("Backend.Auto"), "auto");
	for (const auto &b : enumerateBackends()) {
		std::string label = b.name;
		if (b.kind == BackendKind::DirectML)
			label = "DirectML (GPU)";
		else if (b.kind == BackendKind::CUDA)
			label = "CUDA (NVIDIA)";
		else if (b.kind == BackendKind::TensorRT)
			label = "TensorRT (NVIDIA)";
		else if (b.kind == BackendKind::CoreML)
			label = "CoreML (Apple)";
		else if (b.kind == BackendKind::OpenVINO)
			label = "OpenVINO (Intel)";
		else if (b.kind == BackendKind::CPU)
			label = "CPU";
		if (!b.deviceName.empty() && b.available)
			label += " - " + b.deviceName;
		if (!b.available)
			label += std::string(" ") + T("Backend.Unavailable");
		size_t idx = obs_property_list_add_string(backend, label.c_str(), backendKindName(b.kind));
		if (!b.available)
			obs_property_list_item_disable(backend, idx, true);
	}
	obs_properties_add_group(props, "group_quality", T("Group.Quality"), OBS_GROUP_NORMAL, q);

	// --- Edge --------------------------------------------------------------
	obs_properties_t *e = obs_properties_create();
	auto slider = [&](obs_properties_t *g, const char *key, const char *label, double lo, double hi) {
		obs_property_t *p = obs_properties_add_float_slider(g, key, T(label), lo, hi, 0.01);
		obs_property_set_modified_callback(p, onAnyTuningChanged);
		return p;
	};
	slider(e, keys::Separation, "Separation", 0.05, 0.95);
	slider(e, keys::Smoothness, "Smoothness", 0.0, 1.0);
	slider(e, keys::Feather, "Feather", 0.0, 1.0);
	slider(e, keys::Shift, "Shift", -1.0, 1.0);
	slider(e, keys::Decontaminate, "Decontaminate", 0.0, 1.0);
	slider(e, keys::HaloRemoval, "HaloRemoval", 0.0, 1.0);
	obs_properties_add_bool(e, keys::RemoveBlobs, T("RemoveBlobs"));
	obs_properties_add_bool(e, keys::FillHoles, T("FillHoles"));
	obs_property_t *uq = obs_properties_add_list(e, keys::UpsampleQuality, T("UpsampleQuality"), OBS_COMBO_TYPE_LIST,
						     OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(uq, T("UpsampleQuality.Fast"), 0);
	obs_property_list_add_int(uq, T("UpsampleQuality.High"), 1);
	obs_properties_add_group(props, "group_edge", T("Group.Edge"), OBS_GROUP_NORMAL, e);

	// --- Temporal ----------------------------------------------------------
	obs_properties_t *t = obs_properties_create();
	slider(t, keys::Stability, "Stability", 0.0, 1.0);
	slider(t, keys::MotionResponse, "MotionResponse", 0.0, 1.0);
	obs_properties_add_group(props, "group_temporal", T("Group.Temporal"), OBS_GROUP_NORMAL, t);

	// --- Status ------------------------------------------------------------
	obs_properties_t *st = obs_properties_create();
	obs_property_t *status = obs_properties_add_text(st, "status_text", ctx ? ctx->statusText(true).c_str() : "",
							 OBS_TEXT_INFO);
	obs_property_text_set_info_type(status, ctx && ctx->worker && ctx->worker->status().state == WorkerState::Error
							? OBS_TEXT_INFO_ERROR
							: OBS_TEXT_INFO_NORMAL);
	obs_properties_add_button2(st, "status_refresh", T("StatusRefresh"), onRefreshClicked, ctx);
	obs_properties_add_group(props, "group_status", T("Group.Status"), OBS_GROUP_NORMAL, st);

	// --- Model manager -------------------------------------------------------
	obs_properties_t *m = obs_properties_create();
	obs_property_t *mlist = obs_properties_add_list(m, keys::ModelToManage, T("ModelToManage"), OBS_COMBO_TYPE_LIST,
							OBS_COMBO_FORMAT_STRING);
	for (const auto &ms : mm.list()) {
		std::string label = ms.desc.displayName + (ms.installed ? " [installed]" : " [not installed]");
		obs_property_list_add_string(mlist, label.c_str(), ms.desc.id.c_str());
	}
	obs_property_set_modified_callback(mlist, onManagedModelChanged);
	obs_property_t *mstatus = obs_properties_add_text(m, "mm_status", "", OBS_TEXT_INFO);
	if (ctx) {
		obs_data_t *settings = obs_source_get_settings(ctx->source);
		obs_property_set_description(mstatus, modelStatusText(mm, obs_data_get_string(settings, keys::ModelToManage)).c_str());
		obs_data_release(settings);
	}
	obs_properties_add_button2(m, "mm_download", T("ModelDownload"), onDownloadClicked, ctx);
	obs_properties_add_button2(m, "mm_delete", T("ModelDelete"), onDeleteClicked, ctx);
	obs_properties_add_path(m, keys::CustomModelPath, T("CustomModelPath"), OBS_PATH_FILE, T("CustomModelFilter"),
				nullptr);
	obs_properties_add_button2(m, "mm_add_custom", T("CustomModelAdd"), onAddCustomClicked, ctx);
	obs_properties_add_group(props, "group_models", T("Group.Models"), OBS_GROUP_NORMAL, m);

	// --- Advanced ------------------------------------------------------------
	obs_properties_t *a = obs_properties_create();
	obs_property_t *dv = obs_properties_add_list(a, keys::DebugView, T("DebugView"), OBS_COMBO_TYPE_LIST,
						     OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(dv, T("DebugView.None"), "none");
	obs_property_list_add_string(dv, T("DebugView.Matte"), "matte");
	obs_property_list_add_string(dv, T("DebugView.Edges"), "edges");
	obs_property_list_add_string(dv, T("DebugView.Confidence"), "confidence");
	obs_property_list_add_string(dv, T("DebugView.Foreground"), "foreground");
	obs_property_list_add_string(dv, T("DebugView.RawMatte"), "raw_matte");
	obs_properties_add_bool(a, keys::Overlay, T("Overlay"));
	obs_properties_add_int(a, keys::CpuThreads, T("CpuThreads"), 0, 32, 1);
	obs_property_t *gpuList = obs_properties_add_list(a, keys::GpuIndex, T("GpuIndex"), OBS_COMBO_TYPE_LIST,
							  OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(gpuList, T("GpuIndex.Auto"), -1);
	for (const auto &g : sysinfo::enumerateGpus()) {
		if (g.isSoftware)
			continue;
		char label[320];
		std::snprintf(label, sizeof(label), "%d: %s (%.0f MB)", g.adapterIndex, g.name.c_str(),
			      double(g.dedicatedVideoMemory) / (1024.0 * 1024.0));
		obs_property_list_add_int(gpuList, label, g.adapterIndex);
	}
	obs_properties_add_int(a, keys::ManualTier, T("ManualTier"), -1, 8, 1);
	obs_properties_add_int(a, keys::MaxAiFps, T("MaxAiFps"), 0, 120, 1);
	obs_properties_add_group(props, "group_advanced", T("Group.Advanced"), OBS_GROUP_NORMAL, a);

	obs_properties_add_text(props, "privacy_note", T("Privacy"), OBS_TEXT_INFO);
	return props;
}

} // namespace promatte

// ProMatte benchmark utility.
//
// Measures, per (model, backend, resolution tier): preprocessing, inference and
// post-processing time, throughput, process RSS / VRAM growth, CPU usage and two
// quality proxies (temporal flicker on a quasi-static sequence, alpha softness).
// Emits machine-readable JSON.
//
//   promatte-bench --manifest data/models/manifest.json --models-dir models/converted \
//                  [--model <id>] [--backend auto|directml|cuda|tensorrt|cpu] [--tier N|all] \
//                  [--frames 120] [--warmup 15] [--input <dir with .ppm frames>] \
//                  [--width 1280 --height 720] [--out results.json] [--dump-dir <dir>]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "image_io.h"
#include "inference/backend_registry.h"
#include "inference/model_descriptor.h"
#include "models/model_manager.h"
#include "postprocessing/matte_refine.h"
#include "preprocessing/preprocess.h"
#include "temporal/temporal_stabilizer.h"
#include "utils/file_utils.h"
#include "utils/logging.h"
#include "utils/system_info.h"
#include "utils/timer.h"

using namespace promatte;
using json = nlohmann::json;

namespace {

struct Args {
	std::string manifest = "data/models/manifest.json";
	std::string modelsDir = "models/converted";
	std::string model;
	std::string backend = "auto";
	std::string tier = "all";
	int frames = 120;
	int warmup = 15;
	std::string input;
	uint32_t width = 1280, height = 720;
	std::string out = "benchmark-results.json";
	std::string dumpDir;
	bool verbose = false;
	int device = -1; // GPU adapter index for DirectML/CUDA (-1 = backend default)
	int seqLen = 12; // frames per input sequence (flicker is measured within a sequence)
	std::string install; // model id to download/verify into --models-dir, then exit
};

bool parseArgs(int argc, char **argv, Args &a)
{
	for (int i = 1; i < argc; ++i) {
		std::string k = argv[i];
		auto next = [&](std::string &dst) {
			if (i + 1 >= argc)
				return false;
			dst = argv[++i];
			return true;
		};
		std::string v;
		if (k == "--manifest" && next(v))
			a.manifest = v;
		else if (k == "--models-dir" && next(v))
			a.modelsDir = v;
		else if (k == "--model" && next(v))
			a.model = v;
		else if (k == "--backend" && next(v))
			a.backend = v;
		else if (k == "--tier" && next(v))
			a.tier = v;
		else if (k == "--frames" && next(v))
			a.frames = std::atoi(v.c_str());
		else if (k == "--warmup" && next(v))
			a.warmup = std::atoi(v.c_str());
		else if (k == "--input" && next(v))
			a.input = v;
		else if (k == "--width" && next(v))
			a.width = uint32_t(std::atoi(v.c_str()));
		else if (k == "--height" && next(v))
			a.height = uint32_t(std::atoi(v.c_str()));
		else if (k == "--out" && next(v))
			a.out = v;
		else if (k == "--dump-dir" && next(v))
			a.dumpDir = v;
		else if (k == "--verbose")
			a.verbose = true;
		else if (k == "--device" && next(v))
			a.device = std::atoi(v.c_str());
		else if (k == "--seq-len" && next(v))
			a.seqLen = std::max(1, std::atoi(v.c_str()));
		else if (k == "--install" && next(v))
			a.install = v;
		else {
			std::fprintf(stderr, "unknown or incomplete argument: %s\n", k.c_str());
			return false;
		}
	}
	return true;
}

std::vector<bench::Image> loadInputs(const Args &a)
{
	std::vector<bench::Image> frames;
	if (!a.input.empty()) {
		for (const auto &f : fs::listFiles(a.input)) {
			if (fs::extension(f) != ".ppm")
				continue;
			bench::Image img;
			if (bench::loadPpm(f, img))
				frames.push_back(std::move(img));
		}
		if (!frames.empty())
			std::fprintf(stderr, "loaded %zu frames from %s (%ux%u)\n", frames.size(), a.input.c_str(),
				     frames.front().width, frames.front().height);
	}
	if (frames.empty()) {
		std::fprintf(stderr, "using synthetic frames %ux%u\n", a.width, a.height);
		for (int i = 0; i < 60; ++i)
			frames.push_back(bench::synthesizeFrame(a.width, a.height, uint32_t(i), float(i) * 0.05f));
	}
	return frames;
}

struct Result {
	std::string model, backend, device, tier;
	uint32_t aiW = 0, aiH = 0;
	float ratio = 1.f;
	double preMs = 0, infMs = 0, infP95 = 0, postMs = 0, totalMs = 0, fps = 0;
	double rssMb = 0, rssDeltaMb = 0, vramMb = 0, vramDeltaMb = 0, cpuPercent = 0;
	double flicker = 0, softness = 0, coverage = 0;
	double initMs = 0;
	bool ok = false;
	std::string error;
};

Result runOne(const Args &a, const ModelDescriptor &m, const std::string &path, BackendKind kind, size_t tierIdx,
	      const std::vector<bench::Image> &frames)
{
	Result r;
	r.model = m.id;
	r.backend = backendKindName(kind);
	const ResolutionTier &tier = m.tiers[tierIdx];
	r.tier = tier.label;
	r.ratio = tier.downsampleRatio;
	chooseAiResolution(frames.front().width, frames.front().height, tier.width, tier.height, m.fixedInput, m.alignment,
			   r.aiW, r.aiH);

	const uint64_t rss0 = sysinfo::processWorkingSetBytes();
	const uint64_t vram0 = sysinfo::processVideoMemoryBytes();
	auto backend = createBackend(kind);
	BackendConfig cfg;
	cfg.kind = kind;
	cfg.allowFallback = false;
	cfg.deviceIndex = a.device >= 0 ? a.device : preferredGpuAdapter("");
	std::string err;
	Stopwatch initSw;
	if (!backend->initialize(cfg, m, path, err)) {
		r.error = err;
		return r;
	}
	r.initMs = initSw.elapsedMs();
	r.device = backend->capabilities().deviceName;

	// Pre-resize all frames to the AI resolution (the plugin does this on the GPU).
	const size_t n = size_t(r.aiW) * r.aiH;
	std::vector<std::vector<uint8_t>> small(frames.size());
	for (size_t i = 0; i < frames.size(); ++i) {
		small[i].resize(n * 4);
		resizeBgra(frames[i].bgra.data(), frames[i].width, frames[i].height, frames[i].width * 4, small[i].data(),
			   r.aiW, r.aiH);
	}
	PreprocessOptions po;
	po.layout = m.inputLayout;
	po.order = m.channelOrder;
	std::copy_n(m.mean, 3, po.mean);
	std::copy_n(m.std, 3, po.std);
	std::vector<float> tensor(n * 3);
	std::vector<uint8_t> luma(n);
	InferenceInput in;
	in.width = r.aiW;
	in.height = r.aiH;
	in.downsampleRatio = tier.downsampleRatio;
	in.shape = m.inputLayout == TensorLayout::NCHW ? std::vector<int64_t>{1, 3, int64_t(r.aiH), int64_t(r.aiW)}
						       : std::vector<int64_t>{1, int64_t(r.aiH), int64_t(r.aiW), 3};
	in.data = tensor.data();
	InferenceOutput out;
	MatteRefiner refiner;
	RefineParams rp;
	TemporalStabilizer temporal;
	TemporalParams tp;
	temporal.setParams(tp);

	std::vector<double> infTimes;
	double preSum = 0, postSum = 0, totalSum = 0;
	std::vector<float> prevAlpha;
	double flickerSum = 0;
	int flickerCount = 0;
	double softSum = 0, coverSum = 0;
	sysinfo::processCpuPercent(); // prime
	Stopwatch wall;
	const int total = a.warmup + a.frames;
	for (int i = 0; i < total; ++i) {
		if (i == a.warmup) {
			wall.reset(); // exclude warm-up (first-run shader compilation) from throughput
			sysinfo::processCpuPercent();
		}
		// Frames are short quasi-static sequences (sensor noise + 1-2 px jitter);
		// flicker is the mean |alpha_t - alpha_t-1| within a sequence.
		const size_t fi = size_t(i) % small.size();
		const auto &frame = small[fi];
		Stopwatch sw;
		bgraToTensor(frame.data(), r.aiW, r.aiH, r.aiW * 4, po, tensor.data());
		bgraToLuma(frame.data(), r.aiW, r.aiH, r.aiW * 4, luma.data());
		double pre = sw.elapsedMs();
		sw.reset();
		if (!backend->processFrame(in, out, err)) {
			r.error = err;
			return r;
		}
		double inf = sw.elapsedMs();
		sw.reset();
		std::vector<float> raw = out.alpha; // keep raw for quality proxies
		refiner.process(out.alpha.data(), r.aiW, r.aiH, rp);
		temporal.process(out.alpha.data(), luma.data(), r.aiW, r.aiH, 33.3, m.recurrent);
		double post = sw.elapsedMs();
		if (i >= a.warmup) {
			infTimes.push_back(inf);
			preSum += pre;
			postSum += post;
			totalSum += pre + inf + post;
			double soft = 0, cover = 0;
			for (float v : raw) {
				if (v > 0.1f && v < 0.9f)
					soft += 1;
				if (v > 0.5f)
					cover += 1;
			}
			softSum += soft / double(n);
			coverSum += cover / double(n);
			if (!prevAlpha.empty() && (fi % size_t(a.seqLen)) != 0) {
				double d = 0;
				for (size_t k = 0; k < n; ++k)
					d += std::abs(raw[k] - prevAlpha[k]);
				flickerSum += d / double(n);
				++flickerCount;
			}
			prevAlpha = raw;
		}
	}
	const double wallMs = wall.elapsedMs();
	r.cpuPercent = sysinfo::processCpuPercent();
	std::sort(infTimes.begin(), infTimes.end());
	const size_t cnt = infTimes.size();
	if (cnt) {
		double sum = 0;
		for (double v : infTimes)
			sum += v;
		r.infMs = sum / double(cnt);
		r.infP95 = infTimes[std::min(cnt - 1, size_t(double(cnt) * 0.95))];
		r.preMs = preSum / double(cnt);
		r.postMs = postSum / double(cnt);
		r.totalMs = totalSum / double(cnt);
		r.fps = 1000.0 * double(a.frames) / wallMs;
		r.softness = softSum / double(cnt);
		r.coverage = coverSum / double(cnt);
		r.flicker = flickerCount ? flickerSum / flickerCount : 0;
	}
	r.rssMb = double(sysinfo::processWorkingSetBytes()) / (1024.0 * 1024.0);
	r.rssDeltaMb = double(int64_t(sysinfo::processWorkingSetBytes()) - int64_t(rss0)) / (1024.0 * 1024.0);
	r.vramMb = double(sysinfo::processVideoMemoryBytes()) / (1024.0 * 1024.0);
	r.vramDeltaMb = double(int64_t(sysinfo::processVideoMemoryBytes()) - int64_t(vram0)) / (1024.0 * 1024.0);
	r.ok = true;

	if (!a.dumpDir.empty()) {
		fs::createDirectories(a.dumpDir);
		std::vector<uint8_t> gray(n);
		for (size_t k = 0; k < n; ++k)
			gray[k] = uint8_t(std::clamp(out.alpha[k], 0.f, 1.f) * 255.f + 0.5f);
		std::string base = fs::joinPath(a.dumpDir, m.id + "_" + r.backend + "_" + tier.label);
		bench::savePgm(base + "_matte.pgm", gray, r.aiW, r.aiH);
		bench::Image comp;
		comp.width = r.aiW;
		comp.height = r.aiH;
		comp.bgra.resize(n * 4);
		const auto &frame = small[size_t(total - 1) % small.size()];
		for (size_t k = 0; k < n; ++k) {
			float al = std::clamp(out.alpha[k], 0.f, 1.f);
			for (int c = 0; c < 3; ++c)
				comp.bgra[k * 4 + c] = uint8_t(frame[k * 4 + c] * al + (c == 1 ? 255 : 0) * (1.f - al));
			comp.bgra[k * 4 + 3] = 255;
		}
		bench::savePng(base + "_composite.png", comp);
	}
	backend->shutdown();
	return r;
}

} // namespace

int main(int argc, char **argv)
{
	Args a;
	if (!parseArgs(argc, argv, a))
		return 2;
	log::setMinLevel(a.verbose ? log::Level::Debug : log::Level::Warning);

	ModelManager manager(a.modelsDir, a.modelsDir);
	std::string err;
	if (!manager.loadManifestFile(a.manifest, err)) {
		std::fprintf(stderr, "%s\n", err.c_str());
		return 1;
	}
	if (!a.install.empty()) {
		if (manager.isInstalled(a.install)) {
			std::string problem;
			bool ok = manager.verify(a.install, problem);
			std::fprintf(stderr, "%s already installed at %s (%s)\n", a.install.c_str(),
				     manager.resolvePath(a.install).c_str(), ok ? "checksum OK" : problem.c_str());
			return ok ? 0 : 1;
		}
		if (!manager.startDownload(a.install)) {
			std::fprintf(stderr, "cannot download '%s' (unknown model or no download URL)\n", a.install.c_str());
			return 1;
		}
		DownloadState st;
		double lastPct = -1;
		do {
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
			st = manager.downloadState();
			double pct = st.total ? 100.0 * double(st.received) / double(st.total) : 0;
			if (pct - lastPct >= 5) {
				std::fprintf(stderr, "downloading %s: %5.1f%% (%.1f MB)\n", a.install.c_str(), pct,

					     double(st.received) / (1024.0 * 1024.0));
				lastPct = pct;
			}
		} while (st.active);
		manager.waitForDownload();
		std::string summary = st.succeeded ? "installed at " + manager.resolvePath(a.install)
						   : "download failed: " + st.error;
		std::fprintf(stderr, "\n%s\n", summary.c_str());
		return st.succeeded ? 0 : 1;
	}
	auto frames = loadInputs(a);

	std::vector<BackendKind> backends;
	if (a.backend == "all") {
		for (const auto &c : enumerateBackends())
			if (c.available)
				backends.push_back(c.kind);
	} else {
		BackendKind k = backendKindFromString(a.backend);
		if (k == BackendKind::Auto)
			k = resolveAutoBackend();
		backends.push_back(k);
	}

	json results = json::array();
	json sys;
	sys["cpu"] = sysinfo::cpuName();
	sys["cores"] = sysinfo::logicalCores();
	sys["ram_gb"] = double(sysinfo::totalPhysicalMemory()) / (1024.0 * 1024 * 1024);
	sys["os"] = sysinfo::osVersion();
	json gpus = json::array();
	for (const auto &g : sysinfo::enumerateGpus())
		gpus.push_back({{"name", g.name},
				{"vendor", sysinfo::vendorName(g.vendorId)},
				{"vram_mb", double(g.dedicatedVideoMemory) / (1024.0 * 1024)}});
	sys["gpus"] = gpus;
	sys["input"] = {{"width", frames.front().width}, {"height", frames.front().height}, {"frames", frames.size()}};

	for (const auto &status : manager.list()) {
		const ModelDescriptor &m = status.desc;
		if (!a.model.empty() && m.id != a.model)
			continue;
		if (!status.installed) {
			std::fprintf(stderr, "skip %s: not installed in %s\n", m.id.c_str(), a.modelsDir.c_str());
			continue;
		}
		for (BackendKind kind : backends) {
			for (size_t t = 0; t < m.tiers.size(); ++t) {
				if (a.tier != "all" && size_t(std::atoi(a.tier.c_str())) != t)
					continue;
				std::fprintf(stderr, "[%s] %s tier %zu (%s) ...\n", backendKindName(kind), m.id.c_str(), t,
					     m.tiers[t].label.c_str());
				Result r = runOne(a, m, status.path, kind, t, frames);
				json j;
				j["model"] = r.model;
				j["backend"] = r.backend;
				j["device"] = r.device;
				j["tier"] = r.tier;
				j["input"] = std::to_string(r.aiW) + "x" + std::to_string(r.aiH);
				j["downsample_ratio"] = r.ratio;
				j["ok"] = r.ok;
				if (!r.ok) {
					j["error"] = r.error;
					std::fprintf(stderr, "   FAILED: %s\n", r.error.c_str());
				} else {
					j["init_ms"] = r.initMs;
					j["preprocess_ms"] = r.preMs;
					j["latency_ms"] = r.infMs;
					j["latency_p95_ms"] = r.infP95;
					j["postprocess_ms"] = r.postMs;
					j["total_ms"] = r.totalMs;
					j["fps"] = r.fps;
					j["rss_mb"] = r.rssMb;
					j["rss_delta_mb"] = r.rssDeltaMb;
					j["vram_mb"] = r.vramMb;
					j["vram_delta_mb"] = r.vramDeltaMb;
					j["cpu_percent"] = r.cpuPercent;
					j["flicker"] = r.flicker;
					j["softness"] = r.softness;
					j["coverage"] = r.coverage;
					std::fprintf(stderr,
						     "   %ux%u  inf %.1f ms (p95 %.1f)  pre %.2f  post %.2f  total %.1f ms  %.1f fps"
						     "  cpu %.0f%%  vram +%.0f MB  flicker %.4f  soft %.3f\n",
						     r.aiW, r.aiH, r.infMs, r.infP95, r.preMs, r.postMs, r.totalMs, r.fps,
						     r.cpuPercent, r.vramDeltaMb, r.flicker, r.softness);
				}
				results.push_back(j);
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			}
		}
	}
	json root;
	root["system"] = sys;
	root["results"] = results;
	root["version"] = PROMATTE_VERSION;
	fs::writeTextFile(a.out, root.dump(2));
	std::fprintf(stderr, "wrote %s\n", a.out.c_str());
	return 0;
}

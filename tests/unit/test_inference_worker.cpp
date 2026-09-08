#include <doctest.h>

#include <chrono>
#include <functional>
#include <thread>

#include "inference/backend_registry.h"
#include "inference/inference_worker.h"
#include "models/model_manager.h"
#include "utils/file_utils.h"

using namespace promatte;

namespace {

bool loadModel(ModelDescriptor &out, std::string &path, const char *id)
{
	ModelManager mm(PROMATTE_MODELS_DIR, PROMATTE_MODELS_DIR);
	std::string err;
	if (!mm.loadManifestFile(fs::joinPath(PROMATTE_SOURCE_DIR, "data/models/manifest.json"), err))
		return false;
	auto d = mm.find(id);
	if (!d)
		return false;
	path = mm.resolvePath(id);
	if (path.empty())
		return false;
	out = *d;
	return true;
}

void fillFrame(FrameBuffer &f, uint32_t w, uint32_t h, uint8_t v)
{
	f.resize(w, h);
	std::fill(f.bgra.begin(), f.bgra.end(), v);
	for (size_t i = 3; i < f.bgra.size(); i += 4)
		f.bgra[i] = 255;
}

bool waitFor(const std::function<bool()> &pred, int ms)
{
	auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
	while (std::chrono::steady_clock::now() < end) {
		if (pred())
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return pred();
}

} // namespace

TEST_CASE("worker without a model reports NoModel and never blocks submit")
{
	InferenceWorker w;
	w.start();
	WorkerConfig cfg; // empty model
	w.setConfig(cfg);
	FrameBuffer f;
	fillFrame(f, 64, 64, 100);
	for (int i = 0; i < 10; ++i)
		w.submitFrame(f); // must not block or crash
	CHECK(waitFor([&] { return w.status().state == WorkerState::NoModel; }, 2000));
	MatteBuffer m;
	CHECK_FALSE(w.takeMatte(m));
	w.stop();
	CHECK(w.status().state == WorkerState::Stopped);
}

TEST_CASE("worker with a missing model file reports an error, not a crash")
{
	InferenceWorker w;
	w.start();
	WorkerConfig cfg;
	cfg.model.id = "ghost";
	cfg.model.tiers.push_back({64, 64, 1.f, "Fixed"});
	cfg.modelPath = "C:/definitely/not/here.onnx";
	cfg.backend.kind = BackendKind::CPU;
	w.setConfig(cfg);
	CHECK(waitFor([&] { return w.status().state == WorkerState::Error; }, 5000));
	CHECK_FALSE(w.status().error.empty());
	w.stop();
}

TEST_CASE("worker processes frames with a real model on the CPU backend (latest frame wins)")
{
	ModelDescriptor model;
	std::string path;
	if (!loadModel(model, path, "mediapipe_selfie_landscape")) {
		MESSAGE("model not converted yet; skipping");
		return;
	}
	InferenceWorker w;
	w.start();
	WorkerConfig cfg;
	cfg.model = model;
	cfg.modelPath = path;
	cfg.backend.kind = BackendKind::CPU;
	cfg.backend.cpuThreads = 2;
	cfg.sourceWidth = 640;
	cfg.sourceHeight = 360;
	cfg.sourceFps = 30;
	w.setConfig(cfg);
	REQUIRE(waitFor([&] { return w.status().state == WorkerState::Running; }, 15000));
	uint32_t aw = 0, ah = 0;
	w.desiredResolution(aw, ah);
	CHECK(aw == 256);
	CHECK(ah == 144);

	// Flood the worker faster than it can process: no unbounded queue, drops counted.
	FrameBuffer f;
	for (int i = 0; i < 50; ++i) {
		fillFrame(f, aw, ah, uint8_t(i * 5));
		w.submitFrame(f);
	}
	MatteBuffer m;
	CHECK(waitFor([&] { return w.takeMatte(m); }, 10000));
	CHECK(m.width == aw);
	CHECK(m.height == ah);
	CHECK(m.rgba.size() == size_t(aw) * ah * 4);
	auto s = w.stats();
	CHECK(s.framesSubmitted == 50);
	CHECK(s.framesDropped > 0);
	CHECK(s.framesDropped < 50);
	CHECK(s.queueDepth <= 1);

	// A frame at the wrong size is resized on the CPU rather than rejected.
	fillFrame(f, 320, 200, 50);
	w.submitFrame(f);
	MatteBuffer m2;
	m2.seq = m.seq;
	CHECK(waitFor([&] { return w.takeMatte(m2); }, 5000));
	CHECK(m2.width == aw);

	// Disabling stops processing immediately.
	w.setEnabled(false);
	std::this_thread::sleep_for(std::chrono::milliseconds(300)); // let an in-flight frame finish
	auto before = w.stats().framesProcessed;
	for (int i = 0; i < 5; ++i) {
		fillFrame(f, aw, ah, 30);
		w.submitFrame(f);
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	CHECK(w.stats().framesProcessed == before);
	w.setEnabled(true);

	// Release the backend and re-configure -> works again.
	w.releaseBackend();
	CHECK(waitFor([&] { return w.status().state == WorkerState::Idle; }, 3000));
	w.setConfig(cfg);
	REQUIRE(waitFor([&] { return w.status().state == WorkerState::Running; }, 15000));
	w.stop();
}

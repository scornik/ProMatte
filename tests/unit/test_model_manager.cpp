#include <doctest.h>

#include "models/model_manager.h"
#include "performance/capability_store.h"
#include "utils/file_utils.h"
#include "utils/sha256.h"

using namespace promatte;

namespace {
const char *kManifest = R"({"models":[
  {"id":"tiny","file_name":"tiny.onnx","license":"MIT","bundled":true,"sha256":"%s","size_bytes":5,
   "tiers":[{"width":8,"height":8}]},
  {"id":"remote","file_name":"remote.onnx","license":"MIT","download_url":"https://example.invalid/remote.onnx",
   "tiers":[{"width":8,"height":8}]}
]})";

std::string tempDir(const char *name)
{
	std::string d = fs::joinPath(fs::joinPath(PROMATTE_SOURCE_DIR, "build"), name);
	fs::createDirectories(d);
	for (const auto &f : fs::listFiles(d))
		fs::removeFile(f);
	return d;
}
} // namespace

TEST_CASE("model manager resolves bundled and user models and verifies checksums")
{
	std::string bundled = tempDir("mm_bundled");
	std::string user = tempDir("mm_user");
	REQUIRE(fs::writeTextFile(fs::joinPath(bundled, "tiny.onnx"), "hello"));
	std::string sha = Sha256::hashString("hello");
	char buf[2048];
	std::snprintf(buf, sizeof(buf), kManifest, sha.c_str());

	ModelManager mm(bundled, user);
	std::string err;
	REQUIRE_MESSAGE(mm.loadManifestText(buf, err), err);

	auto list = mm.list();
	REQUIRE(list.size() == 2);
	CHECK(list[0].installed);
	CHECK_FALSE(list[1].installed);
	CHECK(mm.resolvePath("tiny") == fs::joinPath(bundled, "tiny.onnx"));
	CHECK(mm.resolvePath("remote").empty());
	CHECK(mm.resolvePath("nope").empty());

	std::string problem;
	CHECK(mm.verify("tiny", problem));
	CHECK_FALSE(mm.verify("remote", problem));
	CHECK(problem == "not installed");

	// A user-dir copy takes precedence; a corrupted one fails verification.
	REQUIRE(fs::writeTextFile(fs::joinPath(user, "tiny.onnx"), "HELLO"));
	CHECK(mm.resolvePath("tiny") == fs::joinPath(user, "tiny.onnx"));
	CHECK_FALSE(mm.verify("tiny", problem));
	CHECK(problem == "checksum mismatch");
	CHECK(mm.remove("tiny", err));
	CHECK(mm.resolvePath("tiny") == fs::joinPath(bundled, "tiny.onnx"));
	CHECK_FALSE(mm.remove("tiny", err)); // bundled cannot be deleted
	CHECK(mm.verify("tiny", problem));

	// installFromFile rejects wrong content, accepts the right one
	std::string wrong = fs::joinPath(user, "wrong.bin");
	fs::writeTextFile(wrong, "nope");
	CHECK_FALSE(mm.installFromFile(wrong, "tiny", err));
	std::string right = fs::joinPath(user, "right.bin");
	fs::writeTextFile(right, "hello");
	CHECK(mm.installFromFile(right, "tiny", err));
	CHECK(mm.resolvePath("tiny") == fs::joinPath(user, "tiny.onnx"));

	// default model selection prefers installed models
	auto def = mm.defaultModel(true);
	REQUIRE(def.has_value());
	CHECK(def->id == "tiny");
}

TEST_CASE("model download to an unreachable host fails cleanly")
{
	std::string bundled = tempDir("mm_bundled2");
	std::string user = tempDir("mm_user2");
	char buf[2048];
	std::snprintf(buf, sizeof(buf), kManifest, "");
	ModelManager mm(bundled, user);
	std::string err;
	REQUIRE(mm.loadManifestText(buf, err));
	CHECK_FALSE(mm.startDownload("tiny")); // no download url
	CHECK(mm.startDownload("remote"));
	CHECK_FALSE(mm.startDownload("remote")); // already active
	mm.waitForDownload();
	auto st = mm.downloadState();
	CHECK(st.finished);
	CHECK_FALSE(st.succeeded);
	CHECK_FALSE(st.error.empty());
	CHECK_FALSE(mm.isInstalled("remote"));
}

TEST_CASE("custom model registration")
{
	std::string bundled = tempDir("mm_bundled3");
	std::string user = tempDir("mm_user3");
	ModelManager mm(bundled, user);
	std::string err;
	REQUIRE(mm.loadManifestText("{\"models\":[]}", err));
	ModelDescriptor tmpl;
	tmpl.tiers.push_back({256, 256, 1.f, "Fixed"});
	CHECK_FALSE(mm.registerCustomModel(fs::joinPath(user, "missing.onnx"), tmpl).has_value());
	std::string p = fs::joinPath(user, "mine.onnx");
	fs::writeTextFile(p, "x");
	auto d = mm.registerCustomModel(p, tmpl);
	REQUIRE(d.has_value());
	CHECK(d->family == "custom");
	CHECK(mm.resolvePath(d->id) == p);
	CHECK(mm.list().size() == 1);
	CHECK(mm.remove(d->id, err));
	CHECK(mm.list().empty());
}

TEST_CASE("capability store remembers slow models and survives a reload")
{
	std::string dir = tempDir("cap_store");
	std::string path = fs::joinPath(dir, "capability.json");
	const std::string key = CapabilityStore::key("Test GPU", "directml");
	{
		CapabilityStore s(path);
		CHECK_FALSE(s.load()); // no file yet
		CHECK(s.size() == 0);
		s.record(key, "fast_model", 12.0, 33.0);
		// One slow measurement is not enough to blacklist a model.
		s.record(key, "slow_model", 400.0, 33.0);
		CHECK_FALSE(s.isTooSlow(key, "slow_model"));
		for (int i = 1; i < CapabilityStore::kSlowStrikes; ++i)
			s.record(key, "slow_model", 400.0, 33.0);
		CHECK(s.size() == 2);
		CHECK_FALSE(s.isTooSlow(key, "fast_model"));
		CHECK(s.isTooSlow(key, "slow_model"));
		CHECK(s.get(key, "fast_model").costMs == doctest::Approx(12.0));
		CHECK_FALSE(s.has(key, "unknown"));
		CHECK_FALSE(s.isTooSlow(CapabilityStore::key("Other GPU", "cpu"), "slow_model"));
	}
	{
		CapabilityStore s(path);
		REQUIRE(s.load());
		CHECK(s.size() == 2);
		CHECK(s.isTooSlow(key, "slow_model"));
		// A measurement inside budget rehabilitates it (conditions change).
		s.record(key, "slow_model", 10.0, 33.0);
		CHECK_FALSE(s.isTooSlow(key, "slow_model"));
		// ... and repeated slowness blacklists it again.
		for (int i = 0; i < CapabilityStore::kSlowStrikes; ++i)
			s.record(key, "slow_model", 400.0, 33.0);
		CHECK(s.isTooSlow(key, "slow_model"));
	}
	{
		CapabilityStore s(path);
		CHECK(s.load());
		CHECK(s.isTooSlow(key, "slow_model"));
		s.clear();
		CHECK(s.size() == 0);
	}
	// A corrupt file must not throw.
	fs::writeTextFile(path, "{not json");
	CapabilityStore bad(path);
	CHECK_FALSE(bad.load());
	CHECK(bad.size() == 0);
}

TEST_CASE("model selection prefers cheap models first and climbs")
{
	std::string bundled = tempDir("sel_bundled");
	std::string user = tempDir("sel_user");
	const char *manifest = R"({"models":[
	  {"id":"cheap","file_name":"cheap.onnx","license":"MIT","bundled":true,"quality_rank":45,"cost_rank":10,
	   "recommended_for_cpu":true,"tiers":[{"width":8,"height":8}]},
	  {"id":"mid","file_name":"mid.onnx","license":"MIT","bundled":true,"quality_rank":58,"cost_rank":15,
	   "tiers":[{"width":8,"height":8}]},
	  {"id":"heavy","file_name":"heavy.onnx","license":"MIT","bundled":true,"quality_rank":90,"cost_rank":60,
	   "recommended_for_gpu":true,"tiers":[{"width":8,"height":8}]}
	]})";
	for (const char *f : {"cheap.onnx", "mid.onnx", "heavy.onnx"})
		REQUIRE(fs::writeTextFile(fs::joinPath(bundled, f), "x"));
	ModelManager mm(bundled, user);
	std::string err;
	REQUIRE(mm.loadManifestText(manifest, err));

	// Auto starts on the cheapest model, whatever the backend.
	auto start = mm.selectModel(true, true, nullptr);
	REQUIRE(start.has_value());
	CHECK(start->id == "cheap");
	// Climbing goes one rung at a time.
	auto up1 = mm.betterModel("cheap", true, nullptr);
	REQUIRE(up1.has_value());
	CHECK(up1->id == "mid");
	auto up2 = mm.betterModel("mid", true, nullptr);
	REQUIRE(up2.has_value());
	CHECK(up2->id == "heavy");
	CHECK_FALSE(mm.betterModel("heavy", true, nullptr).has_value());
	// Blocked models are skipped everywhere.
	auto blocked = [](const std::string &id) { return id == "mid"; };
	auto up3 = mm.betterModel("cheap", true, blocked);
	REQUIRE(up3.has_value());
	CHECK(up3->id == "heavy");
	auto start2 = mm.selectModel(true, true, [](const std::string &id) { return id == "cheap"; });
	REQUIRE(start2.has_value());
	CHECK(start2->id == "mid");
	// Quality-first selection picks the GPU-recommended model.
	auto best = mm.selectModel(true, false, nullptr);
	REQUIRE(best.has_value());
	CHECK(best->id == "heavy");
}

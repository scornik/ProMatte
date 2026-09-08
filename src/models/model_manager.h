#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "inference/model_descriptor.h"

namespace promatte {

struct ModelStatus {
	ModelDescriptor desc;
	bool installed = false;
	bool verified = false; // checksum matched (or no checksum to check)
	std::string path;
	std::string problem;
};

struct DownloadState {
	bool active = false;
	bool finished = false;
	bool succeeded = false;
	std::string modelId;
	uint64_t received = 0;
	uint64_t total = 0;
	std::string error;
};

// Knows which models are approved (manifest), which are installed (bundled data
// dir or per-user config dir), verifies checksums and downloads/deletes models.
// Thread-safe; downloads run on a background thread with cancellation.
class ModelManager {
public:
	ModelManager(std::string bundledDir, std::string userDir);
	~ModelManager();

	bool loadManifestFile(const std::string &path, std::string &error);
	bool loadManifestText(const std::string &json, std::string &error);

	std::vector<ModelStatus> list() const;
	std::optional<ModelDescriptor> find(const std::string &id) const;
	// Installed path for the model (user dir first, then bundled); empty when absent.
	std::string resolvePath(const std::string &id) const;
	bool isInstalled(const std::string &id) const { return !resolvePath(id).empty(); }
	bool verify(const std::string &id, std::string &problem) const;
	bool remove(const std::string &id, std::string &error);
	bool installFromFile(const std::string &sourcePath, const std::string &id, std::string &error);

	// Registers a user-supplied ONNX file as a custom model (no checksum).
	std::optional<ModelDescriptor> registerCustomModel(const std::string &path, const ModelDescriptor &tmpl);

	bool startDownload(const std::string &id);
	void cancelDownload();
	DownloadState downloadState() const;
	// Blocks until the current download ends (tests / shutdown).
	void waitForDownload();

	// Picks the default model for the given backend type among installed models.
	// `blocked` model ids (known too slow on this device) are skipped, and when
	// `preferCheap` is set the cheapest recommended model wins - Auto starts there
	// and climbs only when measurements show headroom.
	std::optional<ModelDescriptor> defaultModel(bool gpuBackend) const;
	std::optional<ModelDescriptor> selectModel(bool gpuBackend, bool preferCheap,
						   const std::function<bool(const std::string &)> &blocked) const;
	// Next better-quality installed model that is not blocked, or nullopt.
	std::optional<ModelDescriptor> betterModel(const std::string &currentId, bool gpuBackend,
						   const std::function<bool(const std::string &)> &blocked) const;

	const std::string &userDir() const { return userDir_; }
	const std::string &bundledDir() const { return bundledDir_; }

private:
	std::string candidatePath(const ModelDescriptor &d, bool user) const;
	void downloadThread(ModelDescriptor d);

	std::string bundledDir_;
	std::string userDir_;
	mutable std::mutex m_;
	std::vector<ModelDescriptor> models_;
	std::thread downloadThread_;
	std::atomic<bool> cancel_{false};
	mutable std::mutex dlMutex_;
	DownloadState dl_;
};

} // namespace promatte

#include "models/model_manager.h"

#include <algorithm>

#include "models/model_downloader.h"
#include "utils/file_utils.h"
#include "utils/logging.h"
#include "utils/sha256.h"

namespace promatte {

ModelManager::ModelManager(std::string bundledDir, std::string userDir)
	: bundledDir_(std::move(bundledDir)),
	  userDir_(std::move(userDir))
{
}

ModelManager::~ModelManager()
{
	cancelDownload();
	waitForDownload();
}

bool ModelManager::loadManifestFile(const std::string &path, std::string &error)
{
	std::string text;
	if (!fs::readTextFile(path, text)) {
		error = "cannot read manifest " + path;
		return false;
	}
	return loadManifestText(text, error);
}

bool ModelManager::loadManifestText(const std::string &json, std::string &error)
{
	std::vector<ModelDescriptor> parsed;
	if (!parseModelManifest(json, parsed, error))
		return false;
	std::lock_guard<std::mutex> lock(m_);
	// keep custom (user-registered) models
	std::vector<ModelDescriptor> custom;
	for (const auto &m : models_)
		if (m.family == "custom")
			custom.push_back(m);
	models_ = std::move(parsed);
	for (auto &c : custom)
		models_.push_back(c);
	return true;
}

std::string ModelManager::candidatePath(const ModelDescriptor &d, bool user) const
{
	if (d.family == "custom")
		return d.downloadUrl; // custom models store their absolute path here
	return fs::joinPath(user ? userDir_ : bundledDir_, d.fileName);
}

std::vector<ModelStatus> ModelManager::list() const
{
	std::lock_guard<std::mutex> lock(m_);
	std::vector<ModelStatus> out;
	for (const auto &d : models_) {
		ModelStatus s;
		s.desc = d;
		std::string p = candidatePath(d, true);
		if (d.family != "custom" && !fs::exists(p))
			p = candidatePath(d, false);
		if (fs::exists(p)) {
			s.installed = true;
			s.path = p;
			s.verified = true; // full checksum verification is done on demand (verify())
		}
		out.push_back(s);
	}
	return out;
}

std::optional<ModelDescriptor> ModelManager::find(const std::string &id) const
{
	std::lock_guard<std::mutex> lock(m_);
	for (const auto &d : models_)
		if (d.id == id)
			return d;
	return std::nullopt;
}

std::string ModelManager::resolvePath(const std::string &id) const
{
	auto d = find(id);
	if (!d)
		return {};
	std::string p = candidatePath(*d, true);
	if (fs::exists(p))
		return p;
	if (d->family == "custom")
		return {};
	p = candidatePath(*d, false);
	return fs::exists(p) ? p : std::string{};
}

bool ModelManager::verify(const std::string &id, std::string &problem) const
{
	auto d = find(id);
	if (!d) {
		problem = "unknown model";
		return false;
	}
	std::string p = resolvePath(id);
	if (p.empty()) {
		problem = "not installed";
		return false;
	}
	if (d->sizeBytes && fs::fileSize(p) != d->sizeBytes) {
		problem = "size mismatch";
		return false;
	}
	if (!d->sha256.empty()) {
		std::string h = Sha256::hashFile(p);
		if (h != d->sha256) {
			problem = "checksum mismatch";
			return false;
		}
	}
	return true;
}

bool ModelManager::remove(const std::string &id, std::string &error)
{
	auto d = find(id);
	if (!d) {
		error = "unknown model";
		return false;
	}
	if (d->family == "custom") {
		std::lock_guard<std::mutex> lock(m_);
		models_.erase(std::remove_if(models_.begin(), models_.end(), [&](const ModelDescriptor &m) { return m.id == id; }),
			      models_.end());
		return true;
	}
	std::string p = candidatePath(*d, true);
	if (!fs::exists(p)) {
		if (fs::exists(candidatePath(*d, false))) {
			error = "bundled models cannot be deleted";
			return false;
		}
		error = "not installed";
		return false;
	}
	if (!fs::removeFile(p)) {
		error = "could not delete " + p;
		return false;
	}
	PM_LOG_INFO("model '%s' deleted (%s)", id.c_str(), p.c_str());
	return true;
}

bool ModelManager::installFromFile(const std::string &sourcePath, const std::string &id, std::string &error)
{
	auto d = find(id);
	if (!d) {
		error = "unknown model";
		return false;
	}
	if (!fs::exists(sourcePath)) {
		error = "file not found";
		return false;
	}
	if (!d->sha256.empty() && Sha256::hashFile(sourcePath) != d->sha256) {
		error = "checksum mismatch - this is not the approved model file";
		return false;
	}
	if (!fs::createDirectories(userDir_)) {
		error = "cannot create " + userDir_;
		return false;
	}
	std::string dest = candidatePath(*d, true);
	std::string content;
	if (!fs::readTextFile(sourcePath, content) || !fs::writeTextFile(dest + ".part", content) ||
	    !(fs::removeFile(dest), fs::renameFile(dest + ".part", dest))) {
		error = "copy failed";
		return false;
	}
	return true;
}

std::optional<ModelDescriptor> ModelManager::registerCustomModel(const std::string &path, const ModelDescriptor &tmpl)
{
	if (!fs::exists(path))
		return std::nullopt;
	ModelDescriptor d = tmpl;
	d.family = "custom";
	d.id = "custom:" + Sha256::hashString(path).substr(0, 12);
	d.displayName = "Custom: " + fs::fileName(path);
	d.fileName = fs::fileName(path);
	d.downloadUrl = path;
	d.sha256.clear();
	d.sizeBytes = 0;
	d.bundled = false;
	std::lock_guard<std::mutex> lock(m_);
	for (auto &m : models_)
		if (m.id == d.id) {
			m = d;
			return d;
		}
	models_.push_back(d);
	return d;
}

bool ModelManager::startDownload(const std::string &id)
{
	auto d = find(id);
	if (!d || d->downloadUrl.empty() || d->family == "custom")
		return false;
	{
		std::lock_guard<std::mutex> lock(dlMutex_);
		if (dl_.active)
			return false;
		dl_ = DownloadState{};
		dl_.active = true;
		dl_.modelId = id;
		dl_.total = d->sizeBytes;
	}
	waitForDownload(); // join a previous finished thread
	cancel_ = false;
	downloadThread_ = std::thread([this, m = *d] { downloadThread(m); });
	return true;
}

void ModelManager::downloadThread(ModelDescriptor d)
{
	std::string error;
	bool ok = false;
	if (!fs::createDirectories(userDir_)) {
		error = "cannot create " + userDir_;
	} else {
		std::string dest = fs::joinPath(userDir_, d.fileName);
		PM_LOG_INFO("downloading model '%s' from %s", d.id.c_str(), d.downloadUrl.c_str());
		ok = downloadFile(
			d.downloadUrl, dest,
			[this](uint64_t received, uint64_t total) {
				std::lock_guard<std::mutex> lock(dlMutex_);
				dl_.received = received;
				if (total)
					dl_.total = total;
				return !cancel_.load();
			},
			error);
		if (ok && !d.sha256.empty()) {
			std::string h = Sha256::hashFile(dest);
			if (h != d.sha256) {
				error = "checksum mismatch (expected " + d.sha256.substr(0, 12) + "..., got " + h.substr(0, 12) +
					"...)";
				fs::removeFile(dest);
				ok = false;
			}
		}
		if (ok)
			PM_LOG_INFO("model '%s' installed at %s", d.id.c_str(), dest.c_str());
		else
			PM_LOG_ERROR("model '%s' download failed: %s", d.id.c_str(), error.c_str());
	}
	std::lock_guard<std::mutex> lock(dlMutex_);
	dl_.active = false;
	dl_.finished = true;
	dl_.succeeded = ok;
	dl_.error = error;
}

void ModelManager::cancelDownload()
{
	cancel_ = true;
}

DownloadState ModelManager::downloadState() const
{
	std::lock_guard<std::mutex> lock(dlMutex_);
	return dl_;
}

void ModelManager::waitForDownload()
{
	if (downloadThread_.joinable())
		downloadThread_.join();
}

std::optional<ModelDescriptor> ModelManager::defaultModel(bool gpuBackend) const
{
	return selectModel(gpuBackend, /*preferCheap=*/true, nullptr);
}

std::optional<ModelDescriptor> ModelManager::selectModel(bool gpuBackend, bool preferCheap,
							 const std::function<bool(const std::string &)> &blocked) const
{
	auto all = list();
	const ModelDescriptor *best = nullptr;
	int bestScore = -1;
	for (const auto &s : all) {
		if (!s.installed || s.desc.family == "custom")
			continue;
		if (blocked && blocked(s.desc.id))
			continue;
		int score;
		if (preferCheap) {
			// Cheapest first, quality as the tie-breaker.
			score = 200 - s.desc.costRank * 2 + s.desc.qualityRank / 10;
			if (s.desc.recommendedForCpu)
				score += 20;
		} else {
			score = s.desc.qualityRank;
			if (gpuBackend && s.desc.recommendedForGpu)
				score += 100;
			if (!gpuBackend && s.desc.recommendedForCpu)
				score += 100;
			if (!gpuBackend)
				score -= s.desc.costRank;
		}
		if (score > bestScore) {
			bestScore = score;
			best = &s.desc;
		}
	}
	if (!best)
		return std::nullopt;
	return *best;
}

std::optional<ModelDescriptor> ModelManager::betterModel(const std::string &currentId, bool gpuBackend,
							 const std::function<bool(const std::string &)> &blocked) const
{
	auto cur = find(currentId);
	const int curQuality = cur ? cur->qualityRank : -1;
	const int curCost = cur ? cur->costRank : -1;
	const ModelDescriptor *best = nullptr;
	int bestCost = 1 << 30;
	// list() returns by value: keep it alive until after the loop.
	const std::vector<ModelStatus> all = list();
	for (const auto &s : all) {
		if (!s.installed || s.desc.family == "custom" || s.desc.id == currentId)
			continue;
		if (blocked && blocked(s.desc.id))
			continue;
		if (s.desc.qualityRank <= curQuality || s.desc.costRank <= curCost)
			continue;
		// The cheapest of the better models: climb one rung at a time.
		if (s.desc.costRank < bestCost) {
			bestCost = s.desc.costRank;
			best = &s.desc;
		}
	}
	(void)gpuBackend;
	if (!best)
		return std::nullopt;
	return *best;
}

} // namespace promatte

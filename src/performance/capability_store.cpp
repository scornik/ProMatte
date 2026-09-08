#include "performance/capability_store.h"

#include <algorithm>
#include <chrono>

#include <nlohmann/json.hpp>

#include "utils/file_utils.h"
#include "utils/logging.h"

namespace promatte {

using json = nlohmann::json;

namespace {
int64_t nowSeconds()
{
	return int64_t(std::chrono::duration_cast<std::chrono::seconds>(
			       std::chrono::system_clock::now().time_since_epoch())
			       .count());
}
} // namespace

CapabilityStore::CapabilityStore(std::string path) : path_(std::move(path)) {}

std::string CapabilityStore::key(const std::string &device, const std::string &backend)
{
	return device + "|" + backend;
}

bool CapabilityStore::has(const std::string &k, const std::string &modelId) const
{
	std::lock_guard<std::mutex> lock(m_);
	auto it = data_.find(k);
	return it != data_.end() && it->second.count(modelId) > 0;
}

CapabilityStore::Record CapabilityStore::get(const std::string &k, const std::string &modelId) const
{
	std::lock_guard<std::mutex> lock(m_);
	auto it = data_.find(k);
	if (it == data_.end())
		return {};
	auto jt = it->second.find(modelId);
	return jt == it->second.end() ? Record{} : jt->second;
}

bool CapabilityStore::isTooSlow(const std::string &k, const std::string &modelId) const
{
	return get(k, modelId).tooSlow;
}

void CapabilityStore::record(const std::string &k, const std::string &modelId, double costMs, double budgetMs)
{
	{
		std::lock_guard<std::mutex> lock(m_);
		Record &r = data_[k][modelId];
		const bool slow = budgetMs > 0 && costMs > budgetMs * kSlowFactor;
		if (slow) {
			r.slowCount = std::min(r.slowCount + 1, kSlowStrikes * 2);
			if (r.slowCount >= kSlowStrikes)
				r.tooSlow = true;
		} else {
			// A measurement inside the budget rehabilitates the model: conditions
			// change (other GPU load, a smaller camera, a different scene).
			r.slowCount = 0;
			if (budgetMs > 0 && costMs <= budgetMs)
				r.tooSlow = false;
		}
		r.costMs = costMs;
		r.budgetMs = budgetMs;
		r.updated = nowSeconds();
		dirty_ = true;
	}
	save();
}

bool CapabilityStore::load()
{
	std::string text;
	if (path_.empty() || !fs::readTextFile(path_, text))
		return false;
	json root;
	try {
		root = json::parse(text);
	} catch (const std::exception &e) {
		PM_LOG_WARN("capability store unreadable (%s); starting fresh", e.what());
		return false;
	}
	std::lock_guard<std::mutex> lock(m_);
	data_.clear();
	if (!root.is_object())
		return false;
	for (auto &[device, models] : root.items()) {
		if (!models.is_object())
			continue;
		for (auto &[model, rec] : models.items()) {
			if (!rec.is_object())
				continue;
			Record r;
			r.costMs = rec.value("cost_ms", 0.0);
			r.budgetMs = rec.value("budget_ms", 0.0);
			r.tooSlow = rec.value("too_slow", false);
			r.slowCount = rec.value("slow_count", 0);
			r.updated = rec.value("updated", int64_t(0));
			data_[device][model] = r;
		}
	}
	return true;
}

bool CapabilityStore::save() const
{
	if (path_.empty())
		return false;
	json root = json::object();
	{
		std::lock_guard<std::mutex> lock(m_);
		if (!dirty_)
			return true;
		for (const auto &[device, models] : data_) {
			for (const auto &[model, r] : models) {
				root[device][model] = {{"cost_ms", r.costMs},
						       {"budget_ms", r.budgetMs},
						       {"too_slow", r.tooSlow},
						       {"slow_count", r.slowCount},
						       {"updated", r.updated}};
			}
		}
		dirty_ = false;
	}
	std::string dir = path_;
	size_t slash = dir.find_last_of("/\\");
	if (slash != std::string::npos)
		fs::createDirectories(dir.substr(0, slash));
	return fs::writeTextFile(path_, root.dump(2));
}

void CapabilityStore::clear()
{
	std::lock_guard<std::mutex> lock(m_);
	data_.clear();
	dirty_ = true;
}

size_t CapabilityStore::size() const
{
	std::lock_guard<std::mutex> lock(m_);
	size_t n = 0;
	for (const auto &[device, models] : data_)
		n += models.size();
	return n;
}

} // namespace promatte

#pragma once
#include <map>
#include <mutex>
#include <string>

namespace promatte {

// Remembers how expensive each (device, backend, model) combination actually is
// on this machine so "Auto" does not have to re-learn after every restart.
//
// Records are written by the filter when the worker has measured a model for a
// few seconds and read when Auto picks a model. Stored as JSON in the plugin's
// per-user config directory; a missing or corrupt file is simply ignored.
class CapabilityStore {
public:
	struct Record {
		double costMs = 0;   // worker time per frame at the lowest tier (latest)
		double budgetMs = 0; // budget in force when it was measured
		bool tooSlow = false;
		int slowCount = 0;   // consecutive measurements far over budget
		int64_t updated = 0; // unix seconds
	};

	// A model is only blacklisted after this many consecutive measurements above
	// `slowFactor` x budget, so one transient stall does not disable it forever.
	static constexpr int kSlowStrikes = 3;
	static constexpr double kSlowFactor = 2.0;

	explicit CapabilityStore(std::string path);

	// Key: device name + backend, e.g. "NVIDIA GeForce 940MX|directml".
	static std::string key(const std::string &device, const std::string &backend);

	bool has(const std::string &key, const std::string &modelId) const;
	Record get(const std::string &key, const std::string &modelId) const;
	// Returns true when this model is known to be unusable on this device.
	bool isTooSlow(const std::string &key, const std::string &modelId) const;
	void record(const std::string &key, const std::string &modelId, double costMs, double budgetMs);

	bool load();
	bool save() const;
	void clear();
	size_t size() const;

private:
	std::string path_;
	mutable std::mutex m_;
	std::map<std::string, std::map<std::string, Record>> data_;
	mutable bool dirty_ = false;
};

} // namespace promatte

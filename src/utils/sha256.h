#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace promatte {

// Small, dependency-free SHA-256 (FIPS 180-4). Used for model checksum validation.
class Sha256 {
public:
	Sha256();
	void update(const void *data, size_t len);
	std::array<uint8_t, 32> finish();
	static std::string hex(const std::array<uint8_t, 32> &digest);
	static std::string hashString(const std::string &s);
	// Returns lowercase hex digest of file contents, or empty string on I/O error.
	static std::string hashFile(const std::string &path);

private:
	void transform(const uint8_t block[64]);
	uint32_t state_[8];
	uint64_t bitlen_ = 0;
	uint8_t buffer_[64];
	size_t bufferLen_ = 0;
};

} // namespace promatte

#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace promatte {

// Progress callback: return false to cancel. total may be 0 when unknown.
using DownloadProgress = std::function<bool(uint64_t received, uint64_t total)>;

// Downloads `url` to `destPath` (written via a temporary file and renamed on
// success). Follows redirects, HTTPS only. Uses WinHTTP on Windows.
// Returns false and fills `error` on failure or cancellation.
bool downloadFile(const std::string &url, const std::string &destPath, const DownloadProgress &progress,
		  std::string &error);

} // namespace promatte

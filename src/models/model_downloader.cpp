#include "models/model_downloader.h"

#include <cstdio>
#include <vector>

#include "utils/file_utils.h"
#include "utils/logging.h"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace promatte {

#ifdef _WIN32

namespace {

struct HInternet {
	HINTERNET h = nullptr;
	explicit HInternet(HINTERNET handle) : h(handle) {}
	~HInternet()
	{
		if (h)
			WinHttpCloseHandle(h);
	}
	explicit operator bool() const { return h != nullptr; }
};

std::string lastErrorString(const char *what)
{
	DWORD code = GetLastError();
	char buf[256];
	std::snprintf(buf, sizeof(buf), "%s failed (error %lu)", what, static_cast<unsigned long>(code));
	return buf;
}

} // namespace

bool downloadFile(const std::string &url, const std::string &destPath, const DownloadProgress &progress,
		  std::string &error)
{
	std::wstring wurl = fs::toWide(url);
	URL_COMPONENTS uc{};
	uc.dwStructSize = sizeof(uc);
	wchar_t host[256]{}, path[2048]{};
	uc.lpszHostName = host;
	uc.dwHostNameLength = 256;
	uc.lpszUrlPath = path;
	uc.dwUrlPathLength = 2048;
	if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
		error = "invalid URL";
		return false;
	}
	if (uc.nScheme != INTERNET_SCHEME_HTTPS) {
		error = "only https:// downloads are allowed";
		return false;
	}

	HInternet session(WinHttpOpen(L"ProMatte/1.0 (OBS Studio plugin)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
				      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if (!session) {
		error = lastErrorString("WinHttpOpen");
		return false;
	}
	WinHttpSetTimeouts(session.h, 15000, 15000, 30000, 60000);
	DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
	WinHttpSetOption(session.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
	DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
	WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

	HInternet connect(WinHttpConnect(session.h, host, uc.nPort, 0));
	if (!connect) {
		error = lastErrorString("WinHttpConnect");
		return false;
	}
	HInternet request(WinHttpOpenRequest(connect.h, L"GET", path, nullptr, WINHTTP_NO_REFERER,
					     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
	if (!request) {
		error = lastErrorString("WinHttpOpenRequest");
		return false;
	}
	if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
	    !WinHttpReceiveResponse(request.h, nullptr)) {
		error = lastErrorString("HTTP request");
		return false;
	}
	DWORD status = 0, size = sizeof(status);
	WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
			    &status, &size, WINHTTP_NO_HEADER_INDEX);
	if (status != 200) {
		error = "HTTP status " + std::to_string(status);
		return false;
	}
	uint64_t total = 0;
	{
		wchar_t lenBuf[64]{};
		DWORD lenSize = sizeof(lenBuf);
		if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenBuf,
					&lenSize, WINHTTP_NO_HEADER_INDEX))
			total = _wtoi64(lenBuf);
	}

	std::string tmpPath = destPath + ".part";
	FILE *f = _wfopen(fs::toWide(tmpPath).c_str(), L"wb");
	if (!f) {
		error = "cannot create " + tmpPath;
		return false;
	}
	std::vector<uint8_t> buf(256 * 1024);
	uint64_t received = 0;
	bool ok = true;
	for (;;) {
		DWORD avail = 0;
		if (!WinHttpQueryDataAvailable(request.h, &avail)) {
			error = lastErrorString("WinHttpQueryDataAvailable");
			ok = false;
			break;
		}
		if (avail == 0)
			break;
		while (avail > 0) {
			DWORD toRead = std::min<DWORD>(avail, DWORD(buf.size()));
			DWORD got = 0;
			if (!WinHttpReadData(request.h, buf.data(), toRead, &got)) {
				error = lastErrorString("WinHttpReadData");
				ok = false;
				break;
			}
			if (got == 0)
				break;
			if (std::fwrite(buf.data(), 1, got, f) != got) {
				error = "disk write failed";
				ok = false;
				break;
			}
			received += got;
			avail -= got;
			if (progress && !progress(received, total)) {
				error = "cancelled";
				ok = false;
				break;
			}
		}
		if (!ok)
			break;
	}
	std::fclose(f);
	if (ok && total != 0 && received != total) {
		error = "incomplete download";
		ok = false;
	}
	if (!ok) {
		fs::removeFile(tmpPath);
		return false;
	}
	fs::removeFile(destPath);
	if (!fs::renameFile(tmpPath, destPath)) {
		error = "cannot move downloaded file into place";
		fs::removeFile(tmpPath);
		return false;
	}
	return true;
}

#else // ---------------------------------------------------------------- POSIX

namespace {

struct WriteCtx {
	std::FILE *file = nullptr;
	const DownloadProgress *progress = nullptr;
	uint64_t received = 0;
	uint64_t total = 0;
	bool cancelled = false;
	bool writeFailed = false;
};

size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	auto *ctx = static_cast<WriteCtx *>(userdata);
	const size_t bytes = size * nmemb;
	if (std::fwrite(ptr, 1, bytes, ctx->file) != bytes) {
		ctx->writeFailed = true;
		return 0; // aborts the transfer
	}
	ctx->received += bytes;
	if (ctx->progress && *ctx->progress && !(*ctx->progress)(ctx->received, ctx->total)) {
		ctx->cancelled = true;
		return 0;
	}
	return bytes;
}

int progressCallback(void *clientp, curl_off_t dltotal, curl_off_t, curl_off_t, curl_off_t)
{
	auto *ctx = static_cast<WriteCtx *>(clientp);
	if (dltotal > 0)
		ctx->total = uint64_t(dltotal);
	return ctx->cancelled ? 1 : 0;
}

} // namespace

bool downloadFile(const std::string &url, const std::string &destPath, const DownloadProgress &progress,
		  std::string &error)
{
	if (url.rfind("https://", 0) != 0) {
		error = "only https:// downloads are allowed";
		return false;
	}
	CURL *curl = curl_easy_init();
	if (!curl) {
		error = "could not initialise libcurl";
		return false;
	}
	const std::string tmpPath = destPath + ".part";
	std::FILE *f = std::fopen(tmpPath.c_str(), "wb");
	if (!f) {
		curl_easy_cleanup(curl);
		error = "cannot create " + tmpPath;
		return false;
	}
	WriteCtx ctx;
	ctx.file = f;
	ctx.progress = &progress;

	char errbuf[CURL_ERROR_SIZE]{};
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
	// Redirects must stay on HTTPS: a model must never be fetched over plain HTTP.
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "ProMatte/1.0 (OBS Studio plugin)");
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

	const CURLcode rc = curl_easy_perform(curl);
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	std::fclose(f);

	bool ok = rc == CURLE_OK;
	if (!ok) {
		if (ctx.cancelled)
			error = "cancelled";
		else if (ctx.writeFailed)
			error = "disk write failed";
		else
			error = errbuf[0] ? errbuf : curl_easy_strerror(rc);
	} else if (status != 200) {
		error = "HTTP status " + std::to_string(status);
		ok = false;
	} else if (ctx.total != 0 && ctx.received != ctx.total) {
		error = "incomplete download";
		ok = false;
	}
	curl_easy_cleanup(curl);

	if (!ok) {
		fs::removeFile(tmpPath);
		return false;
	}
	fs::removeFile(destPath);
	if (!fs::renameFile(tmpPath, destPath)) {
		error = "cannot move downloaded file into place";
		fs::removeFile(tmpPath);
		return false;
	}
	return true;
}

#endif

} // namespace promatte

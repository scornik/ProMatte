#include "models/model_downloader.h"

#include <cstdio>
#include <vector>

#include "utils/file_utils.h"
#include "utils/logging.h"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
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

#else

bool downloadFile(const std::string &, const std::string &, const DownloadProgress &, std::string &error)
{
	error = "model download is not implemented on this platform yet; place the model file manually";
	return false;
}

#endif

} // namespace promatte

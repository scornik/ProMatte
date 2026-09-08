#include "utils/file_utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace promatte::fs {

namespace stdfs = std::filesystem;

namespace {
stdfs::path P(const std::string &utf8)
{
#ifdef _WIN32
	return stdfs::path(toWide(utf8));
#else
	return stdfs::path(utf8);
#endif
}
std::string S(const stdfs::path &p)
{
#ifdef _WIN32
	return fromWide(p.wstring());
#else
	return p.string();
#endif
}
} // namespace

bool exists(const std::string &path)
{
	std::error_code ec;
	return stdfs::exists(P(path), ec);
}

bool isDirectory(const std::string &path)
{
	std::error_code ec;
	return stdfs::is_directory(P(path), ec);
}

uint64_t fileSize(const std::string &path)
{
	std::error_code ec;
	auto s = stdfs::file_size(P(path), ec);
	return ec ? 0 : uint64_t(s);
}

bool createDirectories(const std::string &path)
{
	std::error_code ec;
	stdfs::create_directories(P(path), ec);
	return !ec || isDirectory(path);
}

bool removeFile(const std::string &path)
{
	std::error_code ec;
	return stdfs::remove(P(path), ec);
}

bool renameFile(const std::string &from, const std::string &to)
{
	std::error_code ec;
	stdfs::rename(P(from), P(to), ec);
	return !ec;
}

bool readTextFile(const std::string &path, std::string &out)
{
	std::ifstream in(P(path), std::ios::binary);
	if (!in)
		return false;
	std::ostringstream ss;
	ss << in.rdbuf();
	out = ss.str();
	return true;
}

bool writeTextFile(const std::string &path, const std::string &content)
{
	std::ofstream out(P(path), std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out << content;
	return bool(out);
}

std::string joinPath(const std::string &a, const std::string &b)
{
	if (a.empty())
		return b;
	if (b.empty())
		return a;
	char last = a.back();
	if (last == '/' || last == '\\')
		return a + b;
	return a + "/" + b;
}

std::string fileName(const std::string &path)
{
	return S(P(path).filename());
}

std::string extension(const std::string &path)
{
	std::string e = S(P(path).extension());
	std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	return e;
}

std::vector<std::string> listFiles(const std::string &dir)
{
	std::vector<std::string> out;
	std::error_code ec;
	for (auto &entry : stdfs::directory_iterator(P(dir), ec)) {
		if (entry.is_regular_file(ec))
			out.push_back(S(entry.path()));
	}
	std::sort(out.begin(), out.end());
	return out;
}

std::wstring toWide(const std::string &utf8)
{
#ifdef _WIN32
	if (utf8.empty())
		return {};
	int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
	std::wstring w(size_t(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), w.data(), n);
	return w;
#else
	return std::wstring(utf8.begin(), utf8.end());
#endif
}

std::string fromWide(const std::wstring &wide)
{
#ifdef _WIN32
	if (wide.empty())
		return {};
	int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), nullptr, 0, nullptr, nullptr);
	std::string s(size_t(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), s.data(), n, nullptr, nullptr);
	return s;
#else
	return std::string(wide.begin(), wide.end());
#endif
}

} // namespace promatte::fs

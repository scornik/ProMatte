#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace promatte::fs {

bool exists(const std::string &path);
bool isDirectory(const std::string &path);
uint64_t fileSize(const std::string &path);
bool createDirectories(const std::string &path);
bool removeFile(const std::string &path);
bool renameFile(const std::string &from, const std::string &to);
bool readTextFile(const std::string &path, std::string &out);
bool writeTextFile(const std::string &path, const std::string &content);
std::string joinPath(const std::string &a, const std::string &b);
std::string fileName(const std::string &path);
std::string extension(const std::string &path); // lowercase, includes dot
std::vector<std::string> listFiles(const std::string &dir);
std::wstring toWide(const std::string &utf8);
std::string fromWide(const std::wstring &wide);

} // namespace promatte::fs

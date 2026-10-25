#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace coop {

std::wstring Utf8ToWide(std::string_view s);
std::string WideToUtf8(std::wstring_view s);

// Monotonic milliseconds since an arbitrary point (QueryPerformanceCounter based).
uint64_t NowMs();
double NowSeconds();

// Directory containing the given module (or the executable for nullptr), with trailing backslash.
std::wstring ModuleDir(void* module = nullptr);

bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out);
// durable: on the disk before it replaces the old file (a profile); a world the host uploads again can skip the wait
bool WriteFileBytes(const std::wstring& path, const void* data, size_t size, bool durable = true);
bool FileExists(const std::wstring& path);
void EnsureDirectory(const std::wstring& path);

uint32_t Crc32(const void* data, size_t size, uint32_t crc = 0);

std::string Trim(std::string_view s);
std::string ToLower(std::string_view s);
std::vector<std::string> Split(std::string_view s, char sep);
// printf-checked: both texts of TR(...) get checked against the arguments
std::string Format(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Keeps only characters allowed in a nickname (letters incl. Cyrillic, digits, _-.[]) and limits length.
std::string SanitizeNickname(std::string_view nick);
// CON, PRN, AUX, NUL, COM1-9, LPT1-9 (with any extension): DOS devices, not files, on Windows 10
bool ReservedFileName(std::string_view name);

}  // namespace coop

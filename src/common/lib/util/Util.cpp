#include "common/lib/util/Util.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace coop {

std::wstring Utf8ToWide(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

std::string WideToUtf8(std::wstring_view s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

static double QpcPeriodMs() {
    static double period = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return 1000.0 / (double)f.QuadPart;
    }();
    return period;
}

uint64_t NowMs() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * QpcPeriodMs());
}

double NowSeconds() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * QpcPeriodMs() / 1000.0;
}

std::wstring ModuleDir(void* module) {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW((HMODULE)module, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    std::wstring p(buf, n);
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L".\\" : p.substr(0, slash + 1);
}

bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart > 512ll * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize((size_t)size.QuadPart);
    DWORD read = 0;
    bool ok = out.empty() || (ReadFile(h, out.data(), (DWORD)out.size(), &read, nullptr) && read == out.size());
    CloseHandle(h);
    return ok;
}

bool WriteFileBytes(const std::wstring& path, const void* data, size_t size, bool durable) {
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = size == 0 || (WriteFile(h, data, (DWORD)size, &written, nullptr) && written == size);
    ok = ok && (!durable || FlushFileBuffers(h));  // a power cut left an empty profile
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool FileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

void EnsureDirectory(const std::wstring& path) {
    std::wstring cur;
    for (size_t i = 0; i < path.size(); ++i) {
        wchar_t c = path[i];
        cur += c;
        if ((c == L'\\' || c == L'/') && cur.size() > 3) CreateDirectoryW(cur.c_str(), nullptr);
    }
    if (!cur.empty()) CreateDirectoryW(cur.c_str(), nullptr);
}

uint32_t Crc32(const void* data, size_t size, uint32_t crc) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

std::string Trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return std::string(s.substr(a, b - a));
}

std::string ToLower(std::string_view s) {
    std::wstring w = Utf8ToWide(s);
    CharLowerBuffW(w.data(), (DWORD)w.size());
    return WideToUtf8(w);
}

std::vector<std::string> Split(std::string_view s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

std::string Format(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return {};
    if ((size_t)n < sizeof(buf)) return std::string(buf, n);
    std::string out(n, '\0');
    va_start(ap, fmt);
    vsnprintf(out.data(), n + 1, fmt, ap);
    va_end(ap);
    return out;
}

bool ReservedFileName(std::string_view name) {
    std::string n = ToLower(std::string(name.substr(0, name.find('.'))));
    static const char* const kDevices[] = {"con", "prn", "aux", "nul"};
    for (const char* d : kDevices)
        if (n == d) return true;
    return n.size() == 4 && (n.compare(0, 3, "com") == 0 || n.compare(0, 3, "lpt") == 0) && n[3] >= '1' && n[3] <= '9';
}

std::string SanitizeNickname(std::string_view nick) {
    std::wstring w = Utf8ToWide(Trim(nick));
    std::wstring out;
    for (wchar_t c : w) {
        bool ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ||
                  (c >= 0x0410 && c <= 0x044F) || c == 0x0401 || c == 0x0451 || c == L'_' || c == L'-' ||
                  c == L'.' || c == L'[' || c == L']';
        if (ok) out += c;
        if (out.size() >= 20) break;
    }
    return WideToUtf8(out);
}

}  // namespace coop

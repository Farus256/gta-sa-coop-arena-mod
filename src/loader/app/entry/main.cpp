// vorbisFile.dll — ASI loader for a game that has none (clean GTA San Andreas 1.0 US).
//
// gta_sa.exe imports vorbisFile.dll at start-up, so this is the earliest point to load plugins. The real
// vorbisfile functions are forwarded to the original DLL (renamed to vorbisHooked.dll by the installer,
// see vorbisFile.def). Like the common ASI loaders it loads every *.asi of the game folder and of
// "scripts\" in alphabetical order, so other mods added later work too.
#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

void LoadFrom(const std::wstring& dir) {
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*.asi").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) names.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    for (const std::wstring& name : names) {
        std::wstring path = dir + name;
        if (GetModuleHandleW(path.c_str())) continue;  // another loader may have it already
        if (!LoadLibraryW(path.c_str())) {
            std::wstring error = std::to_wstring(GetLastError());
            // no config of its own here: the language of Windows
            std::wstring msg = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN
                                   ? L"Не удалось загрузить плагин " + name + L" (ошибка " + error + L")."
                                   : L"Could not load the plugin " + name + L" (error " + error + L").";
            MessageBoxW(nullptr, msg.c_str(), L"ASI loader", MB_ICONWARNING);
        }
    }
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir = exe;
        dir.resize(dir.find_last_of(L"\\/") + 1);
        LoadFrom(dir);
        LoadFrom(dir + L"scripts\\");
    }
    return TRUE;
}

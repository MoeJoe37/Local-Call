#include "WindowsRuntimeGuard.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>

namespace {

std::wstring lastErrorMessage(DWORD code = GetLastError())
{
    wchar_t* buffer = nullptr;
    DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);
    std::wstring msg = size && buffer ? std::wstring(buffer, size) : L"Unknown Windows error";
    if (buffer) LocalFree(buffer);
    return msg;
}

void showFatal(const std::wstring& message)
{
    MessageBoxW(nullptr, message.c_str(), L"LocalCall runtime deployment error", MB_ICONERROR | MB_OK);
}

std::wstring directoryOf(const std::wstring& path)
{
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return path.substr(0, pos);
}

bool existsFile(const std::wstring& path)
{
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring quote(const std::wstring& s)
{
    return L"\"" + s + L"\"";
}

bool requireFile(const std::wstring& appDir, const std::wstring& name)
{
    const std::wstring path = appDir + L"\\" + name;
    if (!existsFile(path)) {
        showFatal(L"Missing runtime file:\n\n" + path +
                  L"\n\nThis build was not deployed correctly. Run:\n"
                  L"scripts\\deploy-windows.ps1 -Clean\n\n"
                  L"or use scripts\\fix-windows-entrypoint.ps1 -Rebuild.");
        return false;
    }
    return true;
}

bool requireExport(const std::wstring& dllPath, const char* symbol, const std::wstring& humanName)
{
    HMODULE mod = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!mod) {
        showFatal(L"Failed to load " + humanName + L":\n\n" + dllPath +
                  L"\n\nWindows error: " + lastErrorMessage());
        return false;
    }
    FARPROC proc = GetProcAddress(mod, symbol);
    if (!proc) {
        FreeLibrary(mod);
        showFatal(humanName + L" was found, but it is not ABI-compatible with this build:\n\n" +
                  dllPath +
                  L"\n\nThe deployed Qt DLL is from a different Qt kit/compiler, usually MinGW or another Qt version.\n\n"
                  L"Delete the build folder and redeploy with the exact MSVC kit used by CMake:\n"
                  L"C:/Qt/6.11.0/msvc2022_64");
        return false;
    }
    // Keep the verified module loaded for the lifetime of the process.
    return true;
}

void prependEnvironmentPath(const std::wstring& appDir, const std::wstring& qtBin)
{
    DWORD size = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    std::wstring current;
    if (size > 0) {
        current.resize(size);
        DWORD written = GetEnvironmentVariableW(L"PATH", current.data(), size);
        current.resize(written);
    }
    std::wstring value = appDir;
    if (!qtBin.empty()) value += L";" + qtBin;
    if (!current.empty()) value += L";" + current;
    SetEnvironmentVariableW(L"PATH", value.c_str());
}

} // namespace

bool localcall_prepare_windows_runtime()
{
    wchar_t exeBuffer[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, exeBuffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        showFatal(L"Could not resolve LocalCall.exe path. Windows error: " + lastErrorMessage());
        return false;
    }

    const std::wstring exePath(exeBuffer, len);
    const std::wstring appDir = directoryOf(exePath);
    const std::wstring qtBin = appDir; // deployed DLLs must live beside the EXE.

    // Avoid Qt/MinGW/old Qt DLLs from PATH/System locations. This is available
    // on supported Windows versions. SetDllDirectoryW is kept as a compatibility
    // belt-and-suspenders fallback.
    using SetDefaultDllDirectoriesFn = BOOL (WINAPI*)(DWORD);
    using AddDllDirectoryFn = DLL_DIRECTORY_COOKIE (WINAPI*)(PCWSTR);

    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    auto pSetDefaultDllDirectories = reinterpret_cast<SetDefaultDllDirectoriesFn>(
        GetProcAddress(kernel, "SetDefaultDllDirectories"));
    auto pAddDllDirectory = reinterpret_cast<AddDllDirectoryFn>(
        GetProcAddress(kernel, "AddDllDirectory"));

    if (pSetDefaultDllDirectories) {
        pSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                  LOAD_LIBRARY_SEARCH_USER_DIRS |
                                  LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (pAddDllDirectory) {
        pAddDllDirectory(appDir.c_str());
        const std::wstring platformsDir = appDir + L"\\platforms";
        pAddDllDirectory(platformsDir.c_str());
    }
    SetDllDirectoryW(appDir.c_str());
    prependEnvironmentPath(appDir, qtBin);

    const std::wstring platforms = appDir + L"\\platforms";
    SetEnvironmentVariableW(L"QT_PLUGIN_PATH", appDir.c_str());
    SetEnvironmentVariableW(L"QT_QPA_PLATFORM_PLUGIN_PATH", platforms.c_str());

    const std::vector<std::wstring> required = {
        L"Qt6Core.dll",
        L"Qt6Gui.dll",
        L"Qt6Widgets.dll",
        L"Qt6Network.dll",
        L"Qt6Concurrent.dll"
    };
    for (const auto& dll : required) {
        if (!requireFile(appDir, dll)) return false;
    }
    if (!requireFile(platforms, L"qwindows.dll")) return false;

    // The app no longer depends on QSlider. Entry-point errors are prevented by
    // deploying exact Qt DLLs beside the executable and by removing widgets that
    // caused fragile protected/virtual QSlider imports on mismatched Qt runtimes.

    return true;
}

#else
bool localcall_prepare_windows_runtime()
{
    return true;
}
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>
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
    std::wstring msg = (size && buffer) ? std::wstring(buffer, size) : L"Unknown Windows error";
    if (buffer) LocalFree(buffer);
    return msg;
}

void showFatal(const std::wstring& message)
{
    MessageBoxW(nullptr, message.c_str(), L"LocalCall launcher", MB_ICONERROR | MB_OK);
}

std::wstring exePath()
{
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (len == buffer.size()) {
        buffer.resize(buffer.size() * 2);
        len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    if (len == 0) return L"";
    buffer.resize(len);
    return buffer;
}

std::wstring directoryOf(const std::wstring& path)
{
    const auto pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : path.substr(0, pos);
}

bool existsFile(const std::wstring& path)
{
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool ensureDir(const std::wstring& dir)
{
    if (dir.empty()) return false;
    DWORD attrs = GetFileAttributesW(dir.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    std::wstring parent = directoryOf(dir);
    if (parent != dir && !parent.empty() && parent != L".") ensureDir(parent);
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring trim(std::wstring value)
{
    auto notSpace = [](wchar_t c) { return !std::iswspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    if (value.size() >= 2 && ((value.front() == L'"' && value.back() == L'"') ||
                              (value.front() == L'\'' && value.back() == L'\''))) {
        value = value.substr(1, value.size() - 2);
    }
    std::replace(value.begin(), value.end(), L'/', L'\\');
    return value;
}

std::wstring readTextFileUtf16OrAscii(const std::wstring& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return L"";
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return L"";

    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) {
        std::wstring out;
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
            wchar_t wc = static_cast<unsigned char>(bytes[i]) |
                         (static_cast<unsigned char>(bytes[i + 1]) << 8);
            out.push_back(wc);
        }
        return out;
    }

    int count = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (count <= 0) count = MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (count <= 0) return L"";
    std::wstring out(static_cast<size_t>(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), out.data(), count)) {
        MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), out.data(), count);
    }
    return out;
}

bool copyIfExists(const std::wstring& from, const std::wstring& to)
{
    if (!existsFile(from)) return false;
    ensureDir(directoryOf(to));
    if (!CopyFileW(from.c_str(), to.c_str(), FALSE)) {
        // If a file is locked, keep going; the launch will still use the file if it is compatible.
        return false;
    }
    return true;
}

void repairQtRuntimeFromPrefix(const std::wstring& appDir)
{
    const std::wstring prefixFile = appDir + L"\\localcall-qt-prefix.txt";
    if (!existsFile(prefixFile)) return;

    std::wstring prefix = trim(readTextFileUtf16OrAscii(prefixFile));
    if (prefix.empty()) return;
    const std::wstring qtBin = prefix + L"\\bin";
    if (GetFileAttributesW(qtBin.c_str()) == INVALID_FILE_ATTRIBUTES) return;

    const std::vector<std::wstring> dlls = {
        L"Qt6Core.dll", L"Qt6Gui.dll", L"Qt6Widgets.dll", L"Qt6Network.dll", L"Qt6Concurrent.dll",
        L"Qt6Multimedia.dll", L"Qt6MultimediaWidgets.dll", L"Qt6OpenGL.dll", L"Qt6Svg.dll"
    };
    for (const auto& dll : dlls) {
        copyIfExists(qtBin + L"\\" + dll, appDir + L"\\" + dll);
    }

    const std::wstring plugins = prefix + L"\\plugins";
    copyIfExists(plugins + L"\\platforms\\qwindows.dll", appDir + L"\\platforms\\qwindows.dll");
    copyIfExists(plugins + L"\\styles\\qwindowsvistastyle.dll", appDir + L"\\styles\\qwindowsvistastyle.dll");
    copyIfExists(plugins + L"\\imageformats\\qico.dll", appDir + L"\\imageformats\\qico.dll");
    copyIfExists(plugins + L"\\imageformats\\qjpeg.dll", appDir + L"\\imageformats\\qjpeg.dll");
    copyIfExists(plugins + L"\\iconengines\\qsvgicon.dll", appDir + L"\\iconengines\\qsvgicon.dll");
}

bool requireFile(const std::wstring& path, const std::wstring& advice)
{
    if (existsFile(path)) return true;
    showFatal(L"Missing required runtime file:\n\n" + path + L"\n\n" + advice);
    return false;
}

void prepareSearchPath(const std::wstring& appDir)
{
    using SetDefaultDllDirectoriesFn = BOOL (WINAPI*)(DWORD);
    using AddDllDirectoryFn = DLL_DIRECTORY_COOKIE (WINAPI*)(PCWSTR);
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    auto pSetDefaultDllDirectories = reinterpret_cast<SetDefaultDllDirectoriesFn>(GetProcAddress(kernel, "SetDefaultDllDirectories"));
    auto pAddDllDirectory = reinterpret_cast<AddDllDirectoryFn>(GetProcAddress(kernel, "AddDllDirectory"));

    if (pSetDefaultDllDirectories) {
        pSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                  LOAD_LIBRARY_SEARCH_USER_DIRS |
                                  LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (pAddDllDirectory) {
        pAddDllDirectory(appDir.c_str());
        pAddDllDirectory((appDir + L"\\platforms").c_str());
    }
    SetDllDirectoryW(appDir.c_str());

    DWORD pathLen = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    std::wstring oldPath;
    if (pathLen > 0) {
        oldPath.resize(pathLen);
        DWORD written = GetEnvironmentVariableW(L"PATH", oldPath.data(), pathLen);
        oldPath.resize(written);
    }
    std::wstring newPath = appDir + L";" + oldPath;
    SetEnvironmentVariableW(L"PATH", newPath.c_str());
    SetEnvironmentVariableW(L"QT_PLUGIN_PATH", appDir.c_str());
    SetEnvironmentVariableW(L"QT_QPA_PLATFORM_PLUGIN_PATH", (appDir + L"\\platforms").c_str());
}

bool smokeLoadQt(const std::wstring& appDir)
{
    const std::vector<std::wstring> required = {
        L"Qt6Core.dll", L"Qt6Gui.dll", L"Qt6Widgets.dll", L"Qt6Network.dll", L"Qt6Concurrent.dll"
    };
    for (const auto& dll : required) {
        if (!requireFile(appDir + L"\\" + dll,
                         L"Run scripts\\fix-windows-entrypoint.ps1 -Rebuild, or launch from the clean dist folder.")) {
            return false;
        }
    }
    if (!requireFile(appDir + L"\\platforms\\qwindows.dll",
                     L"The Windows Qt platform plugin was not deployed. Run scripts\\deploy-windows.ps1 -Clean.")) {
        return false;
    }

    // Load Qt Widgets now, from appDir, before starting the real Qt process.
    // This catches incompatible MinGW/old Qt DLLs and shows a controlled message
    // instead of the Windows loader entry-point popup from LocalCallApp.exe.
    const std::wstring widgets = appDir + L"\\Qt6Widgets.dll";
    HMODULE mod = LoadLibraryExW(widgets.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!mod) {
        showFatal(L"Qt6Widgets.dll exists but cannot be loaded from the app folder:\n\n" + widgets +
                  L"\n\nWindows error: " + lastErrorMessage() +
                  L"\n\nDelete build and dist, then run scripts\\fix-windows-entrypoint.ps1 -Rebuild.");
        return false;
    }
    return true;
}


bool isElevated()
{
    BOOL elevated = FALSE;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elev{};
        DWORD size = 0;
        if (GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &size))
            elevated = elev.TokenIsElevated;
        CloseHandle(token);
    }
    return elevated == TRUE;
}

bool relaunchSelfElevated(const std::wstring& launcherPath)
{
    std::wstring args = GetCommandLineW() ? GetCommandLineW() : L"";
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(args.c_str(), &argc);
    std::wstring rest;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (!rest.empty()) rest += L" ";
            std::wstring a = argv[i];
            const bool quote = a.find_first_of(L" \t\"") != std::wstring::npos;
            if (quote) {
                rest += L"\"";
                for (wchar_t c : a) { if (c == L'\"') rest += L"\\\""; else rest.push_back(c); }
                rest += L"\"";
            } else {
                rest += a;
            }
        }
        LocalFree(argv);
    }

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = launcherPath.c_str();
    sei.lpParameters = rest.empty() ? nullptr : rest.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) == TRUE;
}

std::wstring buildCommandLine(const std::wstring& appExe, LPWSTR originalCommandLine)
{
    std::wstring args = originalCommandLine ? originalCommandLine : L"";
    // Remove the launcher's argv[0] using CommandLineToArgvW for correctness.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(args.c_str(), &argc);
    std::wstring rest;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (!rest.empty()) rest += L" ";
            std::wstring a = argv[i];
            bool quote = a.find_first_of(L" \t\"") != std::wstring::npos;
            if (quote) {
                rest += L"\"";
                for (wchar_t c : a) {
                    if (c == L'\"') rest += L"\\\"";
                    else rest.push_back(c);
                }
                rest += L"\"";
            } else {
                rest += a;
            }
        }
        LocalFree(argv);
    }
    std::wstring cmd = L"\"" + appExe + L"\"";
    if (!rest.empty()) cmd += L" " + rest;
    return cmd;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    const std::wstring launcher = exePath();
    if (launcher.empty()) {
        showFatal(L"Could not resolve LocalCall.exe path.");
        return 127;
    }
    const std::wstring appDir = directoryOf(launcher);
    const std::wstring appExe = appDir + L"\\LocalCallApp.exe";

    // The media stack needs firewall-rule creation and stable UDP/TCP binding.
    // The embedded manifest asks for elevation, but this fallback still relaunches
    // through UAC if an installer strips or ignores the manifest.
    if (!isElevated()) {
        if (relaunchSelfElevated(launcher)) return 0;
        showFatal(L"LocalCall needs Administrator permission to configure firewall and media access rules.");
        return 740;
    }

    repairQtRuntimeFromPrefix(appDir);
    prepareSearchPath(appDir);

    if (!requireFile(appExe, L"LocalCall.exe is now a launcher. The real Qt app LocalCallApp.exe must be beside it.")) {
        return 127;
    }
    if (!smokeLoadQt(appDir)) {
        return 127;
    }

    std::wstring cmd = buildCommandLine(appExe, GetCommandLineW());
    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');

    BOOL ok = CreateProcessW(
        appExe.c_str(),
        mutableCmd.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        appDir.c_str(),
        &si,
        &pi);

    if (!ok) {
        showFatal(L"Failed to start LocalCallApp.exe:\n\n" + appExe + L"\n\nWindows error: " + lastErrorMessage());
        return 127;
    }

    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    return static_cast<int>(exitCode);
}

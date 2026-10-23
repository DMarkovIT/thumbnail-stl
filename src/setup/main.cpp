#include "registration.h"

#include <windows.h>

#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <string>

namespace {
std::wstring ExecutableDirectory() {
    wchar_t path[MAX_PATH * 2] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (length == 0) return std::wstring();
    std::wstring full(path, length);
    const size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return std::wstring();
    return full.substr(0, slash + 1);
}

void PrintUsage() {
    wprintf(L"Использование:\r\n");
    wprintf(L"  stlprev-setup register [путь к STLPreview.dll]\r\n");
    wprintf(L"  stlprev-setup unregister\r\n");
    wprintf(L"  stlprev-setup status\r\n");
}
}

int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_U16TEXT);

    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    const std::wstring command = argv[1];
    std::wstring log;

    if (command == L"register") {
        std::wstring dllPath = argc >= 3 ? argv[2] : ExecutableDirectory() + L"STLPreview.dll";
        if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            wprintf(L"Не найден файл DLL: %s\r\n", dllPath.c_str());
            return 2;
        }
        const bool ok = RegisterShellExtension(dllPath, log);
        wprintf(L"%s", log.c_str());
        return ok ? 0 : 3;
    }

    if (command == L"unregister") {
        UnregisterShellExtension(log);
        wprintf(L"%s", log.c_str());
        return 0;
    }

    if (command == L"status") {
        const bool ok = IsShellExtensionRegistered(log);
        wprintf(L"%s", log.c_str());
        return ok ? 0 : 4;
    }

    PrintUsage();
    return 1;
}

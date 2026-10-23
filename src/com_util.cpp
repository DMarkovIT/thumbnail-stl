#include "com_util.h"

#include <shlobj.h>

#include <algorithm>
#include <cstring>

namespace {
constexpr size_t kMaxFileSize = 512ull * 1024ull * 1024ull;
constexpr size_t kReadChunk = 256 * 1024;
}

bool ReadStreamAll(IStream* stream, std::vector<uint8_t>& out, std::wstring& error) {
    out.clear();
    if (!stream) {
        error = L"Нет потока данных";
        return false;
    }

    STATSTG stat{};
    if (SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart > 0) {
        if (stat.cbSize.QuadPart > kMaxFileSize) {
            error = L"Файл слишком большой";
            return false;
        }
        out.reserve(static_cast<size_t>(stat.cbSize.QuadPart));
    }

    for (;;) {
        const size_t offset = out.size();
        out.resize(offset + kReadChunk);

        ULONG bytesRead = 0;
        const HRESULT hr = stream->Read(out.data() + offset, static_cast<ULONG>(kReadChunk), &bytesRead);
        out.resize(offset + bytesRead);

        if (FAILED(hr)) {
            error = L"Ошибка чтения потока";
            return false;
        }
        if (bytesRead == 0 || hr == S_FALSE) break;
        if (out.size() > kMaxFileSize) {
            error = L"Файл слишком большой";
            return false;
        }
    }

    if (out.empty()) {
        error = L"Файл пуст";
        return false;
    }
    return true;
}

bool ReadFileAll(const wchar_t* path, std::vector<uint8_t>& out, std::wstring& error) {
    out.clear();
    if (!path || !*path) {
        error = L"Путь к файлу не задан";
        return false;
    }

    const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Не удалось открыть файл";
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0) {
        CloseHandle(file);
        error = L"Пустой файл";
        return false;
    }
    if (static_cast<unsigned long long>(size.QuadPart) > kMaxFileSize) {
        CloseHandle(file);
        error = L"Файл слишком большой";
        return false;
    }

    out.resize(static_cast<size_t>(size.QuadPart));
    size_t offset = 0;
    while (offset < out.size()) {
        DWORD bytesRead = 0;
        const DWORD request = static_cast<DWORD>(std::min<size_t>(out.size() - offset, 4 * 1024 * 1024));
        if (!ReadFile(file, out.data() + offset, request, &bytesRead, nullptr) || bytesRead == 0) {
            CloseHandle(file);
            out.clear();
            error = L"Ошибка чтения файла";
            return false;
        }
        offset += bytesRead;
    }

    CloseHandle(file);
    return true;
}

bool CreateD3D11Device(ID3D11Device** device, ID3D11DeviceContext** context) {
    if (!device || !context) return false;

    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    auto create = [&](D3D_DRIVER_TYPE driverType, const D3D_FEATURE_LEVEL* featureLevels,
                      UINT featureLevelCount) {
        return D3D11CreateDevice(nullptr, driverType, nullptr, flags, featureLevels, featureLevelCount,
                                 D3D11_SDK_VERSION, device, nullptr, context);
    };

    HRESULT hr = create(D3D_DRIVER_TYPE_HARDWARE, levels, ARRAYSIZE(levels));
    if (hr == E_INVALIDARG) {
        hr = create(D3D_DRIVER_TYPE_HARDWARE, levels + 1, ARRAYSIZE(levels) - 1);
    }
    if (FAILED(hr)) {
        const HRESULT hardwareResult = hr;
        hr = create(D3D_DRIVER_TYPE_WARP, levels, ARRAYSIZE(levels));
        if (hr == E_INVALIDARG) {
            hr = create(D3D_DRIVER_TYPE_WARP, levels + 1, ARRAYSIZE(levels) - 1);
        }
        if (FAILED(hr)) {
            wchar_t message[256] = {};
            _snwprintf_s(message, ARRAYSIZE(message), _TRUNCATE,
                         L"D3D11CreateDevice: аппаратный 0x%08lX, WARP 0x%08lX",
                         static_cast<unsigned long>(hardwareResult), static_cast<unsigned long>(hr));
            DebugLog(message);
        }
    }
    return SUCCEEDED(hr);
}

std::wstring GuidToString(REFGUID guid) {
    wchar_t buffer[64] = {};
    if (StringFromGUID2(guid, buffer, ARRAYSIZE(buffer)) <= 0) return std::wstring();
    return std::wstring(buffer);
}

HBITMAP CreateHBitmapFromBgra(const std::vector<uint8_t>& bgra, UINT width, UINT height) {
    if (bgra.size() < static_cast<size_t>(width) * height * 4) return nullptr;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    const HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        return nullptr;
    }
    std::memcpy(bits, bgra.data(), static_cast<size_t>(width) * height * 4);
    return bitmap;
}

void DownsampleBgraHalf(const std::vector<uint8_t>& source, UINT sourceWidth, UINT sourceHeight,
                        std::vector<uint8_t>& destination, UINT& destinationWidth, UINT& destinationHeight) {
    destinationWidth = sourceWidth / 2;
    destinationHeight = sourceHeight / 2;
    if (destinationWidth == 0 || destinationHeight == 0) {
        destination = source;
        destinationWidth = sourceWidth;
        destinationHeight = sourceHeight;
        return;
    }

    destination.assign(static_cast<size_t>(destinationWidth) * destinationHeight * 4, 0);
    for (UINT y = 0; y < destinationHeight; ++y) {
        const uint8_t* row0 = source.data() + static_cast<size_t>(y * 2) * sourceWidth * 4;
        const uint8_t* row1 = row0 + static_cast<size_t>(sourceWidth) * 4;
        uint8_t* target = destination.data() + static_cast<size_t>(y) * destinationWidth * 4;
        for (UINT x = 0; x < destinationWidth; ++x) {
            for (int channel = 0; channel < 4; ++channel) {
                const int sum = row0[static_cast<size_t>(x * 2) * 4 + channel] +
                                row0[static_cast<size_t>(x * 2 + 1) * 4 + channel] +
                                row1[static_cast<size_t>(x * 2) * 4 + channel] +
                                row1[static_cast<size_t>(x * 2 + 1) * 4 + channel];
                target[static_cast<size_t>(x) * 4 + channel] = static_cast<uint8_t>((sum + 2) / 4);
            }
        }
    }
}

namespace {
std::wstring ChooseLogPath() {
    PWSTR localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) {
        std::wstring directory = std::wstring(localAppData) + L"\\STLPreview";
        CoTaskMemFree(localAppData);
        CreateDirectoryW(directory.c_str(), nullptr);
        return directory + L"\\stlpreview.log";
    }

    wchar_t tempPath[MAX_PATH] = {};
    if (GetTempPathW(ARRAYSIZE(tempPath), tempPath) > 0) {
        return std::wstring(tempPath) + L"stlpreview.log";
    }

    wchar_t modulePath[MAX_PATH * 2] = {};
    if (GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath)) > 0) {
        std::wstring path = modulePath;
        const size_t slash = path.find_last_of(L"\\/");
        if (slash != std::wstring::npos) return path.substr(0, slash + 1) + L"stlpreview.log";
    }
    return std::wstring();
}
}

void DebugLog(const wchar_t* message) {
    if (!message) return;
    OutputDebugStringW(L"STLPreview: ");
    OutputDebugStringW(message);
    OutputDebugStringW(L"\n");

    static const std::wstring logPath = ChooseLogPath();
    if (logPath.empty()) return;

    const HANDLE file = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t line[2048] = {};
    _snwprintf_s(line, ARRAYSIZE(line), _TRUNCATE, L"[%02u:%02u:%02u.%03u pid %lu] %s\r\n", time.wHour,
                 time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentProcessId(), message);
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(wcslen(line) * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(file);
}


COLORREF SystemThemeBackgroundColor() {
    DWORD lightTheme = 1;
    DWORD size = sizeof(lightTheme);
    DWORD type = 0;
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, &type, &lightTheme, &size) != ERROR_SUCCESS) {
        lightTheme = 1;
    }
    return lightTheme ? RGB(248, 248, 250) : RGB(32, 32, 34);
}

#include "com_ids.h"
#include "com_util.h"
#include "renderer.h"
#include "stl_mesh.h"

#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
std::wstring g_dllPath;

bool SaveBmp(const wchar_t* path, const std::vector<uint8_t>& bgra, UINT width, UINT height) {
    if (bgra.size() < static_cast<size_t>(width) * height * 4) return false;

    BITMAPFILEHEADER fileHeader{};
    BITMAPINFOHEADER infoHeader{};
    infoHeader.biSize = sizeof(infoHeader);
    infoHeader.biWidth = static_cast<LONG>(width);
    infoHeader.biHeight = -static_cast<LONG>(height);
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 32;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = width * height * 4;
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(infoHeader);
    fileHeader.bfSize = fileHeader.bfOffBits + infoHeader.biSizeImage;

    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"wb") != 0 || !file) return false;
    fwrite(&fileHeader, 1, sizeof(fileHeader), file);
    fwrite(&infoHeader, 1, sizeof(infoHeader), file);
    fwrite(bgra.data(), 1, bgra.size(), file);
    fclose(file);
    return true;
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool CaptureScreen(const RECT& rect, std::vector<uint8_t>& pixels, UINT& width, UINT& height) {
    width = static_cast<UINT>(rect.right - rect.left);
    height = static_cast<UINT>(rect.bottom - rect.top);
    if (width == 0 || height == 0) return false;

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) return false;

    HGDIOBJ old = SelectObject(memory, dib);
    BitBlt(memory, 0, 0, static_cast<int>(width), static_cast<int>(height), screen, rect.left, rect.top, SRCCOPY);
    pixels.assign(static_cast<uint8_t*>(bits), static_cast<uint8_t*>(bits) + static_cast<size_t>(width) * height * 4);

    SelectObject(memory, old);
    DeleteObject(dib);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return true;
}

bool ReadBitmap(HBITMAP bitmap, std::vector<uint8_t>& pixels, UINT& width, UINT& height) {
    BITMAP source{};
    if (!GetObjectW(bitmap, sizeof(source), &source)) return false;
    width = source.bmWidth;
    height = static_cast<UINT>(source.bmHeight < 0 ? -source.bmHeight : source.bmHeight);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    pixels.assign(static_cast<size_t>(width) * height * 4, 0);
    HDC screen = GetDC(nullptr);
    const int lines = GetDIBits(screen, bitmap, 0, height, pixels.data(), &info, DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen);
    return lines == static_cast<int>(height);
}

HRESULT CreateComObject(REFCLSID clsid, REFIID riid, void** ppv, bool* usedSurrogate,
                        DWORD context = CLSCTX_ALL) {
    if (usedSurrogate) *usedSurrogate = false;

    HRESULT hr = CoCreateInstance(clsid, nullptr, context, riid, ppv);
    if (SUCCEEDED(hr)) {
        if (usedSurrogate) *usedSurrogate = true;
        return hr;
    }
    {
        wchar_t message[512] = {};
        LPWSTR text = nullptr;
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
        _snwprintf_s(message, ARRAYSIZE(message), _TRUNCATE, L"CoCreateInstance: 0x%08lX %s",
                     static_cast<unsigned long>(hr), text ? text : L"");
        if (text) LocalFree(text);
        DebugLog(message);
    }

    HMODULE module = LoadLibraryW(g_dllPath.c_str());
    if (!module) return HRESULT_FROM_WIN32(GetLastError());

    using GetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
    auto getClassObject = reinterpret_cast<GetClassObjectFn>(
        reinterpret_cast<void*>(GetProcAddress(module, "DllGetClassObject")));
    if (!getClassObject) return E_FAIL;

    ComPtr<IClassFactory> factory;
    hr = getClassObject(clsid, IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) return hr;
    return factory->CreateInstance(nullptr, riid, ppv);
}

int RunRender(const std::wstring& input, const std::wstring& output, UINT size, float azimuth = 38.0f,
              float elevation = 24.0f, bool greenBackground = false) {
    std::vector<uint8_t> bytes;
    std::wstring error;
    if (!ReadFileAll(input.c_str(), bytes, error)) {
        wprintf(L"Не удалось прочитать файл: %s\r\n", error.c_str());
        return 2;
    }

    Mesh mesh;
    if (!LoadStl(bytes.data(), bytes.size(), 4'000'000, mesh, error)) {
        wprintf(L"Разбор STL: %s\r\n", error.c_str());
        return 3;
    }
    wprintf(L"Треугольников: %u, радиус: %.3f\r\n", mesh.triangleCount, mesh.radius);
    wprintf(L"Габариты: %.3f %.3f %.3f .. %.3f %.3f %.3f\r\n", mesh.bounds[0], mesh.bounds[1], mesh.bounds[2],
            mesh.bounds[3], mesh.bounds[4], mesh.bounds[5]);

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if (!::CreateD3D11Device(device.GetAddressOf(), context.GetAddressOf())) {
        wprintf(L"Нет D3D11-устройства\r\n");
        return 4;
    }

    Renderer renderer;
    if (!renderer.Initialize(device.Get())) {
        wprintf(L"Рендерер не инициализировался\r\n");
        return 5;
    }
    if (!renderer.SetMesh(mesh)) return 6;

    CameraState camera;
    camera.azimuthDeg = azimuth;
    camera.elevationDeg = elevation;

    if (greenBackground) camera.background = {0.10f, 0.70f, 0.15f};
    if (!renderer.Render(size, size, camera, 4)) return 7;

    std::vector<uint8_t> pixels;
    UINT width = 0;
    UINT height = 0;
    if (!renderer.ReadPixels(pixels, width, height)) return 8;

    if (!SaveBmp(output.c_str(), pixels, width, height)) {
        wprintf(L"Не удалось записать %s\r\n", output.c_str());
        return 9;
    }
    wprintf(L"Готово: %s (%ux%u)\r\n", output.c_str(), width, height);
    return 0;
}

int RunThumb(const std::wstring& input, const std::wstring& output, UINT size) {
    ComPtr<IThumbnailProvider> provider;
    bool surrogate = false;
    const HRESULT hr = CreateComObject(CLSID_StlThumbnailProvider, IID_PPV_ARGS(provider.GetAddressOf()),
                                       &surrogate);
    if (FAILED(hr)) {
        wprintf(L"CoCreateInstance провайдера эскизов: 0x%08X\r\n", static_cast<unsigned>(hr));
        return 2;
    }

    ComPtr<IInitializeWithFile> initializer;
    if (FAILED(provider.As(&initializer))) return 3;
    if (FAILED(initializer->Initialize(input.c_str(), STGM_READ))) return 3;

    HBITMAP bitmap = nullptr;
    WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
    const HRESULT thumbHr = provider->GetThumbnail(size, &bitmap, &alpha);
    if (FAILED(thumbHr) || !bitmap) {
        wprintf(L"GetThumbnail: 0x%08X\r\n", static_cast<unsigned>(thumbHr));
        return 4;
    }

    std::vector<uint8_t> pixels;
    UINT width = 0;
    UINT height = 0;
    const bool read = ReadBitmap(bitmap, pixels, width, height);
    DeleteObject(bitmap);
    if (!read) return 5;

    if (!SaveBmp(output.c_str(), pixels, width, height)) return 6;
    wprintf(L"Эскиз: %s (%ux%u), alpha=%d\r\n", output.c_str(), width, height, static_cast<int>(alpha));
    return 0;
}

int RunShellThumb(const std::wstring& input, const std::wstring& output, UINT size) {
    Microsoft::WRL::ComPtr<IShellItemImageFactory> factory;
    HRESULT hr = SHCreateItemFromParsingName(input.c_str(), nullptr, IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) {
        DebugLog((L"SHCreateItemFromParsingName: 0x" + std::to_wstring(static_cast<unsigned long>(hr))).c_str());
        return 2;
    }

    SIZE requested{static_cast<LONG>(size), static_cast<LONG>(size)};
    HBITMAP bitmap = nullptr;
    hr = factory->GetImage(requested, SIIGBF_THUMBNAILONLY | SIIGBF_RESIZETOFIT, &bitmap);
    if (FAILED(hr) || !bitmap) {
        wprintf(L"GetImage: 0x%08lX (эскиз не получен)\r\n", static_cast<unsigned long>(hr));
        return 3;
    }

    std::vector<uint8_t> pixels;
    UINT width = 0;
    UINT height = 0;
    const bool read = ReadBitmap(bitmap, pixels, width, height);
    DeleteObject(bitmap);
    if (!read) return 4;
    if (!SaveBmp(output.c_str(), pixels, width, height)) return 5;

    wprintf(L"Эскиз оболочки: %s (%ux%u)\r\n", output.c_str(), width, height);
    return 0;
}

std::wstring ProcessNameOfWindow(HWND hwnd) {
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == 0) return L"?";

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return L"pid " + std::to_wstring(processId);

    wchar_t path[MAX_PATH] = {};
    DWORD size = ARRAYSIZE(path);
    std::wstring name = L"pid " + std::to_wstring(processId);
    if (QueryFullProcessImageNameW(process, 0, path, &size)) name = FileNameOf(path) + L" (" + name + L")";
    CloseHandle(process);
    return name;
}

LRESULT CALLBACK HostWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

int RunPreview(const std::wstring& input, int seconds, bool localServer) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = HostWndProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = L"STLPreviewTestHost";
    RegisterClassExW(&windowClass);

    HWND host = CreateWindowExW(WS_EX_TOPMOST, windowClass.lpszClassName, L"STL preview test",
                                WS_OVERLAPPEDWINDOW, 80, 80, 900, 700, nullptr, nullptr,
                                windowClass.hInstance, nullptr);
    if (!host) return 2;
    ShowWindow(host, SW_SHOW);
    UpdateWindow(host);

    ComPtr<IPreviewHandler> handler;
    bool surrogate = false;
    const DWORD context = localServer ? CLSCTX_LOCAL_SERVER : CLSCTX_ALL;
    const HRESULT createHr = CreateComObject(CLSID_StlPreviewHandler, IID_PPV_ARGS(handler.GetAddressOf()),
                                             &surrogate, context);
    if (FAILED(createHr)) {
        wprintf(L"CoCreateInstance обработчика предпросмотра: 0x%08X\r\n", static_cast<unsigned>(createHr));
        DestroyWindow(host);
        return 3;
    }
    wprintf(L"Активация: %s\r\n", surrogate ? L"через зарегистрированный сервер" : L"внутрипроцессно (DLL)");

    ComPtr<IInitializeWithFile> initializer;
    handler.As(&initializer);
    if (initializer) initializer->Initialize(input.c_str(), STGM_READ);

    RECT client{};
    GetClientRect(host, &client);
    if (FAILED(handler->SetWindow(host, &client))) {
        wprintf(L"SetWindow не удался\r\n");
        return 4;
    }

    ComPtr<IPreviewHandlerVisuals> visuals;
    if (SUCCEEDED(handler.As(&visuals))) visuals->SetBackgroundColor(RGB(32, 32, 32));

    const HRESULT previewHr = handler->DoPreview();
    wprintf(L"DoPreview: 0x%08X\r\n", static_cast<unsigned>(previewHr));

    HWND pane = nullptr;
    handler->QueryFocus(&pane);
    if (pane) wprintf(L"Окно вида создано процессом: %s\r\n", ProcessNameOfWindow(pane).c_str());
    else wprintf(L"Окно вида не создано\r\n");

    DebugLog(L"тест: цикл сообщений");
    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000;
    while (GetTickCount64() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
    DebugLog(L"тест: цикл сообщений завершён");
    Sleep(400);

    RECT hostRect{};
    GetWindowRect(host, &hostRect);
    std::vector<uint8_t> pixels;
    UINT width = 0;
    UINT height = 0;
    if (CaptureScreen(hostRect, pixels, width, height) && SaveBmp(L"preview.bmp", pixels, width, height)) {
        wprintf(L"Снимок окна: preview.bmp (%ux%u)\r\n", width, height);
    } else {
        wprintf(L"Не удалось сохранить снимок окна\r\n");
    }

    DebugLog(L"тест: Unload");
    handler->Unload();
    DebugLog(L"тест: DestroyWindow");
    DestroyWindow(host);
    DebugLog(L"тест: конец");
    return SUCCEEDED(previewHr) ? 0 : 5;
}
}

int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_U16TEXT);

    wchar_t modulePath[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
    g_dllPath = modulePath;
    const size_t slash = g_dllPath.find_last_of(L"\\/");
    if (slash != std::wstring::npos) g_dllPath = g_dllPath.substr(0, slash + 1) + L"STLPreview.dll";

    if (argc < 3) {
        wprintf(L"Использование:\r\n  stlprev-test render  <model.stl> <out.bmp> [размер]\r\n");
        wprintf(L"  stlprev-test thumb   <model.stl> <out.bmp> [размер]\r\n");
        wprintf(L"  stlprev-test preview <model.stl> [секунды]\r\n");
        return 1;
    }

    const std::wstring command = argv[1];
    const std::wstring input = argv[2];

    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comHr)) {
        wprintf(L"CoInitializeEx: 0x%08X\r\n", static_cast<unsigned>(comHr));
        return 1;
    }

    int result = 1;
    if (command == L"render") {
        const std::wstring output = argc >= 4 ? argv[3] : L"render.bmp";
        const UINT size = argc >= 5 ? static_cast<UINT>(_wtoi(argv[4])) : 512;
        const float azimuth = argc >= 6 ? static_cast<float>(_wtof(argv[5])) : 38.0f;
        const float elevation = argc >= 7 ? static_cast<float>(_wtof(argv[6])) : 24.0f;
        const bool green = argc >= 8 && std::wstring(argv[7]) == L"green";
        result = RunRender(input, output, size, azimuth, elevation, green);
    } else if (command == L"thumb") {
        const std::wstring output = argc >= 4 ? argv[3] : L"thumb.bmp";
        const UINT size = argc >= 5 ? static_cast<UINT>(_wtoi(argv[4])) : 256;
        result = RunThumb(input, output, size);
    } else if (command == L"shellthumb") {
        const std::wstring output = argc >= 4 ? argv[3] : L"shellthumb.bmp";
        const UINT size = argc >= 5 ? static_cast<UINT>(_wtoi(argv[4])) : 256;
        result = RunShellThumb(input, output, size);
    } else if (command == L"preview" || command == L"previewlocal") {
        const int seconds = argc >= 4 ? _wtoi(argv[3]) : 3;
        result = RunPreview(input, seconds, command == L"previewlocal");
    } else {
        wprintf(L"Неизвестная команда: %s\r\n", command.c_str());
    }

    CoUninitialize();
    return result;
}

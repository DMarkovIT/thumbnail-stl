#include "com_ids.h"
#include "com_util.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <wrl/client.h>

#include <string>

namespace {
constexpr UINT_PTR kWatchdogTimer = 1;
constexpr int kExitAfterIdleSeconds = 3;
constexpr int kExitWithoutActivation = 120;

using GetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
using LiveWindowCountFn = int(STDAPICALLTYPE*)();

std::wstring ExecutableDirectory() {
    wchar_t path[MAX_PATH * 2] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (length == 0) return std::wstring();
    std::wstring full(path, length);
    const size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return std::wstring();
    return full.substr(0, slash + 1);
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) return 1;

    const std::wstring dllPath = ExecutableDirectory() + L"STLPreview.dll";
    const HMODULE module = LoadLibraryW(dllPath.c_str());
    if (!module) {
        DebugLog(L"Хост: не удалось загрузить STLPreview.dll");
        return 2;
    }

    const auto getClassObject = reinterpret_cast<GetClassObjectFn>(
        reinterpret_cast<void*>(GetProcAddress(module, "DllGetClassObject")));
    if (!getClassObject) {
        DebugLog(L"Хост: в DLL нет DllGetClassObject");
        return 3;
    }

    Microsoft::WRL::ComPtr<IClassFactory> factory;
    if (FAILED(getClassObject(CLSID_StlPreviewHandler, IID_PPV_ARGS(factory.GetAddressOf())))) {
        DebugLog(L"Хост: фабрика обработчика не создана");
        return 4;
    }

    DWORD cookie = 0;
    if (FAILED(CoRegisterClassObject(CLSID_StlPreviewHandler, factory.Get(), CLSCTX_LOCAL_SERVER,
                                     REGCLS_MULTIPLEUSE | REGCLS_SUSPENDED, &cookie))) {
        DebugLog(L"Хост: CoRegisterClassObject не удался");
        return 5;
    }
    CoResumeClassObjects();

    const auto liveWindowCount = reinterpret_cast<LiveWindowCountFn>(
        reinterpret_cast<void*>(GetProcAddress(module, "STLPreview_LiveWindowCount")));

    bool servedActivation = false;
    int idleSeconds = 0;
    int aliveSeconds = 0;
    SetTimer(nullptr, kWatchdogTimer, 1000, nullptr);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_TIMER && message.hwnd == nullptr) {
            ++aliveSeconds;
            const int windows = liveWindowCount ? liveWindowCount() : 0;
            if (windows > 0) {
                servedActivation = true;
                idleSeconds = 0;
            } else if (servedActivation) {
                if (++idleSeconds >= kExitAfterIdleSeconds) break;
            } else if (aliveSeconds >= kExitWithoutActivation) {
                break;
            }
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    KillTimer(nullptr, kWatchdogTimer);
    CoRevokeClassObject(cookie);
    factory.Reset();
    FreeLibrary(module);
    CoUninitialize();
    return 0;
}

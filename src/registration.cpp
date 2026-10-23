#include "registration.h"

#include "com_ids.h"
#include "com_util.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <string>

namespace {
constexpr wchar_t kClassesRoot[] = L"Software\\Classes";

std::wstring ClassesKey(const std::wstring& tail) { return std::wstring(kClassesRoot) + L"\\" + tail; }

bool SetString(HKEY root, const std::wstring& subKey, const wchar_t* valueName, const std::wstring& value,
               DWORD type = REG_SZ) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }
    const LONG status = RegSetValueExW(key, valueName, 0, type,
                                       reinterpret_cast<const BYTE*>(value.c_str()),
                                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool ReadString(HKEY root, const std::wstring& subKey, const wchar_t* valueName, std::wstring& value) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;

    wchar_t buffer[1024] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    const LONG status = RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &size);
    RegCloseKey(key);

    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return false;
    value.assign(buffer);
    return true;
}

bool DeleteValue(HKEY root, const std::wstring& subKey, const wchar_t* valueName) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;
    RegDeleteValueW(key, valueName);
    RegCloseKey(key);
    return true;
}

void DeleteTree(HKEY root, const std::wstring& subKey) {
    RegDeleteTreeW(root, subKey.c_str());
}

std::wstring HostPathForDll(const std::wstring& dllPath) {
    const size_t slash = dllPath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L"STLPreviewHost.exe";
    return dllPath.substr(0, slash + 1) + L"STLPreviewHost.exe";
}

void Append(std::wstring& log, const wchar_t* line) {
    log += line;
    log += L"\r\n";
}

void NotifyShell() {
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
}

bool RegisterShellExtension(const std::wstring& dllPath, std::wstring& log) {
    log.clear();
    if (dllPath.empty()) {
        Append(log, L"Не указан путь к STLPreview.dll");
        return false;
    }

    const std::wstring thumbnailClsid = GuidToString(CLSID_StlThumbnailProvider);
    const std::wstring previewClsid = GuidToString(CLSID_StlPreviewHandler);

    const std::wstring thumbnailKey = ClassesKey(L"CLSID\\" + thumbnailClsid);
    const std::wstring previewKey = ClassesKey(L"CLSID\\" + previewClsid);
    const std::wstring stlExtensionKey = ClassesKey(kStlExtension);

    bool ok = true;

    ok &= SetString(HKEY_CURRENT_USER, thumbnailKey, nullptr, L"STL Thumbnail Provider");
    ok &= SetString(HKEY_CURRENT_USER, thumbnailKey + L"\\InprocServer32", nullptr, dllPath);
    ok &= SetString(HKEY_CURRENT_USER, thumbnailKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment");

    const std::wstring hostPath = HostPathForDll(dllPath);
    ok &= SetString(HKEY_CURRENT_USER, previewKey, nullptr, L"STL Preview Handler");
    ok &= SetString(HKEY_CURRENT_USER, previewKey + L"\\InprocServer32", nullptr, dllPath);
    ok &= SetString(HKEY_CURRENT_USER, previewKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
    ok &= SetString(HKEY_CURRENT_USER, previewKey + L"\\LocalServer32", nullptr, L"\"" + hostPath + L"\"");

    DeleteTree(HKEY_CURRENT_USER, ClassesKey(L"AppID\\" + GuidToString(APPID_StlPreviewHandler)));

    ok &= SetString(HKEY_CURRENT_USER, stlExtensionKey + L"\\shellex\\" + kThumbnailHandlerGuid, nullptr,
                    thumbnailClsid);
    ok &= SetString(HKEY_CURRENT_USER, stlExtensionKey + L"\\shellex\\" + kPreviewHandlerGuid, nullptr,
                    previewClsid);

    SetString(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\PreviewHandlers", previewClsid.c_str(),
              L"STL Preview Handler");

    if (ok) {
        Append(log, L"Расширение зарегистрировано:");
        Append(log, (L"  эскизы:      " + thumbnailClsid).c_str());
        Append(log, (L"  предпросмотр: " + previewClsid).c_str());
        Append(log, (L"  DLL:          " + dllPath).c_str());
        Append(log, (L"  хост:         " + hostPath).c_str());
        Append(log, (L"  расширение:   " + std::wstring(kStlExtension)).c_str());
    } else {
        Append(log, L"Не удалось записать часть ключей реестра в HKCU\\Software\\Classes");
    }

    NotifyShell();
    return ok;
}

bool UnregisterShellExtension(std::wstring& log) {
    log.clear();

    const std::wstring thumbnailClsid = GuidToString(CLSID_StlThumbnailProvider);
    const std::wstring previewClsid = GuidToString(CLSID_StlPreviewHandler);

    DeleteTree(HKEY_CURRENT_USER, ClassesKey(L"CLSID\\" + thumbnailClsid));
    DeleteTree(HKEY_CURRENT_USER, ClassesKey(L"CLSID\\" + previewClsid));
    DeleteTree(HKEY_CURRENT_USER, ClassesKey(L"AppID\\" + GuidToString(APPID_StlPreviewHandler)));
    DeleteValue(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\PreviewHandlers",
                previewClsid.c_str());

    const std::wstring shelfex = ClassesKey(std::wstring(kStlExtension) + L"\\shellex");
    DeleteTree(HKEY_CURRENT_USER, shelfex + L"\\" + kThumbnailHandlerGuid);
    DeleteTree(HKEY_CURRENT_USER, shelfex + L"\\" + kPreviewHandlerGuid);

    Append(log, L"Регистрация расширения удалена.");
    NotifyShell();
    return true;
}

bool IsShellExtensionRegistered(std::wstring& log) {
    log.clear();

    const std::wstring thumbnailClsid = GuidToString(CLSID_StlThumbnailProvider);
    const std::wstring previewClsid = GuidToString(CLSID_StlPreviewHandler);
    const std::wstring stlExtensionKey = ClassesKey(kStlExtension);

    bool ok = true;
    std::wstring value;

    if (ReadString(HKEY_CURRENT_USER, stlExtensionKey + L"\\shellex\\" + kThumbnailHandlerGuid, nullptr, value) &&
        value == thumbnailClsid) {
        Append(log, L"эскизы:        зарегистрированы");
    } else {
        Append(log, L"эскизы:        НЕ зарегистрированы");
        ok = false;
    }

    if (ReadString(HKEY_CURRENT_USER, stlExtensionKey + L"\\shellex\\" + kPreviewHandlerGuid, nullptr, value) &&
        value == previewClsid) {
        Append(log, L"предпросмотр:  зарегистрирован");
    } else {
        Append(log, L"предпросмотр:  НЕ зарегистрирован");
        ok = false;
    }

    std::wstring dllPath;
    if (ReadString(HKEY_CURRENT_USER, ClassesKey(L"CLSID\\" + thumbnailClsid) + L"\\InprocServer32", nullptr,
                   dllPath)) {
        Append(log, (L"DLL:           " + dllPath).c_str());
        if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            Append(log, L"ВНИМАНИЕ: файла DLL по этому пути нет");
            ok = false;
        }
    } else {
        Append(log, L"DLL:           путь не задан");
        ok = false;
    }

    if (ReadString(HKEY_CURRENT_USER, ClassesKey(L"CLSID\\" + previewClsid) + L"\\LocalServer32", nullptr,
                   value)) {
        Append(log, (L"Хост:          " + value).c_str());
        if (value.size() > 2 && GetFileAttributesW(value.substr(1, value.size() - 2).c_str()) ==
                                    INVALID_FILE_ATTRIBUTES) {
            Append(log, L"ВНИМАНИЕ: файла хоста по этому пути нет");
            ok = false;
        }
    } else {
        Append(log, L"Хост:          не зарегистрирован");
        ok = false;
    }

    return ok;
}

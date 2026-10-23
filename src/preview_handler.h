#pragma once

#include "com_ids.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <wrl/client.h>

class PreviewWindow;

class PreviewHandler final : public IPreviewHandler,
                             public IPreviewHandlerVisuals,
                             public IInitializeWithFile,
                             public IInitializeWithStream,
                             public IObjectWithSite,
                             public IOleWindow {
public:
    PreviewHandler();
    ~PreviewHandler();

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    IFACEMETHODIMP SetWindow(HWND hwndParent, const RECT* prc) override;
    IFACEMETHODIMP SetRect(const RECT* prc) override;
    IFACEMETHODIMP DoPreview() override;
    IFACEMETHODIMP Unload() override;
    IFACEMETHODIMP SetFocus() override;
    IFACEMETHODIMP QueryFocus(HWND* phwnd) override;
    IFACEMETHODIMP TranslateAccelerator(MSG* pmsg) override;

    IFACEMETHODIMP SetBackgroundColor(COLORREF color) override;
    IFACEMETHODIMP SetFont(const LOGFONTW* plf) override;
    IFACEMETHODIMP SetTextColor(COLORREF color) override;

    IFACEMETHODIMP Initialize(LPCWSTR filePath, DWORD grfMode) override;

    IFACEMETHODIMP Initialize(IStream* stream, DWORD grfMode) override;

    IFACEMETHODIMP SetSite(IUnknown* site) override;
    IFACEMETHODIMP GetSite(REFIID riid, void** ppv) override;

    IFACEMETHODIMP GetWindow(HWND* phwnd) override;
    IFACEMETHODIMP ContextSensitiveHelp(BOOL enterMode) override;

private:
    std::atomic<LONG> m_refs{1};
    std::unique_ptr<PreviewWindow> m_window;
    Microsoft::WRL::ComPtr<IUnknown> m_site;
};

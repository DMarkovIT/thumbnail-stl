#pragma once

#include "com_ids.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include <wrl/client.h>

class ThumbnailProvider final : public IThumbnailProvider,
                                public IInitializeWithStream,
                                public IInitializeWithFile {
public:
    ThumbnailProvider() = default;

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override;

    IFACEMETHODIMP Initialize(IStream* stream, DWORD grfMode) override;
    IFACEMETHODIMP Initialize(LPCWSTR filePath, DWORD grfMode) override;

private:
    ~ThumbnailProvider() = default;

    bool EnsureSource(std::wstring& error);

    std::atomic<LONG> m_refs{1};
    std::mutex m_sourceMutex;
    Microsoft::WRL::ComPtr<IStream> m_stream;
    std::wstring m_path;
    std::vector<uint8_t> m_bytes;
    bool m_loaded = false;
    std::wstring m_error;
};

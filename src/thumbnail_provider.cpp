#include "thumbnail_provider.h"

#include "com_util.h"
#include "renderer.h"
#include "stl_mesh.h"

#include <algorithm>

namespace {
DirectX::XMFLOAT3 FromColorRef(COLORREF color) {
    return {GetRValue(color) / 255.0f, GetGValue(color) / 255.0f, GetBValue(color) / 255.0f};
}
}
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace {
struct GpuState {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Renderer renderer;
    bool ready = false;
};

std::mutex g_gpuMutex;
GpuState* g_gpu = new GpuState();

constexpr uint32_t kThumbnailTriangleBudget = 1'500'000;

bool EnsureGpu() {
    if (g_gpu->ready) return true;
    if (!CreateD3D11Device(g_gpu->device.GetAddressOf(), g_gpu->context.GetAddressOf())) return false;
    if (!g_gpu->renderer.Initialize(g_gpu->device.Get())) return false;
    g_gpu->ready = true;
    return true;
}
}

IFACEMETHODIMP ThumbnailProvider::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_IUnknown || riid == IID_IThumbnailProvider) {
        *ppv = static_cast<IThumbnailProvider*>(this);
    } else if (riid == IID_IInitializeWithStream) {
        *ppv = static_cast<IInitializeWithStream*>(this);
    } else if (riid == IID_IInitializeWithFile) {
        *ppv = static_cast<IInitializeWithFile*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) ThumbnailProvider::AddRef() {
    return static_cast<ULONG>(m_refs.fetch_add(1) + 1);
}

IFACEMETHODIMP_(ULONG) ThumbnailProvider::Release() {
    const LONG refs = m_refs.fetch_sub(1) - 1;
    if (refs == 0) delete this;
    return static_cast<ULONG>(refs);
}

IFACEMETHODIMP ThumbnailProvider::Initialize(IStream* stream, DWORD ) {
    if (!stream) return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(m_sourceMutex);
    m_stream = stream;
    m_path.clear();
    m_bytes.clear();
    m_loaded = false;
    m_error.clear();
    return S_OK;
}

IFACEMETHODIMP ThumbnailProvider::Initialize(LPCWSTR filePath, DWORD ) {
    if (!filePath || !*filePath) return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(m_sourceMutex);
    m_path = filePath;
    m_stream.Reset();
    m_bytes.clear();
    m_loaded = false;
    m_error.clear();
    return S_OK;
}

bool ThumbnailProvider::EnsureSource(std::wstring& error) {
    std::lock_guard<std::mutex> lock(m_sourceMutex);
    if (m_loaded) {
        error = m_error;
        return !m_bytes.empty();
    }
    m_loaded = true;

    if (m_stream) {
        ReadStreamAll(m_stream.Get(), m_bytes, m_error);
    } else if (!m_path.empty()) {
        ReadFileAll(m_path.c_str(), m_bytes, m_error);
    } else {
        m_error = L"Источник данных не задан";
    }

    error = m_error;
    return !m_bytes.empty();
}

IFACEMETHODIMP ThumbnailProvider::GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) {
    if (!phbmp || !pdwAlpha) return E_POINTER;
    *phbmp = nullptr;
    *pdwAlpha = WTSAT_ARGB;

    if (cx == 0) cx = 256;
    cx = std::clamp<UINT>(cx, 16, 1024);

    std::wstring error;
    if (!EnsureSource(error)) {
        DebugLog(error.c_str());
        return E_FAIL;
    }

    Mesh mesh;
    if (!LoadStl(m_bytes.data(), m_bytes.size(), kThumbnailTriangleBudget, mesh, error)) {
        DebugLog(error.c_str());
        return E_FAIL;
    }

    std::lock_guard<std::mutex> lock(g_gpuMutex);
    if (!EnsureGpu()) {
        DebugLog(L"Не удалось создать устройство Direct3D");
        return E_FAIL;
    }
    if (!g_gpu->renderer.SetMesh(mesh)) return E_FAIL;

    const UINT renderSize = std::min<UINT>(cx * 2, 2048);

    CameraState camera;
    camera.background = FromColorRef(SystemThemeBackgroundColor());
    camera.azimuthDeg = 38.0f;
    camera.elevationDeg = 24.0f;
    camera.zoom = 1.0f;

    if (!g_gpu->renderer.Render(renderSize, renderSize, camera, 4)) return E_FAIL;

    std::vector<uint8_t> pixels;
    UINT width = 0;
    UINT height = 0;
    if (!g_gpu->renderer.ReadPixels(pixels, width, height)) return E_FAIL;

    std::vector<uint8_t> result;
    UINT resultWidth = 0;
    UINT resultHeight = 0;
    DownsampleBgraHalf(pixels, width, height, result, resultWidth, resultHeight);

    const HBITMAP bitmap = CreateHBitmapFromBgra(result, resultWidth, resultHeight);
    if (!bitmap) return E_FAIL;

    *phbmp = bitmap;
    return S_OK;
}

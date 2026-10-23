#include "preview_handler.h"

#include "com_util.h"
#include "renderer.h"
#include "stl_mesh.h"

#include <algorithm>
#include <cmath>
#include <atomic>
#include <mutex>

#include <windowsx.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kWindowClass[] = L"STLPreviewExplorerPane";
constexpr UINT_PTR kAnimationTimer = 1;
constexpr UINT kFrameIntervalMs = 33;
constexpr float kOrbitSpeed = 14.0f;
constexpr float kDragDegreesPerPixel = 0.45f;
constexpr ULONGLONG kAutoRotateDelayMs = 2500;
constexpr uint32_t kPreviewTriangleBudget = 4'000'000;

std::atomic<int> g_livePreviewWindows{0};

DirectX::XMFLOAT3 FromColorRef(COLORREF color) {
    return {GetRValue(color) / 255.0f, GetGValue(color) / 255.0f, GetBValue(color) / 255.0f};
}
}

class PreviewWindow {
public:
    PreviewWindow();
    ~PreviewWindow() { Destroy(); }

    bool Create(HWND parent, const RECT& rect);
    void Destroy();
    void SetRect(const RECT& rect);
    void SetSource(std::wstring path, ComPtr<IStream> stream);
    void SetBackgroundColor(COLORREF color);
    bool StartPreview();
    void StopPreview();
    void Focus() const {
        if (m_hwnd) ::SetFocus(m_hwnd);
    }
    HWND Hwnd() const { return m_hwnd; }

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    LRESULT WndProc(UINT message, WPARAM wParam, LPARAM lParam);

    bool EnsureSourceLoaded();
    bool InitializeDevice();
    void ReleaseDevice();
    bool InitializeSwapChain();
    bool CreateBackBuffer();
    void ResizeSwapChain();
    bool InitializeScene();
    void RenderFrame();
    void OnTimer();
    void ResetCamera();
    void MarkInteraction();

    HWND m_hwnd = nullptr;
    HWND m_parent = nullptr;

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain1> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_backBuffer;

    Renderer m_renderer;
    Mesh m_mesh;
    CameraState m_camera;

    std::wstring m_path;
    ComPtr<IStream> m_stream;
    std::vector<uint8_t> m_source;

    bool m_sceneReady = false;
    bool m_previewing = false;
    bool m_dirty = false;
    bool m_dragging = false;
    bool m_autoRotate = true;
    bool m_hasBackgroundColor = false;
    POINT m_lastMouse{};
    ULONGLONG m_lastInput = 0;
    ULONGLONG m_lastFrame = 0;
    COLORREF m_backgroundColor = RGB(255, 255, 255);
};

PreviewWindow::PreviewWindow() {
    m_camera.background = FromColorRef(SystemThemeBackgroundColor());
}

bool PreviewWindow::Create(HWND parent, const RECT& rect) {
    static std::once_flag once;
    static bool registered = false;
    std::call_once(once, [] {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &PreviewWindow::StaticWndProc;
        windowClass.hInstance = g_moduleHandle;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = kWindowClass;
        registered = RegisterClassExW(&windowClass) != 0;
    });
    if (!registered) {
        DebugLog((L"RegisterClassExW не удался, код " + std::to_wstring(GetLastError())).c_str());
        return false;
    }
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, ARRAYSIZE(exePath));
        wchar_t message[1024] = {};
        _snwprintf_s(message, ARRAYSIZE(message), _TRUNCATE, L"Процесс: %s, module=0x%p", exePath,
                     reinterpret_cast<void*>(g_moduleHandle));
        DebugLog(message);
    }

    if (m_hwnd && parent == m_parent) {
        SetRect(rect);
        return true;
    }
    Destroy();

    m_parent = parent;
    const DWORD style = parent ? (WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN)
                               : (WS_POPUP | WS_CLIPCHILDREN);

    m_hwnd = CreateWindowExW(0, kWindowClass, L"Предпросмотр STL", style, rect.left, rect.top,
                             std::max<LONG>(rect.right - rect.left, 1),
                             std::max<LONG>(rect.bottom - rect.top, 1), parent, nullptr, g_moduleHandle,
                             this);
    if (!m_hwnd && parent) {
        const DWORD firstError = GetLastError();
        DebugLog((L"CreateWindowExW с родителем не удался, код " + std::to_wstring(firstError)).c_str());

        m_hwnd = CreateWindowExW(0, kWindowClass, L"Предпросмотр STL", WS_POPUP | WS_CLIPCHILDREN,
                                 rect.left, rect.top, std::max<LONG>(rect.right - rect.left, 1),
                                 std::max<LONG>(rect.bottom - rect.top, 1), nullptr, nullptr,
                                 g_moduleHandle, this);
        if (m_hwnd) SetParent(m_hwnd, parent);
    }
    if (!m_hwnd) {
        DebugLog((L"CreateWindowExW не удался, код " + std::to_wstring(GetLastError())).c_str());
        return false;
    }
    ++g_livePreviewWindows;
    return true;
}

void PreviewWindow::Destroy() {
    if (m_hwnd) {
        KillTimer(m_hwnd, kAnimationTimer);
        if (GetCapture() == m_hwnd) ReleaseCapture();
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        --g_livePreviewWindows;
    }
    ReleaseDevice();
    m_renderer.Reset();
    m_source.clear();
    m_sceneReady = false;
    m_previewing = false;
    m_parent = nullptr;
}

void PreviewWindow::SetRect(const RECT& rect) {
    if (!m_hwnd) return;
    SetWindowPos(m_hwnd, nullptr, rect.left, rect.top, std::max<LONG>(rect.right - rect.left, 1),
                 std::max<LONG>(rect.bottom - rect.top, 1), SWP_NOZORDER | SWP_NOACTIVATE);
}

void PreviewWindow::SetSource(std::wstring path, ComPtr<IStream> stream) {
    m_path = std::move(path);
    m_stream = std::move(stream);
    m_source.clear();
    m_sceneReady = false;
}

void PreviewWindow::SetBackgroundColor(COLORREF color) {
    m_backgroundColor = color;
    m_hasBackgroundColor = true;
    m_camera.background = FromColorRef(color);
    m_dirty = true;
}

bool PreviewWindow::EnsureSourceLoaded() {
    if (!m_source.empty()) return true;

    std::wstring error;
    bool ok = false;
    if (m_stream) {
        ok = ReadStreamAll(m_stream.Get(), m_source, error);
    } else if (!m_path.empty()) {
        ok = ReadFileAll(m_path.c_str(), m_source, error);
    } else {
        error = L"Источник данных не задан";
    }
    if (!ok) DebugLog(error.c_str());
    return ok;
}

bool PreviewWindow::InitializeDevice() {
    if (m_device) return true;

    if (!CreateD3D11Device(m_device.GetAddressOf(), m_context.GetAddressOf())) {
        DebugLog(L"Не удалось создать устройство Direct3D");
        return false;
    }
    if (!m_renderer.Initialize(m_device.Get())) {
        DebugLog(L"Рендерер не инициализировался");
        return false;
    }
    if (!InitializeSwapChain()) {
        DebugLog(L"Цепочка обмена не создана");
        return false;
    }
    if (!InitializeScene()) {
        DebugLog(L"Сцена не загружена");
        return false;
    }
    return true;
}

void PreviewWindow::ReleaseDevice() {
    if (m_context) m_context->ClearState();
    m_backBuffer.Reset();
    m_swapChain.Reset();
    m_renderer.Reset();
    m_context.Reset();
    m_device.Reset();
}

bool PreviewWindow::InitializeSwapChain() {
    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(m_device.As(&dxgiDevice))) return false;

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf()))) return false;

    ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())))) return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 0;
    desc.Height = 0;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    if (FAILED(factory->CreateSwapChainForHwnd(m_device.Get(), m_hwnd, &desc, nullptr, nullptr,
                                               m_swapChain.GetAddressOf()))) {
        return false;
    }
    factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
    return CreateBackBuffer();
}

bool PreviewWindow::CreateBackBuffer() {
    if (!m_swapChain) return false;
    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())))) return false;
    if (FAILED(m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_backBuffer.GetAddressOf())))
        return false;
    return true;
}

void PreviewWindow::ResizeSwapChain() {
    if (!m_swapChain) return;
    m_backBuffer.Reset();
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    if (FAILED(m_swapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0))) {
        ReleaseDevice();
        return;
    }
    CreateBackBuffer();
    m_dirty = true;
}

bool PreviewWindow::InitializeScene() {
    if (m_sceneReady) return true;
    if (!EnsureSourceLoaded()) return false;

    std::wstring error;
    if (!LoadStl(m_source.data(), m_source.size(), kPreviewTriangleBudget, m_mesh, error)) {
        DebugLog(error.c_str());
        return false;
    }
    if (!m_renderer.SetMesh(m_mesh)) return false;
    m_sceneReady = true;
    return true;
}

void PreviewWindow::RenderFrame() {
    if (!m_hwnd || !m_swapChain || !m_backBuffer) return;

    RECT client{};
    if (!GetClientRect(m_hwnd, &client)) return;
    const UINT width = static_cast<UINT>(client.right - client.left);
    const UINT height = static_cast<UINT>(client.bottom - client.top);
    if (width == 0 || height == 0) return;

    if (!m_renderer.Render(width, height, m_camera, 4, 0.15f)) return;
    m_renderer.Blit(m_backBuffer.Get(), width, height);
    m_context->OMSetRenderTargets(0, nullptr, nullptr);

    const HRESULT hr = m_swapChain->Present(0, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        ReleaseDevice();
    }
}

void PreviewWindow::OnTimer() {
    const ULONGLONG now = GetTickCount64();
    const float delta = std::min(static_cast<float>(now - m_lastFrame) / 1000.0f, 0.1f);
    m_lastFrame = now;

    bool needsFrame = m_dirty;
    m_dirty = false;

    if (m_autoRotate && !m_dragging && now - m_lastInput > kAutoRotateDelayMs) {
        m_camera.azimuthDeg = std::fmod(m_camera.azimuthDeg + delta * kOrbitSpeed, 360.0f);
        needsFrame = true;
    }

    if (needsFrame) {
        if (!m_device) {
            if (!InitializeDevice()) return;
        }
        RenderFrame();
    }
}

void PreviewWindow::ResetCamera() {
    m_camera.azimuthDeg = 38.0f;
    m_camera.elevationDeg = 24.0f;
    m_camera.zoom = 1.0f;
    m_dirty = true;
}

void PreviewWindow::MarkInteraction() {
    m_lastInput = GetTickCount64();
    m_dirty = true;
}

bool PreviewWindow::StartPreview() {
    if (!m_hwnd) {
        DebugLog(L"StartPreview без окна");
        return false;
    }
    if (!InitializeDevice()) {
        DebugLog(L"StartPreview: устройство или сцена не инициализировались");
        return false;
    }

    ShowWindow(m_hwnd, SW_SHOW);
    m_previewing = true;
    m_lastInput = GetTickCount64();
    m_lastFrame = m_lastInput;
    m_dirty = false;
    SetTimer(m_hwnd, kAnimationTimer, kFrameIntervalMs, nullptr);
    RenderFrame();
    return true;
}

void PreviewWindow::StopPreview() {
    m_previewing = false;
    if (m_hwnd) KillTimer(m_hwnd, kAnimationTimer);
}

LRESULT CALLBACK PreviewWindow::StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        auto* window = reinterpret_cast<PreviewWindow*>(create->lpCreateParams);

        window->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }

    auto* self = reinterpret_cast<PreviewWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->WndProc(message, wParam, lParam);
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT PreviewWindow::WndProc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(m_hwnd, &paint);
            if (m_previewing) RenderFrame();
            EndPaint(m_hwnd, &paint);
            return 0;
        }

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) ResizeSwapChain();
            return 0;

        case WM_TIMER:
            if (wParam == kAnimationTimer) {
                OnTimer();
                return 0;
            }
            break;

        case WM_MOUSEACTIVATE:
            return MA_ACTIVATE;

        case WM_LBUTTONDOWN: {
            SetFocus(m_hwnd);
            SetCapture(m_hwnd);
            m_dragging = true;
            m_lastMouse.x = GET_X_LPARAM(lParam);
            m_lastMouse.y = GET_Y_LPARAM(lParam);
            MarkInteraction();
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!m_dragging) return 0;
            const POINT current{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            m_camera.azimuthDeg -= (current.x - m_lastMouse.x) * kDragDegreesPerPixel;
            m_camera.elevationDeg =
                std::clamp(m_camera.elevationDeg + (current.y - m_lastMouse.y) * kDragDegreesPerPixel, -89.0f,
                           89.0f);
            m_lastMouse = current;
            MarkInteraction();
            return 0;
        }

        case WM_LBUTTONUP:
            if (m_dragging) {
                m_dragging = false;
                if (GetCapture() == m_hwnd) ReleaseCapture();
                MarkInteraction();
            }
            return 0;

        case WM_CAPTURECHANGED:
            m_dragging = false;
            return 0;

        case WM_LBUTTONDBLCLK:
            ResetCamera();
            MarkInteraction();
            return 0;

        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            m_camera.zoom = std::clamp(m_camera.zoom * std::pow(1.15f, delta / 120.0f), 0.5f, 8.0f);
            MarkInteraction();
            return 0;
        }

        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, m_dragging ? IDC_SIZEALL : IDC_ARROW));
                return TRUE;
            }
            break;
        }

        case WM_DESTROY:
            StopPreview();
            return 0;

        default:
            break;
    }
    return DefWindowProcW(m_hwnd, message, wParam, lParam);
}

extern "C" int STLPreview_LiveWindowCount() {
    return g_livePreviewWindows.load();
}

PreviewHandler::PreviewHandler() = default;

PreviewHandler::~PreviewHandler() = default;

IFACEMETHODIMP PreviewHandler::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_IUnknown || riid == IID_IPreviewHandler) {
        *ppv = static_cast<IPreviewHandler*>(this);
    } else if (riid == IID_IPreviewHandlerVisuals) {
        *ppv = static_cast<IPreviewHandlerVisuals*>(this);
    } else if (riid == IID_IInitializeWithFile) {
        *ppv = static_cast<IInitializeWithFile*>(this);
    } else if (riid == IID_IInitializeWithStream) {
        *ppv = static_cast<IInitializeWithStream*>(this);
    } else if (riid == IID_IObjectWithSite) {
        *ppv = static_cast<IObjectWithSite*>(this);
    } else if (riid == IID_IOleWindow) {
        *ppv = static_cast<IOleWindow*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) PreviewHandler::AddRef() {
    return static_cast<ULONG>(m_refs.fetch_add(1) + 1);
}

IFACEMETHODIMP_(ULONG) PreviewHandler::Release() {
    const LONG refs = m_refs.fetch_sub(1) - 1;
    if (refs == 0) delete this;
    return static_cast<ULONG>(refs);
}

IFACEMETHODIMP PreviewHandler::SetWindow(HWND hwndParent, const RECT* prc) {
    if (!prc) return E_INVALIDARG;
    {
        wchar_t message[256] = {};
        _snwprintf_s(message, ARRAYSIZE(message), _TRUNCATE,
                     L"SetWindow: hwnd=0x%p, IsWindow=%d, rect=%ld,%ld,%ld,%ld", hwndParent,
                     IsWindow(hwndParent), prc->left, prc->top, prc->right, prc->bottom);
        DebugLog(message);
    }
    if (!m_window) m_window = std::make_unique<PreviewWindow>();
    if (!m_window->Create(hwndParent, *prc)) return E_FAIL;
    return S_OK;
}

IFACEMETHODIMP PreviewHandler::SetRect(const RECT* prc) {
    if (!prc) return E_INVALIDARG;
    if (m_window) m_window->SetRect(*prc);
    return S_OK;
}

IFACEMETHODIMP PreviewHandler::DoPreview() {
    if (!m_window) return E_UNEXPECTED;
    if (!m_window->StartPreview()) return E_FAIL;
    return S_OK;
}

IFACEMETHODIMP PreviewHandler::Unload() {
    if (m_window) {
        m_window->StopPreview();
        m_window->Destroy();
        m_window.reset();
    }
    m_site.Reset();
    return S_OK;
}

IFACEMETHODIMP PreviewHandler::SetFocus() {
    if (!m_window) return E_UNEXPECTED;
    m_window->Focus();
    return S_OK;
}

IFACEMETHODIMP PreviewHandler::QueryFocus(HWND* phwnd) {
    if (!phwnd) return E_POINTER;
    *phwnd = m_window ? m_window->Hwnd() : nullptr;
    return S_OK;
}

IFACEMETHODIMP PreviewHandler::TranslateAccelerator(MSG*) {
    return S_FALSE;
}

IFACEMETHODIMP PreviewHandler::SetBackgroundColor(COLORREF color) {
    {
        wchar_t message[128] = {};
        _snwprintf_s(message, ARRAYSIZE(message), _TRUNCATE, L"Фон от оболочки: %06lX",
                     static_cast<unsigned long>(color));
        DebugLog(message);
    }

    if (m_window) m_window->SetBackgroundColor(SystemThemeBackgroundColor());
    return S_OK;
}

IFACEMETHODIMP PreviewHandler::SetFont(const LOGFONTW*) {
    return S_OK;
}

IFACEMETHODIMP PreviewHandler::SetTextColor(COLORREF) {
    return S_OK;
}

IFACEMETHODIMP PreviewHandler::Initialize(LPCWSTR filePath, DWORD ) {
    if (!filePath || !*filePath) return E_INVALIDARG;
    if (!m_window) m_window = std::make_unique<PreviewWindow>();
    m_window->SetSource(filePath, nullptr);
    return S_OK;
}

IFACEMETHODIMP PreviewHandler::Initialize(IStream* stream, DWORD ) {
    if (!stream) return E_INVALIDARG;
    if (!m_window) m_window = std::make_unique<PreviewWindow>();
    m_window->SetSource(std::wstring(), stream);
    return S_OK;
}

IFACEMETHODIMP PreviewHandler::SetSite(IUnknown* site) {
    m_site = site;
    return S_OK;
}

IFACEMETHODIMP PreviewHandler::GetSite(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (!m_site) return E_FAIL;
    return m_site->QueryInterface(riid, ppv);
}

IFACEMETHODIMP PreviewHandler::GetWindow(HWND* phwnd) {
    if (!phwnd) return E_POINTER;
    *phwnd = m_window ? m_window->Hwnd() : nullptr;
    return S_OK;
}

IFACEMETHODIMP PreviewHandler::ContextSensitiveHelp(BOOL) {
    return E_NOTIMPL;
}

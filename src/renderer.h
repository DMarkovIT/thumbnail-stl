#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>
#include <DirectXMath.h>
#include <cstdint>
#include <vector>

#include <wrl/client.h>

#include "stl_mesh.h"

struct CameraState {
    float azimuthDeg = 38.0f;
    float elevationDeg = 24.0f;
    float zoom = 1.0f;
    DirectX::XMFLOAT3 background{0.96f, 0.965f, 0.975f};
};

class Renderer {
public:
    bool Initialize(ID3D11Device* device);
    void Reset();

    bool SetMesh(const Mesh& mesh);
    UINT VertexCount() const { return m_vertexCount; }

    bool Render(UINT width, UINT height, const CameraState& camera, UINT samples = 4,
                float fitSmoothing = 1.0f);

    void Blit(ID3D11RenderTargetView* target, UINT width, UINT height);

    bool ReadPixels(std::vector<uint8_t>& pixels, UINT& width, UINT& height);

    UINT Width() const { return m_width; }
    UINT Height() const { return m_height; }

private:
    bool CreateDeviceContextPieces();
    bool EnsureTargets(UINT width, UINT height, UINT samples);
    struct FitFrame {
        float centerX = 0.0f;
        float centerY = 0.0f;
        float halfBase = 0.0f;
    };
    FitFrame ComputeFit(const DirectX::XMMATRIX& view, float aspect) const;
    void ReleaseTargets();

    struct Targets {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> color;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> colorRtv;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> depth;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthDsv;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> resolved;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> resolvedSrv;
    };

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_modelVs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_modelPs;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_layout;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_fullscreenVs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_backgroundPs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_blitPs;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_cameraCb;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_backgroundCb;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;

    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthOn;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthOff;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizer;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;

    Targets m_targets;
    UINT m_width = 0;
    UINT m_height = 0;
    UINT m_samples = 0;
    UINT m_vertexCount = 0;
    float m_radius = 1.0f;
    float m_bounds[6]{};

    std::vector<DirectX::XMFLOAT3> m_fitPoints;
    FitFrame m_fit;
    bool m_fitValid = false;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_staging;
    UINT m_stagingWidth = 0;
    UINT m_stagingHeight = 0;
};

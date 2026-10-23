#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <d3dcompiler.h>

using Microsoft::WRL::ComPtr;

namespace {
const char kModelShaderSource[] = R"(
cbuffer CameraCB : register(b0)
{
    row_major float4x4 gViewProj;
    row_major float4x4 gView;
    float4 gBaseColor;
    float4 gParams;
};

struct VSIn
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float3 normalView : TEXCOORD0;
    float3 positionView : TEXCOORD1;
};

VSOut VSMain(VSIn input)
{
    VSOut output;
    float4 world = float4(input.position, 1.0f);
    output.position = mul(world, gViewProj);
    output.positionView = mul(world, gView).xyz;
    output.normalView = mul(input.normal, (float3x3)gView);
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float3 normal = normalize(input.normalView);
    float3 toEye = normalize(-input.positionView);

    if (dot(normal, toEye) < 0.0f)
        normal = -normal;

    float3 keyDir = normalize(float3(-0.35f, 0.45f, -0.82f));
    float3 fillDir = normalize(float3(0.65f, -0.20f, -0.55f));

    float key = saturate(dot(normal, keyDir));
    float fill = saturate(dot(normal, fillDir));
    float3 color = gBaseColor.rgb * (0.32f + 0.72f * key + 0.20f * fill);

    float3 halfway = normalize(keyDir + toEye);
    color += pow(saturate(dot(normal, halfway)), 48.0f) * 0.20f;

    float rim = pow(1.0f - saturate(dot(normal, toEye)), 4.0f) * 0.05f;
    color += rim;

    return float4(saturate(color), 1.0f);
}
)";

const char kBackgroundShaderSource[] = R"(
cbuffer BackgroundCB : register(b0)
{
    float4 gTopColor;
    float4 gBottomColor;
    float4 gParams;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOut VSFullscreen(uint vertexId : SV_VertexID)
{
    float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    VSOut output;
    output.position = float4(corner * 2.0f - 1.0f, 0.0f, 1.0f);
    output.uv = corner;
    return output;
}

float4 PSBackground(VSOut input) : SV_TARGET
{
    float3 color = lerp(gBottomColor.rgb, gTopColor.rgb, saturate(input.uv.y));
    return float4(color, 1.0f);
}
)";

const char kBlitShaderSource[] = R"(
Texture2D gSource : register(t0);
SamplerState gSampler : register(s0);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOut VSFullscreen(uint vertexId : SV_VertexID)
{
    float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    VSOut output;
    output.position = float4(corner * 2.0f - 1.0f, 0.0f, 1.0f);
    output.uv = corner;
    return output;
}

float4 PSBlit(VSOut input) : SV_TARGET
{
    return gSource.Sample(gSampler, float2(input.uv.x, 1.0f - input.uv.y));
}
)";

struct alignas(16) CameraConstants {
    DirectX::XMFLOAT4X4 viewProj;
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4 baseColor;
    DirectX::XMFLOAT4 params;
};

struct alignas(16) BackgroundConstants {
    DirectX::XMFLOAT4 topColor;
    DirectX::XMFLOAT4 bottomColor;
    DirectX::XMFLOAT4 params;
};

bool CompileShader(const char* source, const char* entryPoint, const char* target, ComPtr<ID3DBlob>& blob) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, entryPoint, target,
                                  flags, 0, blob.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        if (errors) {
            OutputDebugStringA("STLPreview: ошибка компиляции шейдера: ");
            OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
            OutputDebugStringA("\n");
        }
        return false;
    }
    return true;
}

template <typename T>
bool CreateConstantBuffer(ID3D11Device* device, ComPtr<ID3D11Buffer>& buffer) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(T);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(device->CreateBuffer(&desc, nullptr, buffer.GetAddressOf()));
}

template <typename T>
void UpdateConstantBuffer(ID3D11DeviceContext* context, ID3D11Buffer* buffer, const T& data) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &data, sizeof(T));
        context->Unmap(buffer, 0);
    }
}
}

bool Renderer::Initialize(ID3D11Device* device) {
    if (!device) return false;
    Reset();
    m_device = device;
    m_device->GetImmediateContext(m_context.GetAddressOf());
    return CreateDeviceContextPieces();
}

void Renderer::Reset() {
    ReleaseTargets();
    m_staging.Reset();
    m_stagingWidth = 0;
    m_stagingHeight = 0;
    m_vertexBuffer.Reset();
    m_vertexCount = 0;
    m_radius = 1.0f;
    m_fit = FitFrame{};
    m_fitValid = false;
    m_fitPoints.clear();
    m_context.Reset();
    m_device.Reset();
}

bool Renderer::CreateDeviceContextPieces() {
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (!CompileShader(kModelShaderSource, "VSMain", "vs_5_0", vsBlob)) return false;
    if (!CompileShader(kModelShaderSource, "PSMain", "ps_5_0", psBlob)) return false;
    if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr,
                                            m_modelVs.GetAddressOf())))
        return false;
    if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr,
                                           m_modelPs.GetAddressOf())))
        return false;

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(m_device->CreateInputLayout(layout, ARRAYSIZE(layout), vsBlob->GetBufferPointer(),
                                           vsBlob->GetBufferSize(), m_layout.GetAddressOf())))
        return false;

    if (!CompileShader(kBackgroundShaderSource, "VSFullscreen", "vs_5_0", vsBlob)) return false;
    if (!CompileShader(kBackgroundShaderSource, "PSBackground", "ps_5_0", psBlob)) return false;
    if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr,
                                            m_fullscreenVs.GetAddressOf())))
        return false;
    if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr,
                                           m_backgroundPs.GetAddressOf())))
        return false;

    if (!CompileShader(kBlitShaderSource, "PSBlit", "ps_5_0", psBlob)) return false;
    if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr,
                                           m_blitPs.GetAddressOf())))
        return false;

    if (!CreateConstantBuffer<CameraConstants>(m_device.Get(), m_cameraCb)) return false;
    if (!CreateConstantBuffer<BackgroundConstants>(m_device.Get(), m_backgroundCb)) return false;

    D3D11_DEPTH_STENCIL_DESC depthOnDesc{};
    depthOnDesc.DepthEnable = TRUE;
    depthOnDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthOnDesc.DepthFunc = D3D11_COMPARISON_LESS;
    if (FAILED(m_device->CreateDepthStencilState(&depthOnDesc, m_depthOn.GetAddressOf()))) return false;

    D3D11_DEPTH_STENCIL_DESC depthOffDesc{};
    depthOffDesc.DepthEnable = FALSE;
    depthOffDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depthOffDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (FAILED(m_device->CreateDepthStencilState(&depthOffDesc, m_depthOff.GetAddressOf()))) return false;

    D3D11_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.FrontCounterClockwise = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    rasterizerDesc.MultisampleEnable = TRUE;
    if (FAILED(m_device->CreateRasterizerState(&rasterizerDesc, m_rasterizer.GetAddressOf()))) return false;

    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(m_device->CreateSamplerState(&samplerDesc, m_sampler.GetAddressOf()))) return false;

    return true;
}

bool Renderer::SetMesh(const Mesh& mesh) {
    m_vertexBuffer.Reset();
    m_vertexCount = 0;
    m_radius = mesh.radius;
    m_fit = FitFrame{};
    m_fitValid = false;
    for (int i = 0; i < 6; ++i) m_bounds[i] = mesh.bounds[i];

    m_fitPoints.clear();
    const size_t vertexCount = mesh.vertices.size() / 6;
    const size_t maxPoints = 20000;
    const size_t step = vertexCount > maxPoints ? (vertexCount + maxPoints - 1) / maxPoints : 1;
    m_fitPoints.reserve(vertexCount / step + 1);
    for (size_t v = 0; v < vertexCount; v += step) {
        m_fitPoints.push_back({mesh.vertices[v * 6 + 0], mesh.vertices[v * 6 + 1], mesh.vertices[v * 6 + 2]});
    }

    if (!m_device || mesh.vertices.empty()) return false;

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(mesh.vertices.size() * sizeof(float));
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = mesh.vertices.data();

    if (FAILED(m_device->CreateBuffer(&desc, &data, m_vertexBuffer.GetAddressOf()))) {
        m_vertexBuffer.Reset();
        return false;
    }
    m_vertexCount = static_cast<UINT>(mesh.vertices.size() / 6);
    return true;
}

void Renderer::ReleaseTargets() {
    m_targets = Targets{};
    m_width = 0;
    m_height = 0;
    m_samples = 0;
}

bool Renderer::EnsureTargets(UINT width, UINT height, UINT samples) {
    if (!m_device) return false;
    if (width == 0 || height == 0) return false;
    if (samples == 0) samples = 1;

    UINT effectiveSamples = samples;
    if (effectiveSamples > 1) {
        UINT quality = 0;
        if (FAILED(m_device->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, effectiveSamples, &quality)) ||
            quality == 0) {
            effectiveSamples = 1;
        }
    }

    if (m_targets.color && width == m_width && height == m_height && effectiveSamples == m_samples) return true;

    ReleaseTargets();

    auto create = [&](UINT sampleCount) -> bool {
        D3D11_TEXTURE2D_DESC colorDesc{};
        colorDesc.Width = width;
        colorDesc.Height = height;
        colorDesc.MipLevels = 1;
        colorDesc.ArraySize = 1;
        colorDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        colorDesc.SampleDesc.Count = sampleCount;
        colorDesc.Usage = D3D11_USAGE_DEFAULT;
        colorDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (sampleCount == 1) colorDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(m_device->CreateTexture2D(&colorDesc, nullptr, m_targets.color.GetAddressOf()))) {
            ReleaseTargets();
            return false;
        }
        if (FAILED(m_device->CreateRenderTargetView(m_targets.color.Get(), nullptr,
                                                    m_targets.colorRtv.GetAddressOf()))) {
            ReleaseTargets();
            return false;
        }

        D3D11_TEXTURE2D_DESC depthDesc = colorDesc;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        if (FAILED(m_device->CreateTexture2D(&depthDesc, nullptr, m_targets.depth.GetAddressOf()))) {
            ReleaseTargets();
            return false;
        }
        if (FAILED(m_device->CreateDepthStencilView(m_targets.depth.Get(), nullptr,
                                                    m_targets.depthDsv.GetAddressOf()))) {
            ReleaseTargets();
            return false;
        }

        if (sampleCount > 1) {
            D3D11_TEXTURE2D_DESC resolvedDesc = colorDesc;
            resolvedDesc.SampleDesc.Count = 1;
            resolvedDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(m_device->CreateTexture2D(&resolvedDesc, nullptr, m_targets.resolved.GetAddressOf()))) {
                ReleaseTargets();
                return false;
            }
            if (FAILED(m_device->CreateShaderResourceView(m_targets.resolved.Get(), nullptr,
                                                          m_targets.resolvedSrv.GetAddressOf()))) {
                ReleaseTargets();
                return false;
            }
        } else {
            m_targets.resolved = m_targets.color;
            if (FAILED(m_device->CreateShaderResourceView(m_targets.resolved.Get(), nullptr,
                                                          m_targets.resolvedSrv.GetAddressOf()))) {
                ReleaseTargets();
                return false;
            }
        }

        m_width = width;
        m_height = height;
        m_samples = sampleCount;
        return true;
    };

    if (create(effectiveSamples)) return true;
    if (effectiveSamples > 1) return create(1);
    return false;
}

Renderer::FitFrame Renderer::ComputeFit(const DirectX::XMMATRIX& view, float aspect) const {
    FitFrame frame;
    if (m_fitPoints.empty()) {
        frame.halfBase = m_radius;
        return frame;
    }

    float minX = FLT_MAX, maxX = -FLT_MAX, minY = FLT_MAX, maxY = -FLT_MAX;
    for (const DirectX::XMFLOAT3& source : m_fitPoints) {
        DirectX::XMFLOAT3 projected{};
        DirectX::XMStoreFloat3(&projected, DirectX::XMVector3TransformCoord(
                                                DirectX::XMVectorSet(source.x, source.y, source.z, 1.0f), view));
        minX = std::min(minX, projected.x);
        maxX = std::max(maxX, projected.x);
        minY = std::min(minY, projected.y);
        maxY = std::max(maxY, projected.y);
    }

    frame.centerX = (minX + maxX) * 0.5f;
    frame.centerY = (minY + maxY) * 0.5f;
    const float extentX = (maxX - minX) * 0.5f;
    const float extentY = (maxY - minY) * 0.5f;
    frame.halfBase = std::max(extentY, extentX / std::max(aspect, 0.01f)) * 1.10f;
    return frame;
}

bool Renderer::Render(UINT width, UINT height, const CameraState& camera, UINT samples, float fitSmoothing) {
    if (!m_context) return false;
    if (!EnsureTargets(width, height, samples)) return false;

    const float radius = std::max(m_radius, 1e-4f);
    const float azimuth = DirectX::XMConvertToRadians(camera.azimuthDeg);
    const float elevation = DirectX::XMConvertToRadians(std::clamp(camera.elevationDeg, -89.0f, 89.0f));
    const float distance = radius * 4.0f;

    const DirectX::XMVECTOR eye = DirectX::XMVectorScale(
        DirectX::XMVectorSet(std::cos(elevation) * std::sin(azimuth), std::sin(elevation),
                             std::cos(elevation) * std::cos(azimuth), 0.0f),
        distance);
    const DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eye, DirectX::XMVectorZero(),
                                                             DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));

    const float aspect = static_cast<float>(width) / static_cast<float>(height);

    const FitFrame target = ComputeFit(view, aspect);
    const float smoothing = m_fitValid ? std::clamp(fitSmoothing, 0.0f, 1.0f) : 1.0f;
    if (!m_fitValid) {
        m_fit = target;
        m_fitValid = true;
    } else {
        m_fit.centerX += (target.centerX - m_fit.centerX) * smoothing;
        m_fit.centerY += (target.centerY - m_fit.centerY) * smoothing;
        m_fit.halfBase += (target.halfBase - m_fit.halfBase) * smoothing;
    }

    const float halfHeight = std::max(m_fit.halfBase, 1e-4f) / std::max(camera.zoom, 0.05f);
    const float halfWidth = halfHeight * aspect;

    const DirectX::XMMATRIX projection = DirectX::XMMatrixOrthographicOffCenterLH(
        m_fit.centerX - halfWidth, m_fit.centerX + halfWidth, m_fit.centerY - halfHeight,
        m_fit.centerY + halfHeight, distance - radius * 2.0f, distance + radius * 2.0f);

    CameraConstants cameraConstants{};
    DirectX::XMStoreFloat4x4(&cameraConstants.viewProj,
                             DirectX::XMMatrixMultiply(view, projection));
    DirectX::XMStoreFloat4x4(&cameraConstants.view, view);
    cameraConstants.baseColor = {0.74f, 0.77f, 0.81f, 1.0f};

    BackgroundConstants backgroundConstants{};
    const DirectX::XMFLOAT3& bg = camera.background;
    backgroundConstants.topColor = {
        bg.x + (1.0f - bg.x) * 0.18f,
        bg.y + (1.0f - bg.y) * 0.18f,
        bg.z + (1.0f - bg.z) * 0.18f,
        1.0f};
    backgroundConstants.bottomColor = {bg.x * 0.93f, bg.y * 0.93f, bg.z * 0.93f, 1.0f};

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    ID3D11RenderTargetView* renderTargets[] = {m_targets.colorRtv.Get()};
    m_context->OMSetRenderTargets(1, renderTargets, m_targets.depthDsv.Get());

    const float clearColor[4] = {bg.x, bg.y, bg.z, 1.0f};
    m_context->ClearRenderTargetView(m_targets.colorRtv.Get(), clearColor);
    m_context->ClearDepthStencilView(m_targets.depthDsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

    m_context->RSSetState(m_rasterizer.Get());
    m_context->OMSetDepthStencilState(m_depthOff.Get(), 0);
    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_fullscreenVs.Get(), nullptr, 0);
    m_context->PSSetShader(m_backgroundPs.Get(), nullptr, 0);
    UpdateConstantBuffer(m_context.Get(), m_backgroundCb.Get(), backgroundConstants);
    ID3D11Buffer* backgroundBuffer = m_backgroundCb.Get();
    m_context->PSSetConstantBuffers(0, 1, &backgroundBuffer);
    m_context->Draw(3, 0);

    if (m_vertexBuffer && m_vertexCount > 0) {
        m_context->OMSetDepthStencilState(m_depthOn.Get(), 0);
        m_context->RSSetState(m_rasterizer.Get());
        m_context->IASetInputLayout(m_layout.Get());
        m_context->VSSetShader(m_modelVs.Get(), nullptr, 0);
        m_context->PSSetShader(m_modelPs.Get(), nullptr, 0);

        UpdateConstantBuffer(m_context.Get(), m_cameraCb.Get(), cameraConstants);
        ID3D11Buffer* cameraBuffer = m_cameraCb.Get();
        m_context->VSSetConstantBuffers(0, 1, &cameraBuffer);
        m_context->PSSetConstantBuffers(0, 1, &cameraBuffer);

        const UINT stride = 6 * sizeof(float);
        const UINT offset = 0;
        ID3D11Buffer* vertexBuffer = m_vertexBuffer.Get();
        m_context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        m_context->Draw(m_vertexCount, 0);
    }

    if (m_samples > 1) {
        m_context->ResolveSubresource(m_targets.resolved.Get(), 0, m_targets.color.Get(), 0,
                                      DXGI_FORMAT_B8G8R8A8_UNORM);
    }
    return true;
}

void Renderer::Blit(ID3D11RenderTargetView* target, UINT width, UINT height) {
    if (!m_context || !target || !m_targets.resolvedSrv) return;

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    ID3D11RenderTargetView* renderTargets[] = {target};
    m_context->OMSetRenderTargets(1, renderTargets, nullptr);
    m_context->OMSetDepthStencilState(m_depthOff.Get(), 0);
    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_fullscreenVs.Get(), nullptr, 0);
    m_context->PSSetShader(m_blitPs.Get(), nullptr, 0);

    ID3D11ShaderResourceView* sources[] = {m_targets.resolvedSrv.Get()};
    m_context->PSSetShaderResources(0, 1, sources);
    ID3D11SamplerState* samplers[] = {m_sampler.Get()};
    m_context->PSSetSamplers(0, 1, samplers);
    m_context->Draw(3, 0);

    ID3D11ShaderResourceView* nullSource[] = {nullptr};
    m_context->PSSetShaderResources(0, 1, nullSource);
}

bool Renderer::ReadPixels(std::vector<uint8_t>& pixels, UINT& width, UINT& height) {
    if (!m_context || !m_targets.resolved) return false;

    if (!m_staging || m_stagingWidth != m_width || m_stagingHeight != m_height) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = m_width;
        desc.Height = m_height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        m_staging.Reset();
        if (FAILED(m_device->CreateTexture2D(&desc, nullptr, m_staging.GetAddressOf()))) return false;
        m_stagingWidth = m_width;
        m_stagingHeight = m_height;
    }

    m_context->CopyResource(m_staging.Get(), m_targets.resolved.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_context->Map(m_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;

    const size_t rowBytes = static_cast<size_t>(m_width) * 4;
    pixels.resize(rowBytes * m_height);
    const auto* source = static_cast<const uint8_t*>(mapped.pData);
    for (UINT row = 0; row < m_height; ++row) {
        std::memcpy(pixels.data() + row * rowBytes, source + static_cast<size_t>(row) * mapped.RowPitch, rowBytes);
    }
    m_context->Unmap(m_staging.Get(), 0);

    width = m_width;
    height = m_height;
    return true;
}

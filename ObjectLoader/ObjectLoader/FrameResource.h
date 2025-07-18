#pragma once

#include "../../Common/d3dUtil.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "VertexData.h"

struct UnlitObjectConstants
{
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
};

struct OpaqueObjectConstants : public UnlitObjectConstants
{
    DirectX::XMFLOAT4X4 WorldInvTranspose = MathHelper::Identity4x4();
    DirectX::XMUINT2 normalMapSize = { 0, 0 };
    uint32_t useNormalMap = 0;
    uint32_t useDisplacementMap = 0;
    float displacementScale = 1.0f;
    float pad[3];
};

struct GBufferPassConstants
{
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    float DeltaTime = 0.0f;
    DirectX::XMFLOAT2 ScreenSize = { 0.0f, 0.0f };
    float pad[2] = { 0.f, 0.f };
};

struct LightingPassConstants
{
    DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    float pad1 = 0.f;
    DirectX::XMFLOAT2 RTSize = { 1.0f, 1.0f };
    DirectX::XMFLOAT2 mousePosition = { 0.0f, 0.0f };
};


struct Light
{
    Light(bool enabled = true)
        : active{ enabled ? 1 : 0 }
    {}
    DirectX::XMMATRIX world = DirectX::XMMatrixIdentity();
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    int type = 0;
    DirectX::XMFLOAT3 direction = { 0.0f, -1.0f, 0.0f };
    float radius = .5f;
    DirectX::XMFLOAT3 color = { 1.0f, 1.0f, 1.0f };
    float angle = 1.f;
    int active = 1;
    float intensity = 10.f;
    float padding[2]{ 0.f, 0.f };
};

struct DirectionalLightConstants
{
    DirectX::XMFLOAT3 mainLightDirection = { 0.0f, 0.0f, 0.0f };
    int mainLightIsOn = 1;
    DirectX::XMFLOAT3 gLightColor = { 1.0f, 1.0f, 1.0f };
    float pad = 0.0f;
    Light mainSpotlight = Light();
};

// Stores the resources needed for the CPU to build the command lists
// for a frame.  
struct FrameResource
{
public:

    FrameResource(ID3D12Device* device, UINT passCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();

    void addOpaqueObjectBuffer(ID3D12Device* device, std::uint32_t uid);
    void removeOpaqueObjectBuffer(ID3D12Device* device, std::uint32_t uid);

    void addUnlitObjectBuffer(ID3D12Device* device, std::uint32_t uid);
    void removeUnlitObjectBuffer(ID3D12Device* device, std::uint32_t uid);

    // We cannot reset the allocator until the GPU is done processing the commands.
    // So each frame needs their own allocator.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

    // We cannot update a cbuffer until the GPU is done processing the commands
    // that reference it.  So each frame needs their own cbuffers.
   // std::unique_ptr<UploadBuffer<FrameConstants>> FrameCB = nullptr;
    std::unique_ptr<UploadBuffer<GBufferPassConstants>> GBufferPassCB = nullptr;
    std::unique_ptr<UploadBuffer<LightingPassConstants>> LightingPassCB = nullptr;
    std::unique_ptr<UploadBuffer<DirectionalLightConstants>> DirLightCB = nullptr;
    std::unique_ptr<UploadBuffer<Light>> LocalLightCB = nullptr;

    std::unordered_map<std::uint32_t, std::unique_ptr<UploadBuffer<OpaqueObjectConstants>>> OpaqueObjCB = {};
    std::unordered_map<std::uint32_t, std::unique_ptr<UploadBuffer<UnlitObjectConstants>>> UnlitObjCB = {};

    // Fence value to mark commands up to this fence point.  This lets us
    // check if these frame resources are still in use by the GPU.
    UINT64 Fence = 0;

    static std::vector<std::unique_ptr<FrameResource>>& frameResources();
};
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
};

struct MaterialConstants
{
    int useBaseColorMap = 0;
    int useEmissiveMap = 0;
    int useOpacityMap = 0;
    int useRoughnessMap = 0;

    DirectX::XMFLOAT3 baseColor = { 1.0f, 1.0f, 1.0f };
    int useMetallicMap = 0;
    
    int useNormalMap = 0;
    int useAOMap = 0;
    int useDisplacementMap = 0;
    float emissiveIntensity = 0.0f;

    DirectX::XMFLOAT3 emissive = { 0.0f, 0.0f, 0.0f };
    float opacity = 1.f;
    
    float roughness = 1.f;
    float metallic = 1.f;
    float displacementScale = 0.0f;
    int useARMMap = 0;

    int ARMLayout = 0;
    float pad[3] = {};
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
    //0 - point, 1 - spotlight
    int type = 0;
    DirectX::XMFLOAT3 direction = { 0.0f, -1.0f, 0.0f };
    float radius = 5.f;
    DirectX::XMFLOAT3 color = { 1.0f, 1.0f, 1.0f };
    float angle = 1.f;
    int active = 1;
    float intensity = 10.f;
    float padding[2]{ 0.f, 0.f };
};

static const int gCascadesCount = 3;

struct Cascade
{
    float splitNear = 0.0f;
    float splitFar = 100.0f;
    DirectX::XMMATRIX viewProj = DirectX::XMMatrixIdentity();
};

struct CascadeOnCPU
{
    Cascade cascade;
    DirectX::BoundingBox AABB;
    DirectX::XMMATRIX lightView = DirectX::XMMatrixIdentity();
    int shadowMapDSV = 0;
};

struct DirectionalLightConstants
{
    DirectX::XMFLOAT3 mainLightDirection = { 0.0f, 0.0f, 0.0f };
    int mainLightIsOn = 1;
    DirectX::XMFLOAT3 gLightColor = { 1.0f, 1.0f, 1.0f };
    int lightsContainingFrustum = 0;
    Light mainSpotlight = Light();
    Cascade cascades[gCascadesCount];
};

struct ShadowLightConstants
{
    DirectX::XMMATRIX lightMatrix = DirectX::XMMatrixIdentity();
};

struct LightIndex
{
    LightIndex(int idx)
        : index{ idx }
        { }
    int index;
    int pad[3] = { 0, 0, 0 };
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

    void addOpaqueObjectBuffer(ID3D12Device* device, std::uint32_t uid, int meshesCount, int materialsCount);
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
    std::unique_ptr<UploadBuffer<ShadowLightConstants>> ShadowDirLightCB = nullptr;
    std::unique_ptr<UploadBuffer<ShadowLightConstants>> ShadowLocalLightCB = nullptr;
    std::unique_ptr<UploadBuffer<Light>> LocalLightCB = nullptr;
    std::unique_ptr<UploadBuffer<LightIndex>> LightsInsideFrustum = nullptr;
    std::unique_ptr<UploadBuffer<LightIndex>> LightsContainingFrustum = nullptr;

    std::unordered_map<std::uint32_t, std::unique_ptr<UploadBuffer<OpaqueObjectConstants>>> OpaqueObjCB = {};
    std::unordered_map<std::uint32_t, std::unique_ptr<UploadBuffer<MaterialConstants>>> MaterialCB = {};
    std::unordered_map<std::uint32_t, std::unique_ptr<UploadBuffer<UnlitObjectConstants>>> UnlitObjCB = {};

    // Fence value to mark commands up to this fence point.  This lets us
    // check if these frame resources are still in use by the GPU.
    UINT64 Fence = 0;

    static std::vector<std::unique_ptr<FrameResource>>& frameResources();
};
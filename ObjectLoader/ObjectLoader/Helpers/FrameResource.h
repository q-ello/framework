#pragma once

#include "../../../Common/d3dUtil.h"
#include "../../../Common/MathHelper.h"
#include "../../../Common/UploadBuffer.h"
#include "VertexData.h"

struct StaticObjectConstants
{
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
};

struct OpaqueObjectConstants : public StaticObjectConstants
{
    DirectX::XMFLOAT4X4 WorldInvTranspose = MathHelper::Identity4x4();
};

struct MaterialConstants
{
    int UseBaseColorMap = 0;
    int UseEmissiveMap = 0;
    int UseOpacityMap = 0;
    int UseRoughnessMap = 0;

    DirectX::XMFLOAT3 BaseColor = { 1.0f, 1.0f, 1.0f };
    int UseMetallicMap = 0;
    
    int UseNormalMap = 0;
    int UseAoMap = 0;
    int UseDisplacementMap = 0;
    float EmissiveIntensity = 0.0f;

    DirectX::XMFLOAT3 Emissive = { 0.0f, 0.0f, 0.0f };
    float Opacity = 1.f;
    
    float Roughness = 1.f;
    float Metallic = 1.f;
    float DisplacementScale = 0.0f;
    int UseArmMap = 0;

    int ArmLayout = 0;
    float Pad[3] = {};
};

struct GBufferPassConstants
{
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    float DeltaTime = 0.0f;
    DirectX::XMFLOAT2 ScreenSize = { 0.0f, 0.0f };
	UINT FrameIndex = 0;
    int TaaEnabled = 0;
};

struct LightingPassConstants
{
    DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    float Pad1 = 0.f;
    DirectX::XMFLOAT2 RtSize = { 1.0f, 1.0f };
    DirectX::XMFLOAT2 MousePosition = { 0.0f, 0.0f };
};

struct Light
{
    explicit Light(const bool enabled = true);
    DirectX::XMMATRIX World = DirectX::XMMatrixIdentity();
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
    //0 - point, 1 - spotlight
    int Type = 0;
    DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };
    float Radius = 5.f;
    DirectX::XMFLOAT3 Color = { 1.0f, 1.0f, 1.0f };
    float Angle = 1.f;
    int Active = 1;
    float Intensity = 10.f;
    float Padding[2]{ 0.f, 0.f };
};

static constexpr int gCascadesCount = 3;

struct Cascade
{
    float SplitNear = 0.0f;
    float SplitFar = 100.0f;
    DirectX::XMMATRIX ViewProj = DirectX::XMMatrixIdentity();
};

struct CascadeOnCpu
{
    Cascade Cascade;
    DirectX::BoundingBox Aabb;
    DirectX::XMMATRIX LightView = DirectX::XMMatrixIdentity();
    int ShadowMapDsv = 0;
};

struct DirectionalLightConstants
{
    DirectX::XMFLOAT3 MainLightDirection = { 0.0f, 0.0f, 0.0f };
    int MainLightIsOn = 1;
    DirectX::XMFLOAT3 LightColor = { 1.0f, 1.0f, 1.0f };
    int LightsContainingFrustum = 0;
    Light MainSpotlight = Light();
    Cascade Cascades[gCascadesCount];
};

struct ShadowLightConstants
{
    DirectX::XMMATRIX LightMatrix = DirectX::XMMatrixIdentity();
};

struct LightIndex
{
    explicit LightIndex(const int idx)
        : Index{ idx }
        { }
    int Index;
    int Pad[3] = { 0, 0, 0 };
};

struct GodRaysConstants
{
    int SamplesCount = 40;
    float Decay = 0.98f;
    float Exposure = 0.92f;
    float Density = 0.96f;
    float Weight = 0.58f;
    float Pad[3];
};

struct SsrConstants
{
    DirectX::XMMATRIX InvProj = DirectX::XMMatrixIdentity();
    int StepScale = 1;
    int MaxSteps = 200;
    int MaxScreenDistance = 500;
    float Pad = 0.0f;
};

struct GridInfo
{
    DirectX::XMMATRIX World = DirectX::XMMatrixIdentity();
    int TexelXStart = 0;
    int TexelYStart = 0;
    int TexelStride = 1;
    int Pad = 0;
};

struct TerrainConstants
{
	DirectX::XMINT2 HeightmapSize = { 0, 0 };
    float MaxHeight = 1.0f;
    int GridSize = 33;
    float HeightThreshold = 0.5f;
    float SlopeThreshold = 0.5f;
    float Pad[2];
};

struct TerrainTextures
{
    int UseLowTexture;
    DirectX::XMFLOAT3 LowColor;
    int UseSlopeTexture;
    DirectX::XMFLOAT3 SlopeColor;
    int UseHighTexture;
    DirectX::XMFLOAT3 HighColor;
};

struct TaaConstants
{
	DirectX::XMFLOAT4X4 PrevView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 PrevProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 PrevInvProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 CurrInvView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 CurrInvProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT2 ScreenSize = { 0.0f, 0.0f };
	float Pad[2] = {0.0f, 0.0f};
};

struct AtmosphereConstants
{
    DirectX::XMFLOAT3 DirToSun = {1.0f, 1.0f, 1.0f};
    float AtmosphereRadius = 1050.0f;
    DirectX::XMFLOAT3 PlanetCenter = {0.0f, -1000.0f, 0.0f};
    float PlanetRadius = 1000.0f;
    DirectX::XMFLOAT3 Wavelengths = {700.0f, 530.0f, 440.0f};
    int NumInScatteringPoints = 10;
    int NumOpticalDepthPoints = 10;
    float DensityFalloff = 4;
    int Pad[2];
};

// Stores the resources needed for the CPU to build the command lists
// for a frame.  
struct FrameResource
{
public:
    FrameResource(ID3D12Device* device, UINT passCount);
    ~FrameResource() = default;
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    FrameResource(const FrameResource&& rhs) = delete;
    FrameResource& operator=(const FrameResource&& rhs) = delete;
    

    void AddOpaqueObjectBuffer(ID3D12Device* device, std::uint32_t uid, int meshesCount, int materialsCount);
    void RemoveOpaqueObjectBuffer(ID3D12Device* device, std::uint32_t uid);

    // We cannot reset the allocator until the GPU is done processing the commands.
    // So each frame needs their own allocator.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

    // We cannot update a cbuffer until the GPU is done processing the commands
    // that reference it.  So each frame needs their own cbuffers.
    std::unique_ptr<UploadBuffer<GBufferPassConstants>> GBufferPassCb = nullptr;
    std::unique_ptr<UploadBuffer<LightingPassConstants>> LightingPassCb = nullptr;
    std::unique_ptr<UploadBuffer<DirectionalLightConstants>> DirLightCb = nullptr;

    std::unique_ptr<UploadBuffer<ShadowLightConstants>> ShadowDirLightCb = nullptr;
    std::unique_ptr<UploadBuffer<ShadowLightConstants>> ShadowLocalLightCb = nullptr;
    
    std::unique_ptr<UploadBuffer<Light>> LocalLightCb = nullptr;
    std::unique_ptr<UploadBuffer<LightIndex>> LightsInsideFrustum = nullptr;
    std::unique_ptr<UploadBuffer<LightIndex>> LightsContainingFrustum = nullptr;
    
    std::unique_ptr<UploadBuffer<StaticObjectConstants>> StaticObjCb = nullptr;
    
    std::unique_ptr<UploadBuffer<GodRaysConstants>> GodRaysCb = nullptr;
    std::unique_ptr<UploadBuffer<SsrConstants>> SsrCb = nullptr;

	std::unique_ptr<UploadBuffer<TerrainConstants>> TerrainCb = nullptr;
	std::unique_ptr<UploadBuffer<GridInfo>> GridInfoCb = nullptr;
    std::unique_ptr<UploadBuffer<TerrainTextures>> TerrainTexturesCb = nullptr;

	std::unique_ptr<UploadBuffer<TaaConstants>> TaaCb = nullptr;
	std::unique_ptr<UploadBuffer<AtmosphereConstants>> AtmosphereCb = nullptr;

    std::unordered_map<std::uint32_t, std::unique_ptr<UploadBuffer<OpaqueObjectConstants>>> OpaqueObjCb = {};
    std::unordered_map<std::uint32_t, std::unique_ptr<UploadBuffer<MaterialConstants>>> MaterialCb = {};
    static UINT StaticObjectCount;

    // Fence value to mark commands up to this fence point.  This lets us
    // check if these frame resources are still in use by the GPU.
    UINT64 Fence = 0;

    static std::vector<std::unique_ptr<FrameResource>>& FrameResources();
};
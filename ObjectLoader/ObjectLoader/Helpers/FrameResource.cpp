#include "FrameResource.h"

#include <d3d12.h>
#include <intsafe.h>

UINT FrameResource::StaticObjectCount = 0;

FrameResource::FrameResource(ID3D12Device* device, UINT passCount)
{
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

    GBufferPassCb = std::make_unique<UploadBuffer<GBufferPassConstants>>(device, passCount, true);
    LightingPassCb = std::make_unique < UploadBuffer <LightingPassConstants>> (device, passCount, true);
    DirLightCb = std::make_unique<UploadBuffer<DirectionalLightConstants>>(device, passCount, true);
    
    ShadowDirLightCb = std::make_unique<UploadBuffer<ShadowLightConstants>>(device, gCascadesCount, true);
    ShadowLocalLightCb = std::make_unique<UploadBuffer<ShadowLightConstants>>(device, 512, false);
    LocalLightCb = std::make_unique<UploadBuffer<Light>>(device, 512, false);

    LightsContainingFrustum = std::make_unique<UploadBuffer<LightIndex>>(device, 512, false);
    LightsInsideFrustum = std::make_unique<UploadBuffer<LightIndex>>(device, 512, false);

    GodRaysCb = std::make_unique<UploadBuffer<GodRaysConstants>>(device, passCount, true);
    StaticObjCb = std::make_unique < UploadBuffer<StaticObjectConstants>>(device, 512, true);
    SsrCb = std::make_unique<UploadBuffer<SsrConstants>>(device, passCount, true);

	TerrainCb = std::make_unique<UploadBuffer<TerrainConstants>>(device, passCount, true);
	GridInfoCb = std::make_unique<UploadBuffer<GridInfo>>(device, 8192, false);

	TaaCb = std::make_unique<UploadBuffer<TaaConstants>>(device, passCount, true);
    AtmosphereCb = std::make_unique<UploadBuffer<AtmosphereConstants>>(device, passCount, true);
}

void FrameResource::AddOpaqueObjectBuffer(ID3D12Device* device, std::uint32_t uid, int meshesCount, int materialsCount)
{
    OpaqueObjCb[uid] = std::make_unique<UploadBuffer<OpaqueObjectConstants>>(device, meshesCount, true);
    MaterialCb[uid] = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialsCount, true);
}

void FrameResource::RemoveOpaqueObjectBuffer(ID3D12Device* device, std::uint32_t uid)
{
    OpaqueObjCb.erase(uid);
    MaterialCb.erase(uid);
}

std::vector<std::unique_ptr<FrameResource>>& FrameResource::FrameResources()
{
    static std::vector<std::unique_ptr<FrameResource>> frameResources;
    return frameResources;
}

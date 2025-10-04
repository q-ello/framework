#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device* device, UINT passCount)
{
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

    GBufferPassCB = std::make_unique<UploadBuffer<GBufferPassConstants>>(device, passCount, true);
    LightingPassCB = std::make_unique < UploadBuffer <LightingPassConstants>> (device, passCount, true);
    DirLightCB = std::make_unique<UploadBuffer<DirectionalLightConstants>>(device, passCount, true);
    ShadowDirLightCB = std::make_unique<UploadBuffer<ShadowLightConstants>>(device, 1, true);
    ShadowLocalLightCB = std::make_unique<UploadBuffer<ShadowLightConstants>>(device, 512, false);
    LocalLightCB = std::make_unique<UploadBuffer<Light>>(device, 512, false);
    LightsContainingFrustum = std::make_unique<UploadBuffer<LightIndex>>(device, 512, false);
    LightsInsideFrustum = std::make_unique<UploadBuffer<LightIndex>>(device, 512, false);
    ObjCB = std::make_unique<UploadBuffer<OpaqueObjectConstants>>(device, 125, false);
}

FrameResource::~FrameResource()
{

}

void FrameResource::addOpaqueObjectBuffer(ID3D12Device* device, std::uint32_t uid, int meshesCount, int materialsCount)
{
    OpaqueObjCB[uid] = std::make_unique<UploadBuffer<OpaqueObjectConstants>>(device, meshesCount, true);
    MaterialCB[uid] = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialsCount, true);
}

void FrameResource::removeOpaqueObjectBuffer(ID3D12Device* device, std::uint32_t uid)
{
    OpaqueObjCB.erase(uid);
    MaterialCB.erase(uid);
}

void FrameResource::addUnlitObjectBuffer(ID3D12Device* device, std::uint32_t uid)
{
    UnlitObjCB[uid] = std::make_unique<UploadBuffer<UnlitObjectConstants>>(device, 1, true);
}

void FrameResource::removeUnlitObjectBuffer(ID3D12Device* device, std::uint32_t uid)
{
    UnlitObjCB.erase(uid);
}

std::vector<std::unique_ptr<FrameResource>>& FrameResource::frameResources()
{
    static std::vector<std::unique_ptr<FrameResource>> frameResources;
    return frameResources;
}

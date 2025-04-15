#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount)
{
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

    //  FrameCB = std::make_unique<UploadBuffer<FrameConstants>>(device, 1, true);
    PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
    MaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, true);
}

FrameResource::~FrameResource()
{

}

void FrameResource::addObjectBuffer(ID3D12Device* device, std::uint32_t uid)
{
    ObjectsCB[uid] = std::make_unique<UploadBuffer<ObjectConstants>>(device, 1, true);
}

void FrameResource::removeObjectBuffer(ID3D12Device* device, std::uint32_t uid)
{
    ObjectsCB.erase(uid);
}

#include "RayTracingManager.h"

#include "GeometryManager.h"
#include "UploadManager.h"

void RayTracingManager::UpdateData(ID3D12GraphicsCommandList4* cmdList)
{
    for (const auto& obj : _rtObjects)
    {
        if (obj->RayTracingDirty)
        {
            BuildTlas(cmdList);
            return;
        }
    }
}

void RayTracingManager::BuildTlas(ID3D12GraphicsCommandList4* cmdList)
{
    if (_rtObjects.empty() && _instanceCount == 0)
    {
        return;
    }

    _instanceCount = static_cast<UINT>(_rtObjects.size());
    
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
    instanceDescs.reserve(_rtObjects.size());
    
    for (const auto& ri : _rtObjects)
    {
        D3D12_RAYTRACING_INSTANCE_DESC desc;
        const XMMATRIX world = ri->World;
        XMFLOAT4X4 worldF;
        XMStoreFloat4x4(&worldF, world);

        {
            desc.Transform[0][0] = worldF._11;
            desc.Transform[0][1] = worldF._21;
            desc.Transform[0][2] = worldF._31;
            desc.Transform[0][3] = worldF._41;
            desc.Transform[1][0] = worldF._12;
            desc.Transform[1][1] = worldF._22;
            desc.Transform[1][2] = worldF._32;
            desc.Transform[1][3] = worldF._42;
            desc.Transform[2][0] = worldF._13;
            desc.Transform[2][1] = worldF._23;
            desc.Transform[2][2] = worldF._33;
            desc.Transform[2][3] = worldF._43;
        }

        const auto& mesh = GeometryManager::Geometries()[ri->Name][ri->CurrentLodIdx];
        desc.AccelerationStructure = mesh->Rt->Blas->GetGPUVirtualAddress();

        desc.InstanceID = ri->Uid;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.InstanceContributionToHitGroupIndex = 0;
        desc.InstanceMask = 0xFF;

        instanceDescs.push_back(desc);

        ri->RayTracingDirty = false;
    }

    
    // 2. Upload instance buffer
    const UINT bufferSize =
        static_cast<UINT>(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instanceDescs.size());

    if (!_instanceBuffer || _instanceBufferSize < bufferSize)
    {
        _instanceBufferSize = bufferSize;
        _instanceBuffer.Reset();

        _instanceBuffer = UploadManager::CreateUploadBuffer(bufferSize);
    }

    void* mapped = nullptr;
    ThrowIfFailed(_instanceBuffer->Map(0, nullptr, &mapped));
    memcpy(mapped, instanceDescs.data(), bufferSize);
    _instanceBuffer->Unmap(0, nullptr);

    // 3. TLAS inputs
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = static_cast<UINT>(instanceDescs.size());
    inputs.InstanceDescs =
        _instanceBuffer->GetGPUVirtualAddress();
    inputs.Flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    
    // 4. Prebuild info
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
        UploadManager::Device->GetRaytracingAccelerationStructurePrebuildInfo(
            &inputs, &info);

    // 5. Allocate scratch & TLAS
    _scratch = UploadManager::CreateUavBuffer(info.ScratchDataSizeInBytes);
    _tlas = UploadManager::CreateAsBuffer(info.ResultDataMaxSizeInBytes);

    // 6. Build
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
    desc.Inputs = inputs;
    desc.ScratchAccelerationStructureData =
        _scratch->GetGPUVirtualAddress();
    desc.DestAccelerationStructureData =
        _tlas->GetGPUVirtualAddress();

    cmdList->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);

    // 7. UAV barrier
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = _tlas.Get();
    cmdList->ResourceBarrier(1, &barrier);
}

ID3D12Resource* RayTracingManager::Tlas() const
{
    return _tlas.Get();
}

void RayTracingManager::AddRtObject(EditableRenderItem* object)
{
    _rtObjects.push_back(object);
}

void RayTracingManager::DeleteRtObject(const EditableRenderItem* object)
{
    size_t index = -1;
    for (int i = 0; i < _rtObjects.size(); i++)
    {
        if (_rtObjects[i] == object)
        {
            index = i;
            break;
        }
    }
    
    if (index == -1)
        return;

    _rtObjects.erase(_rtObjects.begin() + index);
}

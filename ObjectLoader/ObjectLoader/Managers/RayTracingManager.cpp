#include "RayTracingManager.h"

#include "GeometryManager.h"
#include "UploadManager.h"

RayTracingManager::RayTracingManager(ID3D12Device5* device)
{
    _device = device;
}

void RayTracingManager::Init()
{
    BuildRootSignature();
    BuildPso();
}

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

void RayTracingManager::BuildPso()
{
    std::vector<D3D12_STATE_SUBOBJECT> subobjects;
    subobjects.reserve(6);
    
    //1st subobject: shaders
    const auto dxilLib = d3dUtil::CompileDxilLibrary(    L"Shaders\\RayTracing.hlsl");

    D3D12_EXPORT_DESC exports[] =
    {
        { L"RayGenShadows", nullptr, D3D12_EXPORT_FLAG_NONE },
        { L"ShadowMiss", nullptr, D3D12_EXPORT_FLAG_NONE },
        { L"ShadowClosestHit", nullptr, D3D12_EXPORT_FLAG_NONE }
    };

    D3D12_DXIL_LIBRARY_DESC dxilDesc;
    dxilDesc.DXILLibrary.pShaderBytecode = dxilLib->GetBufferPointer();
    dxilDesc.DXILLibrary.BytecodeLength = dxilLib->GetBufferSize();
    dxilDesc.NumExports = _countof(exports);
    dxilDesc.pExports = exports;

    D3D12_STATE_SUBOBJECT dxilSubobject;
    dxilSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    dxilSubobject.pDesc = &dxilDesc;
    
    subobjects.push_back(dxilSubobject);

    //2nd subobject: hit group
    D3D12_HIT_GROUP_DESC hitGroup = {};
    hitGroup.HitGroupExport = L"ShadowHitGroup";
    hitGroup.ClosestHitShaderImport = L"ShadowClosestHit";
    hitGroup.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;

    D3D12_STATE_SUBOBJECT hitGroupSubobject;
    hitGroupSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    hitGroupSubobject.pDesc = &hitGroup;
    
    subobjects.push_back(hitGroupSubobject);

    //3rd subobject: shader config    
    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig;
    shaderConfig.MaxPayloadSizeInBytes = 4;
    shaderConfig.MaxAttributeSizeInBytes =
        D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;

    D3D12_STATE_SUBOBJECT shaderConfigSubobject;
    shaderConfigSubobject.Type =
        D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    shaderConfigSubobject.pDesc = &shaderConfig;

    subobjects.push_back(shaderConfigSubobject);

    //4th subobject: shader config association
    const WCHAR* shaderExports[] =
    {
        L"RayGenShadows",
        L"ShadowHitGroup",
        L"ShadowMiss"
    };

    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shaderConfigAssoc;
    shaderConfigAssoc.NumExports = _countof(shaderExports);
    shaderConfigAssoc.pExports = shaderExports;
    shaderConfigAssoc.pSubobjectToAssociate = &subobjects[2];

    D3D12_STATE_SUBOBJECT shaderConfigAssocSubobject;
    shaderConfigAssocSubobject.Type =
        D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
    shaderConfigAssocSubobject.pDesc = &shaderConfigAssoc;

    subobjects.push_back(shaderConfigAssocSubobject);

    //5th subobject: global root signature    
    D3D12_STATE_SUBOBJECT globalRs;
    globalRs.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    globalRs.pDesc = _rootSignature.GetAddressOf();

    subobjects.push_back(globalRs);

    //6th subobject: pipeline config
    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig;
    pipelineConfig.MaxTraceRecursionDepth = 1;

    D3D12_STATE_SUBOBJECT pipelineConfigSubobject;
    pipelineConfigSubobject.Type =
        D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    pipelineConfigSubobject.pDesc = &pipelineConfig;

    subobjects.push_back(pipelineConfigSubobject);

    D3D12_STATE_OBJECT_DESC desc;
    desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    desc.NumSubobjects = static_cast<UINT>(subobjects.size());
    desc.pSubobjects = subobjects.data();

    ThrowIfFailed(_device->CreateStateObject(
        &desc,
        IID_PPV_ARGS(&_rtPso)));

    ThrowIfFailed(_rtPso.As(&_rtPsoInfo));
}

void RayTracingManager::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE ranges[3];

    // SRVs: t0-t2
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);

    // UAVs: u0
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    // CBVs: b0
    ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

    CD3DX12_ROOT_PARAMETER params[3];
    params[0].InitAsDescriptorTable(1, &ranges[0]);
    params[1].InitAsDescriptorTable(1, &ranges[1]);
    params[2].InitAsDescriptorTable(1, &ranges[2]);

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(
        _countof(params),
        params,
        0,
        nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_NONE
    );

    Microsoft::WRL::ComPtr<ID3DBlob> blob, error;
    ThrowIfFailed(D3D12SerializeRootSignature(
        &desc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &blob,
        &error));

    ThrowIfFailed(UploadManager::Device->CreateRootSignature(
        0,
        blob->GetBufferPointer(),
        blob->GetBufferSize(),
        IID_PPV_ARGS(&_rootSignature)));
}
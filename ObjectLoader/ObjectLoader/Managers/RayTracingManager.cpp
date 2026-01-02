#include "RayTracingManager.h"

#include "GeometryManager.h"
#include "LightingManager.h"
#include "UploadManager.h"

RayTracingManager::RayTracingManager(ID3D12Device5* device, const int width, const int height) : _gbuffer(nullptr)
{
    _device = device;
    _width = width;
    _height = height;

    const auto& allocator = TextureManager::SrvHeapAllocator;
    _shadowMaskTexture.SrvIndex = allocator->Allocate();
    _shadowMaskTexture.OtherIndex = allocator->Allocate();
    _tlasSrvIndex = allocator->Allocate();
}

void RayTracingManager::Init()
{
    BuildRootSignature();
    BuildPso();
    BuildTextures();
    BuildShaderTables();
}

void RayTracingManager::BindToOtherData(GBuffer* buffer)
{
    _gbuffer = buffer;
}

void RayTracingManager::OnResize(const int newWidth, const int newHeight)
{
    _width = newWidth;
    _height = newHeight;
    BuildTextures();
}

void RayTracingManager::UpdateData()
{
    for (const auto& obj : _rtObjects)
    {
        if (obj->RayTracingDirty)
        {
            BuildTlas();
            return;
        }
    }
}

void RayTracingManager::BuildTlas()
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
    desc.SourceAccelerationStructureData = 0;

    const auto& cmdList = UploadManager::UploadCmdList;
    cmdList->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);

    // 7. UAV barrier
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = _tlas.Get();
    cmdList->ResourceBarrier(1, &barrier);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.RaytracingAccelerationStructure.Location =
        _tlas->GetGPUVirtualAddress();
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;


    _device->CreateShaderResourceView(
        nullptr,
        &srvDesc,
        TextureManager::SrvHeapAllocator->GetCpuHandle(_tlasSrvIndex)
    );
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

void RayTracingManager::DispatchRays(ID3D12GraphicsCommandList4* cmdList, const FrameResource* currFrameResource)
{
    cmdList->SetPipelineState1(_rtPso.Get());
    cmdList->SetComputeRootSignature(_rootSignature.Get());

    const auto& allocator = TextureManager::SrvHeapAllocator;
    
    cmdList->SetComputeRootDescriptorTable(0, allocator->GetGpuHandle(_tlasSrvIndex));

    cmdList->SetComputeRootDescriptorTable(1, _gbuffer->GetGBufferTextureSrv(GBufferInfo::Normals));
    cmdList->SetComputeRootDescriptorTable(2, _gbuffer->GetGBufferDepthSrv(true));

    cmdList->SetComputeRootDescriptorTable(3, allocator->GetGpuHandle(_shadowMaskTexture.OtherIndex));
    
    const auto lightingPassCb = currFrameResource->LightingPassCb->Resource();
    cmdList->SetComputeRootConstantBufferView(4, lightingPassCb->GetGPUVirtualAddress());
    const auto rayTracingCb = currFrameResource->RayTracingCb->Resource();
    cmdList->SetComputeRootConstantBufferView(4, rayTracingCb->GetGPUVirtualAddress());

    D3D12_DISPATCH_RAYS_DESC dispatch = {};
    dispatch.RayGenerationShaderRecord = {
        _rayGenTable->GetGPUVirtualAddress(),
        _shaderRecord
    };

    dispatch.MissShaderTable = {
        _missTable->GetGPUVirtualAddress(),
        _shaderRecord,
        _shaderRecord
    };

    dispatch.HitGroupTable = {
        _hitGroupTable->GetGPUVirtualAddress(),
        _shaderRecord,
        _shaderRecord
    };

    dispatch.Width  = _width;
    dispatch.Height = _height;
    dispatch.Depth  = 1;

    BasicUtil::ChangeTextureState(cmdList, _shadowMaskTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    cmdList->DispatchRays(&dispatch);

    BasicUtil::ChangeTextureState(cmdList, _shadowMaskTexture, D3D12_RESOURCE_STATE_GENERIC_READ);
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
    CD3DX12_DESCRIPTOR_RANGE bvhTexTable;
    bvhTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_DESCRIPTOR_RANGE normalTexTable;
    normalTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    
    CD3DX12_DESCRIPTOR_RANGE depthTexTable;
    depthTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);

    CD3DX12_DESCRIPTOR_RANGE shadowMaskTexTable;
    shadowMaskTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    constexpr int rootParameterCount = 6;

    CD3DX12_ROOT_PARAMETER params[rootParameterCount];
    params[0].InitAsDescriptorTable(1, &bvhTexTable);
    params[1].InitAsDescriptorTable(1, &normalTexTable);
    params[2].InitAsDescriptorTable(1, &depthTexTable);
    params[3].InitAsDescriptorTable(1, &shadowMaskTexTable);
    params[4].InitAsConstantBufferView(0);
    params[5].InitAsConstantBufferView(1);

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(
        _countof(params),
        params,
        static_cast<UINT>(TextureManager::GetLinearSamplers().size()),
        TextureManager::GetLinearSamplers().data(),
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

void RayTracingManager::BuildTextures()
{
    DXGI_FORMAT format = DXGI_FORMAT_R8_UNORM;
    
    D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Alignment = 0;
	resourceDesc.Width = _width;
	resourceDesc.Height = _height;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);

	ThrowIfFailed(_device->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr, IID_PPV_ARGS(&_shadowMaskTexture.Resource)));

	// Create UAV
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    _device->CreateUnorderedAccessView(
        _shadowMaskTexture.Resource.Get(),
        nullptr,
        &uavDesc,
        TextureManager::SrvHeapAllocator->GetCpuHandle(_shadowMaskTexture.OtherIndex)
    );
    
	//create SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc = {};
	shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	shaderResourceViewDesc.Format = format;
	shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	shaderResourceViewDesc.Texture2D.MipLevels = 1;

	_device->CreateShaderResourceView(_shadowMaskTexture.Resource.Get(), &shaderResourceViewDesc,
		TextureManager::SrvHeapAllocator->GetCpuHandle(_shadowMaskTexture.SrvIndex));
}

void RayTracingManager::BuildShaderTables()
{
    Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> props;
    ThrowIfFailed(_rtPso.As(&props));

    //ray gen
    const void* rayGenId = props->GetShaderIdentifier(L"RayGenShadows");

    const UINT recordSize = _shaderRecord;

    _rayGenTable = UploadManager::CreateShaderTable(recordSize);

    void* mapped;
    ThrowIfFailed(_rayGenTable->Map(0, nullptr, &mapped));
    memcpy(mapped, rayGenId, _shaderRecord);
    _rayGenTable->Unmap(0, nullptr);
    
    //miss
    const void* missId   = props->GetShaderIdentifier(L"ShadowMiss");

    _missTable = UploadManager::CreateShaderTable(recordSize);

    ThrowIfFailed(_missTable->Map(0, nullptr, &mapped));
    memcpy(mapped, missId, _shaderRecord);
    _missTable->Unmap(0, nullptr);
    
    //hit group
    const void* hitId    = props->GetShaderIdentifier(L"ShadowHitGroup");

    _hitGroupTable = UploadManager::CreateShaderTable(recordSize);

    ThrowIfFailed(_hitGroupTable->Map(0, nullptr, &mapped));
    memcpy(mapped, hitId, _shaderRecord);
    _hitGroupTable->Unmap(0, nullptr);
}

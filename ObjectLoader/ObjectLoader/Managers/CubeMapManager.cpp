#include "CubeMapManager.h"

#include "UploadManager.h"

CubeMapManager::CubeMapManager(ID3D12Device* device)
{
	_device = device;
}

void CubeMapManager::Init()
{
	BuildInputLayout();
	BuildRootSignature();
	BuildShaders();
	BuildPso();
	BuildRenderItem();

	const std::vector<std::wstring> filenames = {
		L"Sky/skybox.dds",
		L"Sky/irradiance.dds",
		L"Sky/prefiltered.dds",
		L"Sky/brdfLUT.dds"
	};
	//preallocate indices for maps
	for (int i = 0; i < static_cast<int>(CubeMap::Count); i++)
	{
		TextureHandle texHandle;
		if (i == static_cast<int>(CubeMap::Brdf))
		{
			texHandle = TextureManager::LoadTexture(filenames[i].c_str(), 0, 1);
		}
		else
		{
			TextureManager::LoadCubeTexture(filenames[i].c_str(), texHandle);
		}
		_maps.push_back(texHandle);
	}
}

void CubeMapManager::AddObjectToResource(const FrameResource* currFrameResource) const
{
	StaticObjectConstants constants;
	//it is just scaling matrix so no need to transpose
	XMStoreFloat4x4(&constants.World, _skyRItem->World);
	currFrameResource->StaticObjCb->CopyData(_skyRItem->Uid, constants);
}

void CubeMapManager::Draw(ID3D12GraphicsCommandList* cmdList, const FrameResource* currFrameResource) const
{
	if (_maps[static_cast<int>(CubeMap::Skybox)].UseTexture == false)
		return;

	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	const auto passCb = currFrameResource->GBufferPassCb->Resource();
	cmdList->SetGraphicsRootConstantBufferView(1, passCb->GetGPUVirtualAddress());

	cmdList->SetGraphicsRootDescriptorTable(0, GetCubeMapGpuHandle());

	cmdList->SetPipelineState(_pso.Get());

	const auto staticObjectCb = currFrameResource->StaticObjCb->Resource();

	const auto geo = GeometryManager::Geometries()["shapeGeo"].begin()->get();

	const auto vertexBuffer = geo->VertexBufferView();
	const auto indexBuffer = geo->IndexBufferView();
	
	cmdList->IASetVertexBuffers(0, 1, &vertexBuffer);
	cmdList->IASetIndexBuffer(&indexBuffer);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	const D3D12_GPU_VIRTUAL_ADDRESS objCbAddress = staticObjectCb->GetGPUVirtualAddress() + _skyRItem->Uid * _cbSize;

	cmdList->SetGraphicsRootConstantBufferView(2, objCbAddress);

	const SubmeshGeometry mesh = geo->DrawArgs["sphere"];

	cmdList->DrawIndexedInstanced(mesh.IndexCount, 1, mesh.StartIndexLocation, mesh.BaseVertexLocation, 0);
}

void CubeMapManager::AddMap(CubeMap type, const TextureHandle& handle)
{
	_maps[static_cast<int>(type)] = handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE CubeMapManager::GetCubeMapGpuHandle() const
{
	return TextureManager::SrvHeapAllocator->GetGpuHandle(_maps[static_cast<int>(CubeMap::Skybox)].Index);
}

D3D12_GPU_DESCRIPTOR_HANDLE CubeMapManager::GetIblMapsGpuHandle() const
{
	return TextureManager::SrvHeapAllocator->GetGpuHandle(_maps[static_cast<int>(CubeMap::Irradiance)].Index);
}

void CubeMapManager::BuildInputLayout()
{
	_inputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
}

void CubeMapManager::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE skyTexTable;
	skyTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	constexpr int rootParameterCount = 3;

	CD3DX12_ROOT_PARAMETER skySlotRootParameter[rootParameterCount];

	skySlotRootParameter[0].InitAsDescriptorTable(1, &skyTexTable);
	skySlotRootParameter[1].InitAsConstantBufferView(0);
	skySlotRootParameter[2].InitAsConstantBufferView(1);

	const CD3DX12_ROOT_SIGNATURE_DESC lightingRootSigDesc(rootParameterCount, skySlotRootParameter, static_cast<UINT>(TextureManager::GetLinearWrapSampler().size()), TextureManager::GetLinearWrapSampler().data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	const HRESULT hr = D3D12SerializeRootSignature(&lightingRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(UploadManager::Device->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(_rootSignature.GetAddressOf())));
}

void CubeMapManager::BuildShaders()
{
	_vsShader = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "VS", "vs_5_1");
	_psShader = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "PS", "ps_5_1");

}

void CubeMapManager::BuildPso()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc;

	ZeroMemory(&skyPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	skyPsoDesc.InputLayout = { _inputLayout.data(), static_cast<UINT>(_inputLayout.size()) };
	skyPsoDesc.pRootSignature = _rootSignature.Get();
	skyPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	skyPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	skyPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	skyPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	skyPsoDesc.SampleMask = UINT_MAX;
	skyPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	skyPsoDesc.NumRenderTargets = 1;
	skyPsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	skyPsoDesc.SampleDesc.Count = 1;
	skyPsoDesc.SampleDesc.Quality = 0;
	skyPsoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	skyPsoDesc.VS =
	{
		static_cast<BYTE*>(_vsShader->GetBufferPointer()),
		_vsShader->GetBufferSize()
	};
	skyPsoDesc.PS =
	{
		static_cast<BYTE*>(_psShader->GetBufferPointer()),
		_psShader->GetBufferSize()
	};
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&_pso)));
}

void CubeMapManager::BuildRenderItem()
{
	_skyRItem = std::make_unique<EditableRenderItem>();
	_skyRItem->World = XMMatrixScaling(5000.0f, 5000.0f, 5000.0f);
	_skyRItem->Uid = FrameResource::StaticObjectCount++;
	_skyRItem->Geo = &GeometryManager::Geometries()["shapeGeo"];
	_skyRItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

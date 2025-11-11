#include "CubeMapManager.h"

CubeMapManager::CubeMapManager(ID3D12Device* device)
{
	_device = device;
}

CubeMapManager::~CubeMapManager()
{
}

void CubeMapManager::Init()
{
	BuildInputLayout();
	BuildRootSignature();
	BuildShaders();
	BuildPSO();
	BuildRenderItem();

	auto& allocator = TextureManager::srvHeapAllocator;

	std::vector<std::wstring> filenames = {
		L"Sky/skybox.dds",
		L"Sky/irradiance.dds",
		L"Sky/prefiltered.dds",
		L"Sky/brdfLUT.dds"
	};
	//preallocate indices for maps
	for (int i = 0; i < (int)CubeMap::Count; i++)
	{
		TextureHandle texHandle;
		if (i == (int)CubeMap::BRDF)
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

void CubeMapManager::AddObjectToResource(FrameResource* currFrameResource)
{
	StaticObjectConstants constants;
	//it is just scaling matrix so no need to transpose
	XMStoreFloat4x4(&constants.World, _skyRItem->world);
	currFrameResource->StaticObjCB->CopyData(_skyRItem->uid, constants);
}

void CubeMapManager::Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource)
{
	if (_maps[(int)CubeMap::Skybox].useTexture == false)
		return;

	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	auto passCB = currFrameResource->GBufferPassCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	cmdList->SetGraphicsRootDescriptorTable(0, GetCubeMapGPUHandle());

	cmdList->SetPipelineState(_pso.Get());

	auto staticObjectCB = currFrameResource->StaticObjCB->Resource();

	auto geo = GeometryManager::geometries()["shapeGeo"].begin()->get();

	cmdList->IASetVertexBuffers(0, 1, &geo->VertexBufferView());
	cmdList->IASetIndexBuffer(&geo->IndexBufferView());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = staticObjectCB->GetGPUVirtualAddress() + _skyRItem->uid * _cbSize;

	cmdList->SetGraphicsRootConstantBufferView(2, objCBAddress);

	SubmeshGeometry mesh = geo->DrawArgs["sphere"];

	cmdList->DrawIndexedInstanced(mesh.IndexCount, 1, mesh.StartIndexLocation, mesh.BaseVertexLocation, 0);
}

void CubeMapManager::AddMap(CubeMap type, TextureHandle handle)
{
	_maps[(int)type] = handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE CubeMapManager::GetCubeMapGPUHandle()
{
	return TextureManager::srvHeapAllocator->GetGpuHandle(_maps[(int)CubeMap::Skybox].index);
}

D3D12_GPU_DESCRIPTOR_HANDLE CubeMapManager::GetIBLMapsGPUHandle()
{
	return TextureManager::srvHeapAllocator->GetGpuHandle(_maps[(int)CubeMap::Irradiance].index);
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

	const int rootParameterCount = 3;

	CD3DX12_ROOT_PARAMETER skySlotRootParameter[rootParameterCount];

	skySlotRootParameter[0].InitAsDescriptorTable(1, &skyTexTable);
	skySlotRootParameter[1].InitAsConstantBufferView(0);
	skySlotRootParameter[2].InitAsConstantBufferView(1);

	CD3DX12_ROOT_SIGNATURE_DESC lightingRootSigDesc(rootParameterCount, skySlotRootParameter, (UINT)TextureManager::GetLinearWrapSampler().size(), TextureManager::GetLinearWrapSampler().data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&lightingRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(UploadManager::device->CreateRootSignature(
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

void CubeMapManager::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc;

	ZeroMemory(&skyPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	skyPsoDesc.InputLayout = { _inputLayout.data(), (UINT)_inputLayout.size() };
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
		reinterpret_cast<BYTE*>(_vsShader->GetBufferPointer()),
		_vsShader->GetBufferSize()
	};
	skyPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(_psShader->GetBufferPointer()),
		_psShader->GetBufferSize()
	};
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&_pso)));
}

void CubeMapManager::BuildRenderItem()
{
	_skyRItem = std::make_unique<EditableRenderItem>();
	_skyRItem->world = XMMatrixScaling(5000.0f, 5000.0f, 5000.0f);
	_skyRItem->uid = FrameResource::staticObjectCount++;
	_skyRItem->Geo = &GeometryManager::geometries()["shapeGeo"];
	_skyRItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

#include "LightingManager.h"

using namespace Microsoft::WRL;
using namespace DirectX;

LightingManager::LightingManager()
{
	
}

LightingManager::~LightingManager()
{
}

void LightingManager::addLight(ID3D12Device* device)
{
	int lightIndex;
	if (!FreeLightIndices.empty()) {
		lightIndex = FreeLightIndices.back();
		FreeLightIndices.pop_back();
	}
	else {
		if (NextAvailableIndex >= MaxLights) {
			throw std::runtime_error("Maximum number of lights exceeded!");
		}
		lightIndex = NextAvailableIndex++;
	}

	auto lightItem = std::make_unique<LightRenderItem>();
	lightItem->LightIndex = lightIndex;
	lightItem->NumFramesDirty = gNumFrameResources;
	lightItem->LightData = Light();

	_localLights.push_back(std::move(lightItem));
}

void LightingManager::deleteLight(int deletedLight)
{
	FreeLightIndices.push_back(_localLights[deletedLight]->LightIndex);
	_localLights.erase(_localLights.begin() + deletedLight);
}

void LightingManager::UpdateDirectionalLightCB(FrameResource* currFrameResource)
{
	_dirLightCB.mainLightIsOn = _isMainLightOn;
	_dirLightCB.gLightColor = _dirLightColor;

	DirectX::XMVECTOR direction = DirectX::XMLoadFloat3(&_mainLightDirection);
	direction = DirectX::XMVector3Normalize(direction);
	DirectX::XMStoreFloat3(&_dirLightCB.mainLightDirection, direction);

	auto currDirLightCB = currFrameResource->DirLightCB.get();
	currDirLightCB->CopyData(0, _dirLightCB);
}

void LightingManager::UpdateLightCBs(FrameResource* currFrameResource)
{
	for (auto& light : _localLights)
	{
		if (light->NumFramesDirty == 0)
		{
			continue;
		}
		auto currLightCB = currFrameResource->LocalLightCB.get();
		currLightCB->CopyData(light->LightIndex, light->LightData);
		light->NumFramesDirty--;
	}
}

void LightingManager::UpdateWorld(int lightIndex)
{
	Light data = _localLights[lightIndex]->LightData;
	XMMATRIX world;
	//if it is a sphere
	if (data.type == 0)
	{
		XMMATRIX scale = XMMatrixScaling(data.radius, data.radius, data.radius);
		XMMATRIX translation = XMMatrixTranslationFromVector(XMLoadFloat3(&data.position));

		world = scale * translation;
	}
	//or it is a cone
	else
	{
		float diameter = 2.f * data.radius * tanf(data.angle * XM_PI / 180.f);
		XMMATRIX scale = XMMatrixScaling(diameter, data.radius, diameter);
		XMVECTOR direction = XMVector3Normalize(XMLoadFloat3(&data.direction));
		XMMATRIX rotation = XMMatrixRotationRollPitchYawFromVector(direction);
		XMMATRIX translation = XMMatrixTranslationFromVector(direction * data.radius * .5f);
		world = scale * rotation * translation;
	}
	_localLights[lightIndex]->LightData.world = XMMatrixTranspose(world);
	_localLights[lightIndex]->NumFramesDirty = gNumFrameResources;
}

int LightingManager::lightsCount()
{
	return _localLights.size();
}

LightRenderItem* LightingManager::light(int i)
{
	return _localLights[i].get();
}

void LightingManager::Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, D3D12_GPU_DESCRIPTOR_HANDLE descTable)
{
	// Indicate a state transition on the resource usage.
	
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	//update buffer with light data
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(_lightBufferGPU.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST));
	cmdList->CopyBufferRegion(_lightBufferGPU.Get(), 0, currFrameResource->LocalLightCB.get()->Resource(), 0, sizeof(Light) * _localLights.size());
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(_lightBufferGPU.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

	cmdList->SetGraphicsRootDescriptorTable(0, descTable);
	cmdList->SetGraphicsRootShaderResourceView(1, _lightBufferGPU->GetGPUVirtualAddress());

	cmdList->SetPipelineState(_dirLightPSO.Get());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	auto lightingPassCB = currFrameResource->LightingPassCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(2, lightingPassCB->GetGPUVirtualAddress());

	auto dirLightCB = currFrameResource->DirLightCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(3, dirLightCB->GetGPUVirtualAddress());

	//draw directional light
	cmdList->DrawInstanced( 3, 1, 0, 0);

	//draw local lights
	auto geo = GeometryManager::geometries()[L"shapeGeo"].get();

	cmdList->IASetVertexBuffers(0, 1, &geo->VertexBufferView());
	cmdList->IASetIndexBuffer(&geo->IndexBufferView());

	cmdList->SetPipelineState(_localLightsPSO.Get());

	if (_localLights.size() > 0)
	{
		geo = geo;
	}

	SubmeshGeometry mesh = geo->DrawArgs[L"box"];
	cmdList->DrawIndexedInstanced(mesh.IndexCount, _localLights.size(), mesh.StartIndexLocation,
		mesh.BaseVertexLocation, 0);
}

void LightingManager::Init(int srvAmount, ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle)
{
	// Create buffer on GPU
	CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(Light) * MaxLights);

	device->CreateCommittedResource(
		&defaultHeap,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&_lightBufferGPU));

	BuildRootSignature(srvAmount);
	BuildShaders();
	BuildInputLayout();
	BuildPSO();


	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = MaxLights;
	srvDesc.Buffer.StructureByteStride = sizeof(Light);
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	device->CreateShaderResourceView(_lightBufferGPU.Get(), &srvDesc, srvHandle);
}

void LightingManager::BuildInputLayout()
{
	_localLightsInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
}

void LightingManager::BuildRootSignature(int srvAmount)
{
	//lighting root signature
	CD3DX12_DESCRIPTOR_RANGE lightingRange;
	lightingRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvAmount, 0);

	CD3DX12_ROOT_PARAMETER lightingSlotRootParameter[4];

	lightingSlotRootParameter[0].InitAsDescriptorTable(1, &lightingRange);
	lightingSlotRootParameter[1].InitAsShaderResourceView(0, 1);
	lightingSlotRootParameter[2].InitAsConstantBufferView(0);
	lightingSlotRootParameter[3].InitAsConstantBufferView(1);

	CD3DX12_ROOT_SIGNATURE_DESC lightingRootSigDesc(4, lightingSlotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
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

void LightingManager::BuildShaders()
{
	_dirLightVSShader = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "DirLightingVS", "vs_5_1");
	_dirLightPSShader = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "DirLightingPS", "ps_5_1");
	_localLightsVSShader = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "LocalLightingVS", "vs_5_1");
	_localLightsPSShader = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "LocalLightingPS", "ps_5_1");
}

void LightingManager::BuildPSO()
{
	//directional light pso
	D3D12_GRAPHICS_PIPELINE_STATE_DESC lightingPSODesc;

	ZeroMemory(&lightingPSODesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	lightingPSODesc.pRootSignature = _rootSignature.Get();
	lightingPSODesc.VS =
	{
		reinterpret_cast<BYTE*>(_dirLightVSShader->GetBufferPointer()),
		_dirLightVSShader->GetBufferSize()
	};
	lightingPSODesc.PS =
	{
		reinterpret_cast<BYTE*>(_dirLightPSShader->GetBufferPointer()),
		_dirLightPSShader->GetBufferSize()
	};
	lightingPSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	lightingPSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	lightingPSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	lightingPSODesc.SampleMask = UINT_MAX;
	lightingPSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	lightingPSODesc.NumRenderTargets = 1;
	lightingPSODesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	lightingPSODesc.DepthStencilState.DepthEnable = false;
	lightingPSODesc.DepthStencilState.StencilEnable = false;
	lightingPSODesc.SampleDesc.Count = 1;
	lightingPSODesc.SampleDesc.Quality = 0;
	lightingPSODesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	ThrowIfFailed(UploadManager::device->CreateGraphicsPipelineState(&lightingPSODesc, IID_PPV_ARGS(&_dirLightPSO)));

	//local lights pso
	D3D12_GRAPHICS_PIPELINE_STATE_DESC localLightsPSODesc = lightingPSODesc;
	localLightsPSODesc.InputLayout = { _localLightsInputLayout.data(), (UINT)_localLightsInputLayout.size() };

	localLightsPSODesc.VS =
	{
		reinterpret_cast<BYTE*>(_localLightsVSShader->GetBufferPointer()),
		_localLightsVSShader->GetBufferSize()
	};
	localLightsPSODesc.PS =
	{
		reinterpret_cast<BYTE*>(_localLightsPSShader->GetBufferPointer()),
		_localLightsPSShader->GetBufferSize()
	};

	localLightsPSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	localLightsPSODesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	localLightsPSODesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	localLightsPSODesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	localLightsPSODesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	localLightsPSODesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	localLightsPSODesc.DepthStencilState.DepthEnable = TRUE;
	localLightsPSODesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	localLightsPSODesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	ThrowIfFailed(UploadManager::device->CreateGraphicsPipelineState(&localLightsPSODesc, IID_PPV_ARGS(&_localLightsPSO)));

}
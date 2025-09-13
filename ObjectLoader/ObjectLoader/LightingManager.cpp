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
	float radius = lightItem->LightData.radius;
	float diameter = radius * 2.f;
	lightItem->LightData.world = XMMatrixScalingFromVector(XMVectorSet(diameter, diameter, diameter, 1));
	lightItem->Bounds = BoundingSphere(lightItem->LightData.position, lightItem->LightData.radius);

	_localLights.push_back(std::move(lightItem));
}

void LightingManager::deleteLight(int deletedLight)
{
	FreeLightIndices.push_back(_localLights[deletedLight]->LightIndex);
	_localLights.erase(_localLights.begin() + deletedLight);
}

void LightingManager::UpdateDirectionalLightCB(FrameResource* currFrameResource)
{
	DirectionalLightConstants dirLightCB;
	dirLightCB.mainLightIsOn = _isMainLightOn;
	dirLightCB.gLightColor = _dirLightColor;

	DirectX::XMVECTOR direction = DirectX::XMLoadFloat3(&_mainLightDirection);
	direction = DirectX::XMVector3Normalize(direction);
	DirectX::XMStoreFloat3(&dirLightCB.mainLightDirection, direction);
	dirLightCB.mainSpotlight = _handSpotlight;

	dirLightCB.lightsContainingFrustum = _lightsContainingFrustum;

	auto currDirLightCB = currFrameResource->DirLightCB.get();
	currDirLightCB->CopyData(0, dirLightCB);
}

void LightingManager::UpdateLightCBs(FrameResource* currFrameResource, Camera* camera)
{
	//for update
	auto currLightsCB = currFrameResource->LocalLightCB.get();

	//for frustum culling
	XMMATRIX view = camera->GetView();
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);

	int lightsInsideFrustum = 0;
	int lightsContainingFrustum = 0;

	auto currLightsInsideFrustumCB = currFrameResource->LightsInsideFrustum.get();
	auto currLightsContainingFrustumCB = currFrameResource->LightsContainingFrustum.get();

	for (auto& light : _localLights)
	{
		//update data if needed
		if (light->NumFramesDirty != 0)
		{
			currLightsCB->CopyData(light->LightIndex, light->LightData);
			light->NumFramesDirty--;
		}

		// Transform the camera frustum from view space to the object's local space.
		BoundingFrustum localSpaceFrustum;
		camera->CameraFrustum().Transform(localSpaceFrustum, invView);

		// Perform the box/frustum intersection test in local space.
		//todo: if camera in local light it's one thing but if local light in camera it is another
		if (localSpaceFrustum.Contains(light->Bounds) == DirectX::DISJOINT)
		{
			continue;
		}
		else if (light->Bounds.Contains(camera->GetPosition()))
		{
			//we are inside of the light
			currLightsContainingFrustumCB->CopyData(lightsContainingFrustum++, LightIndex(light->LightIndex));
		}
		else
		{
			//the light is inside of the frustum
			currLightsInsideFrustumCB->CopyData(lightsInsideFrustum++, LightIndex(light->LightIndex));
		}
	}

	_lightisInsideFrustum = lightsInsideFrustum;
	_lightsContainingFrustum = lightsContainingFrustum;
}

void LightingManager::UpdateWorld(int lightIndex)
{
	Light data = _localLights[lightIndex]->LightData;
	XMMATRIX world;
	//if it is a sphere
	if (data.type == 0)
	{
		float diameter = data.radius * 2.f;
		XMMATRIX scale = XMMatrixScaling(diameter, diameter, diameter);
		XMMATRIX translation = XMMatrixTranslationFromVector(XMLoadFloat3(&data.position));

		world = scale * translation;
		_localLights[lightIndex]->Bounds = BoundingSphere(data.position, data.radius);
	}
	//or it is a cone
	else
	{
		float diameter = 2.f * data.radius * tanf(data.angle);
		XMMATRIX scale = XMMatrixScaling(diameter, diameter, data.radius);
		XMVECTOR dir = XMLoadFloat3(&data.direction);
		if (XMVector3Less(XMVector3LengthSq(dir), XMVectorReplicate(1e-6f)))
		{
			return;
		}
		XMVECTOR direction = XMVector3Normalize(dir);

		XMVECTOR position = XMLoadFloat3(&data.position);
		position += direction * data.radius * .5f;
		XMMATRIX translation = XMMatrixTranslationFromVector(position);
		
		XMVECTOR up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
		if (fabs(XMVectorGetX(XMVector3Dot(direction, up))) > 0.999f)
		{
			up = XMVectorSet(0.f, 0.f, 1.f, 0.f);
		}

		XMMATRIX rotation = XMMatrixTranspose(XMMatrixLookToLH(XMVectorZero(), direction, up));
		world = scale * rotation * translation;

		XMStoreFloat3(&_localLights[lightIndex]->Bounds.Center, position);
		_localLights[lightIndex]->Bounds.Radius = data.radius * .5f;
	}
	_localLights[lightIndex]->LightData.world = XMMatrixTranspose(world);
	_localLights[lightIndex]->NumFramesDirty = gNumFrameResources;
}

int LightingManager::lightsCount()
{
	return (int)_localLights.size();
}

LightRenderItem* LightingManager::light(int i)
{
	return _localLights[i].get();
}

void LightingManager::DrawDirLight(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, D3D12_GPU_DESCRIPTOR_HANDLE descTable)
{
	// Indicate a state transition on the resource usage.

	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	cmdList->SetGraphicsRootDescriptorTable(0, descTable);
	cmdList->SetGraphicsRootShaderResourceView(1, currFrameResource->LocalLightCB.get()->Resource()->GetGPUVirtualAddress());
	cmdList->SetGraphicsRootShaderResourceView(4, currFrameResource->LightsContainingFrustum.get()->Resource()->GetGPUVirtualAddress());

	cmdList->SetPipelineState(_dirLightPSO.Get());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	auto lightingPassCB = currFrameResource->LightingPassCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(2, lightingPassCB->GetGPUVirtualAddress());

	auto dirLightCB = currFrameResource->DirLightCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(3, dirLightCB->GetGPUVirtualAddress());

	//draw directional light
	cmdList->DrawInstanced(3, 1, 0, 0);
}

void LightingManager::DrawLocalLights(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource)
{
	//draw local lights
	MeshGeometry* geo = GeometryManager::geometries()["shapeGeo"].begin()->get();

	cmdList->IASetVertexBuffers(0, 1, &geo->VertexBufferView());
	cmdList->IASetIndexBuffer(&geo->IndexBufferView());

	cmdList->SetGraphicsRootShaderResourceView(4, currFrameResource->LightsInsideFrustum.get()->Resource()->GetGPUVirtualAddress());

	cmdList->SetPipelineState(_localLightsPSO.Get());

	SubmeshGeometry mesh = geo->DrawArgs["box"];
	cmdList->DrawIndexedInstanced(mesh.IndexCount, (UINT)_lightisInsideFrustum, mesh.StartIndexLocation,
		mesh.BaseVertexLocation, 0);
}

void LightingManager::DrawDebug(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource)
{
	auto geo = GeometryManager::geometries()["shapeGeo"].begin()->get();
	SubmeshGeometry mesh = geo->DrawArgs["box"];
	cmdList->SetPipelineState(_localLightsWireframePSO.Get());
	cmdList->DrawIndexedInstanced(mesh.IndexCount, (UINT)_lightisInsideFrustum, mesh.StartIndexLocation, mesh.BaseVertexLocation, 0);
}

void LightingManager::DrawEmissive(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource)
{
	cmdList->SetPipelineState(_emissivePSO.Get());

	cmdList->DrawInstanced(3, 1, 0, 0);
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

	const int rootParameterCount = 5;

	CD3DX12_ROOT_PARAMETER lightingSlotRootParameter[rootParameterCount];

	lightingSlotRootParameter[0].InitAsDescriptorTable(1, &lightingRange);
	lightingSlotRootParameter[1].InitAsShaderResourceView(0, 1);
	lightingSlotRootParameter[2].InitAsConstantBufferView(0);
	lightingSlotRootParameter[3].InitAsConstantBufferView(1);
	lightingSlotRootParameter[4].InitAsShaderResourceView(1, 1);

	CD3DX12_ROOT_SIGNATURE_DESC lightingRootSigDesc(rootParameterCount, lightingSlotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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
	_localLightsWireframePSShader = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "LocalLightingWireframePS", "ps_5_1");
	_emissivePSShader = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "EmissivePS", "ps_5_1");
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

	localLightsPSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	localLightsPSODesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	localLightsPSODesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	localLightsPSODesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	localLightsPSODesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	localLightsPSODesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	localLightsPSODesc.DepthStencilState.DepthEnable = TRUE;
	localLightsPSODesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	localLightsPSODesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	ThrowIfFailed(UploadManager::device->CreateGraphicsPipelineState(&localLightsPSODesc, IID_PPV_ARGS(&_localLightsPSO)));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframePSO = localLightsPSODesc;
	wireframePSO.PS =
	{
		reinterpret_cast<BYTE*>(_localLightsWireframePSShader->GetBufferPointer()),
		_localLightsWireframePSShader->GetBufferSize()
	};
	wireframePSO.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;

	ThrowIfFailed(UploadManager::device->CreateGraphicsPipelineState(&wireframePSO, IID_PPV_ARGS(&_localLightsWireframePSO)));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC emissivePSODesc = lightingPSODesc;
	emissivePSODesc.PS =
	{
		reinterpret_cast<BYTE*>(_emissivePSShader->GetBufferPointer()),
		_emissivePSShader->GetBufferSize()
	};

	emissivePSODesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	emissivePSODesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	emissivePSODesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	emissivePSODesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	emissivePSODesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	ThrowIfFailed(UploadManager::device->CreateGraphicsPipelineState(&emissivePSODesc, IID_PPV_ARGS(&_emissivePSO)));
}
#include "LightingManager.h"

using namespace Microsoft::WRL;
using namespace DirectX;

LightingManager::LightingManager(ID3D12Device* device)
{
	_device = device;
	//Create the SRV heap.
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 513;

	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&_shadowSRVHeap)));

	_shadowSRVAllocator = std::make_unique<DescriptorHeapAllocator>(_shadowSRVHeap.Get(), _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV), srvHeapDesc.NumDescriptors);

	//create dsv
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 513;

	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	ThrowIfFailed(_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&_shadowDSVHeap)));

	_shadowDSVAllocator = std::make_unique<DescriptorHeapAllocator>(_shadowDSVHeap.Get(), _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV), dsvHeapDesc.NumDescriptors);

	_dirLightShadowMap = CreateShadowTexture();
}

LightingManager::~LightingManager()
{
}

void LightingManager::AddLight(ID3D12Device* device)
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
	lightItem->ShadowMap = CreateShadowTexture();

	_localLights.push_back(std::move(lightItem));
}

void LightingManager::DeleteLight(int deletedLight)
{
	FreeLightIndices.push_back(_localLights[deletedLight]->LightIndex);
	DeleteShadowTexture(_localLights[deletedLight]->ShadowMap);
	_localLights.erase(_localLights.begin() + deletedLight);
}

void LightingManager::UpdateDirectionalLightCB(FrameResource* currFrameResource)
{
	//for lighting pass
	DirectionalLightConstants dirLightCB;
	dirLightCB.mainLightIsOn = _isMainLightOn;
	dirLightCB.gLightColor = _dirLightColor;

	DirectX::XMVECTOR direction = DirectX::XMLoadFloat3(&_mainLightDirection);
	direction = DirectX::XMVector3Normalize(direction);
	DirectX::XMStoreFloat3(&dirLightCB.mainLightDirection, direction);
	dirLightCB.mainSpotlight = _handSpotlight;
	dirLightCB.lightsContainingFrustum = _lightsContainingFrustum.size();
	auto& mainLightViewProj = CalculateMainLightViewProj();
	dirLightCB.mainLightViewProj = XMMatrixTranspose(mainLightViewProj);

	auto currDirLightCB = currFrameResource->DirLightCB.get();
	currDirLightCB->CopyData(0, dirLightCB);

	//for shadow pass
	ShadowLightConstants shadowCB;
	shadowCB.lightMatrix = XMMatrixTranspose(mainLightViewProj);

	auto currShadowDirLightCB = currFrameResource->ShadowDirLightCB.get();
	currShadowDirLightCB->CopyData(0, shadowCB);
}

void LightingManager::UpdateLightCBs(FrameResource* currFrameResource)
{
	//for update
	auto currLightsCB = currFrameResource->LocalLightCB.get();
	auto currShadowLightsCB = currFrameResource->ShadowLocalLightCB.get();

	//for frustum culling
	XMMATRIX view = _camera->GetView();
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);

	int lightsInsideFrustum = 0;
	int lightsContainingFrustum = 0;
	_lightsInsideFrustum.clear();
	_lightsContainingFrustum.clear();

	auto currLightsInsideFrustumCB = currFrameResource->LightsInsideFrustum.get();
	auto currLightsContainingFrustumCB = currFrameResource->LightsContainingFrustum.get();

	for (auto& light : _localLights)
	{
		//update data if needed
		if (light->NumFramesDirty != 0)
		{
			currLightsCB->CopyData(light->LightIndex, light->LightData);
			ShadowLightConstants lightConstants;

			XMFLOAT3 eye = light->LightData.position;
			XMFLOAT3 target = light->LightData.direction; 
			target.x += eye.x;
			target.y += eye.y;
			target.z += eye.z;

			XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&eye), XMLoadFloat3(&target), XMVectorSet(0.f, 1.f, 0.f, 0.f));
			XMMATRIX proj = XMMatrixPerspectiveFovLH(0.25f * XM_PI, 1.f, 1.f, light->LightData.radius * 2.f);
			lightConstants.lightMatrix = XMMatrixTranspose(view * proj);

			currShadowLightsCB->CopyData(light->LightIndex, lightConstants);
			light->NumFramesDirty--;
		}

		// Transform the camera frustum from view space to the object's local space.
		BoundingFrustum localSpaceFrustum;
		_camera->CameraFrustum().Transform(localSpaceFrustum, invView);

		// Perform the box/frustum intersection test in local space.
		if (localSpaceFrustum.Contains(light->Bounds) == DirectX::DISJOINT)
		{
			continue;
		}
		else if (light->Bounds.Contains(_camera->GetPosition()))
		{
			//we are inside of the light
			currLightsContainingFrustumCB->CopyData(lightsContainingFrustum++, LightIndex(light->LightIndex));
			_lightsContainingFrustum.push_back(light->LightIndex);
		}
		else
		{
			//the light is inside of the frustum
			currLightsInsideFrustumCB->CopyData(lightsInsideFrustum++, LightIndex(light->LightIndex));
			_lightsInsideFrustum.push_back(light->LightIndex);
		}
	}
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

int LightingManager::LightsCount()
{
	return (int)_localLights.size();
}

LightRenderItem* LightingManager::GetLight(int i)
{
	return _localLights[i].get();
}

DirectX::XMMATRIX LightingManager::CalculateMainLightViewProj()
{
	//get the center of a camera frustum
	DirectX::XMFLOAT3 corners[8];
	_camera->CameraFrustum().GetCorners(corners);
	DirectX::XMFLOAT3 target = { 0, 0, 0 };
	for (int i = 0; i < 8; i++)
	{
		target.x += corners[i].x;
		target.y += corners[i].y;
		target.z += corners[i].z;
	}
	target.x /= 8;
	target.y /= 8;
	target.z /= 8;

	float lightDistance = 0.5 * _camera->GetFarZ();

	DirectX::XMFLOAT3 eye = { target.x - lightDistance * _mainLightDirection.x, target.y - lightDistance * _mainLightDirection.y, target.z - lightDistance * _mainLightDirection.z };
	XMVECTOR up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
	if (fabsf(XMVectorGetX(XMVector3Dot(XMLoadFloat3(&_mainLightDirection), up))) > 0.9f)
		up = XMVectorSet(1.f, 0.f, 0.f, 0.f);
	XMMATRIX lightView = DirectX::XMMatrixLookAtLH(XMLoadFloat3(&eye), XMLoadFloat3(&target), up);

	XMVECTOR frustumCorners[8];
	for (int i = 0; i < 8; i++)
		frustumCorners[i] = XMVector3TransformCoord(XMLoadFloat3(&corners[i]), lightView);

	// Step 3: Compute bounds
	XMVECTOR minV = frustumCorners[0];
	XMVECTOR maxV = frustumCorners[0];
	for (int i = 1; i < 8; i++) {
		minV = XMVectorMin(minV, frustumCorners[i]);
		maxV = XMVectorMax(maxV, frustumCorners[i]);
	}
	XMFLOAT3 minPt, maxPt;
	XMStoreFloat3(&minPt, minV);
	XMStoreFloat3(&maxPt, maxV);

	// Step 4: Orthographic projection that fits the frustum
	XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(
		minPt.x, maxPt.x,
		minPt.y, maxPt.y,
		minPt.z, maxPt.z
	);

	return lightView * lightProj;
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
	CD3DX12_GPU_DESCRIPTOR_HANDLE tex(_shadowDSVAllocator->GetGpuHandle(_dirLightShadowMap.SRV));
	cmdList->SetGraphicsRootDescriptorTable(5, tex);

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
	cmdList->DrawIndexedInstanced(mesh.IndexCount, (UINT)_lightsInsideFrustum.size(), mesh.StartIndexLocation,
		mesh.BaseVertexLocation, 0);
}

void LightingManager::DrawDebug(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource)
{
	auto geo = GeometryManager::geometries()["shapeGeo"].begin()->get();
	SubmeshGeometry mesh = geo->DrawArgs["box"];
	cmdList->SetPipelineState(_localLightsWireframePSO.Get());
	cmdList->DrawIndexedInstanced(mesh.IndexCount, (UINT)_lightsInsideFrustum.size(), mesh.StartIndexLocation, mesh.BaseVertexLocation, 0);
}

void LightingManager::DrawEmissive(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource)
{
	cmdList->SetPipelineState(_emissivePSO.Get());

	cmdList->DrawInstanced(3, 1, 0, 0);
}

void LightingManager::DrawShadows(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, std::vector<std::shared_ptr<EditableRenderItem>>& objects)
{
	std::vector<CD3DX12_RESOURCE_BARRIER> barriers;
	for (int i = 0; i < _localLights.size(); i++)
	{
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(_localLights[i]->ShadowMap.Resource.Get(), D3D12_RESOURCE_STATE_DEPTH_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));
	}
	if (barriers.size() != 0)
	{
		cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
	}

	cmdList->SetGraphicsRootSignature(_shadowRootSignature.Get());
	cmdList->SetPipelineState(_shadowPSO.Get());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	if (_isMainLightOn)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE tex(_shadowDSVAllocator->GetCpuHandle(_dirLightShadowMap.DSV));
		cmdList->OMSetRenderTargets(0, nullptr, false, &tex);
		cmdList->ClearDepthStencilView(tex, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		cmdList->SetGraphicsRootConstantBufferView(1, currFrameResource->ShadowDirLightCB->Resource()->GetGPUVirtualAddress());

		std::vector<int> visibleObjects = FrustumCulling(objects, CalculateDirLightAABB());
		ShadowPass(currFrameResource, cmdList, visibleObjects, objects);
	}

	std::vector<int> visibleLights = _lightsContainingFrustum;
	visibleLights.insert(visibleLights.end(), _lightsInsideFrustum.begin(), _lightsInsideFrustum.end());

	for (auto& light : _localLights)
	{
		if (!light->LightData.active || light->LightData.type == 0)
			continue;

		if (std::find(visibleLights.begin(), visibleLights.end(), light->LightIndex) == visibleLights.end())
			continue;

		BoundingSphere lightWorldAABB;
		light->Bounds.Transform(lightWorldAABB, light->LightData.world);
		CD3DX12_CPU_DESCRIPTOR_HANDLE tex(_shadowDSVAllocator->GetCpuHandle(light->ShadowMap.DSV));
		cmdList->OMSetRenderTargets(0, nullptr, false, &tex);
		cmdList->ClearDepthStencilView(tex, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		auto currShadowLightsCB = currFrameResource->ShadowLocalLightCB.get()->Resource();

		D3D12_GPU_VIRTUAL_ADDRESS localLightCBAddress = currShadowLightsCB->GetGPUVirtualAddress() + light->LightIndex * sizeof(ShadowLightConstants);
		cmdList->SetGraphicsRootConstantBufferView(1, localLightCBAddress);

		std::vector<int> visibleObjects = FrustumCulling(objects, lightWorldAABB);
		ShadowPass(currFrameResource, cmdList, visibleObjects, objects);
	}

	barriers.clear();
	for (int i = 0; i < _localLights.size(); i++)
	{
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(_localLights[i]->ShadowMap.Resource.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_DEPTH_READ));
	}
	if (barriers.size() != 0)
	{
		cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
	}
}

void LightingManager::Init(int srvAmount, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle)
{
	// Create buffer on GPU
	CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(Light) * MaxLights);

	_device->CreateCommittedResource(
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

	_device->CreateShaderResourceView(_lightBufferGPU.Get(), &srvDesc, srvHandle);
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
	//shadow map
	CD3DX12_DESCRIPTOR_RANGE shadowTexTable;
	shadowTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, srvAmount);

	const int rootParameterCount = 6;

	CD3DX12_ROOT_PARAMETER lightingSlotRootParameter[rootParameterCount];

	lightingSlotRootParameter[0].InitAsDescriptorTable(1, &lightingRange);
	lightingSlotRootParameter[1].InitAsShaderResourceView(0, 1);
	lightingSlotRootParameter[2].InitAsConstantBufferView(0);
	lightingSlotRootParameter[3].InitAsConstantBufferView(1);
	lightingSlotRootParameter[4].InitAsShaderResourceView(1, 1);
	lightingSlotRootParameter[5].InitAsDescriptorTable(1, &shadowTexTable, D3D12_SHADER_VISIBILITY_PIXEL);

	CD3DX12_ROOT_SIGNATURE_DESC lightingRootSigDesc(rootParameterCount, lightingSlotRootParameter, 1, &TextureManager::GetStaticSamplers().data()[0], D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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

	//shadow root signature
	CD3DX12_ROOT_PARAMETER shadowSlotRootParameter[2];
	shadowSlotRootParameter[0].InitAsConstantBufferView(0);
	shadowSlotRootParameter[1].InitAsConstantBufferView(1);
	CD3DX12_ROOT_SIGNATURE_DESC shadowRootSigDesc(2, shadowSlotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	ComPtr<ID3DBlob> shadowSerializedRootSig = nullptr;
	ComPtr<ID3DBlob> shadowErrorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&shadowRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		shadowSerializedRootSig.GetAddressOf(), shadowErrorBlob.GetAddressOf());
	if (shadowErrorBlob != nullptr)
	{
		::OutputDebugStringA((char*)shadowErrorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);
	ThrowIfFailed(_device->CreateRootSignature(
		0,
		shadowSerializedRootSig->GetBufferPointer(),
		shadowSerializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(_shadowRootSignature.GetAddressOf())));

}

void LightingManager::BuildShaders()
{
	_dirLightVSShader = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "DirLightingVS", "vs_5_1");
	_dirLightPSShader = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "DirLightingPS", "ps_5_1");
	_localLightsVSShader = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "LocalLightingVS", "vs_5_1");
	_localLightsPSShader = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "LocalLightingPS", "ps_5_1");
	_localLightsWireframePSShader = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "LocalLightingWireframePS", "ps_5_1");
	_emissivePSShader = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "EmissivePS", "ps_5_1");
	_shadowVSShader = d3dUtil::CompileShader(L"Shaders\\ShadowPass.hlsl", nullptr, "ShadowVS", "vs_5_1");
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
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&lightingPSODesc, IID_PPV_ARGS(&_dirLightPSO)));

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

	ThrowIfFailed(_device->CreateGraphicsPipelineState(&localLightsPSODesc, IID_PPV_ARGS(&_localLightsPSO)));

	//wireframe pso
	D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframePSO = localLightsPSODesc;
	wireframePSO.PS =
	{
		reinterpret_cast<BYTE*>(_localLightsWireframePSShader->GetBufferPointer()),
		_localLightsWireframePSShader->GetBufferSize()
	};
	wireframePSO.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;

	ThrowIfFailed(_device->CreateGraphicsPipelineState(&wireframePSO, IID_PPV_ARGS(&_localLightsWireframePSO)));

	//emissive pso
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

	ThrowIfFailed(_device->CreateGraphicsPipelineState(&emissivePSODesc, IID_PPV_ARGS(&_emissivePSO)));

	//shadow pso
	D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPSODesc;
	ZeroMemory(&shadowPSODesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	shadowPSODesc.InputLayout = { _shadowInputLayout.data(), (UINT)_shadowInputLayout.size() };
	shadowPSODesc.pRootSignature = _shadowRootSignature.Get();
	shadowPSODesc.VS =
	{
		reinterpret_cast<BYTE*>(_shadowVSShader->GetBufferPointer()),
		_shadowVSShader->GetBufferSize()
	};
	shadowPSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	shadowPSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	shadowPSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	shadowPSODesc.SampleMask = UINT_MAX;
	shadowPSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	shadowPSODesc.NumRenderTargets = 0;
	shadowPSODesc.SampleDesc.Count = 1;
	shadowPSODesc.SampleDesc.Quality = 0;
	shadowPSODesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&shadowPSODesc, IID_PPV_ARGS(&_shadowPSO)));
}

ShadowTexture LightingManager::CreateShadowTexture()
{
	ShadowTexture shadowMap;

	//texture
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = 1028;
	texDesc.Height = 1028;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R24G8_TYPELESS; 
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;


	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

	ThrowIfFailed(_device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
		&clearValue, IID_PPV_ARGS(&shadowMap.Resource)));

	//dsv
	UINT dsvIndex = _shadowDSVAllocator.get()->Allocate();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = _shadowDSVAllocator.get()->GetCpuHandle(dsvIndex);

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.Texture2D.MipSlice = 0;
	_device->CreateDepthStencilView(shadowMap.Resource.Get(), &dsvDesc, dsvHandle);

	shadowMap.DSV = dsvIndex;

	//create SRV
	UINT srvIndex = _shadowSRVAllocator.get()->Allocate();
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = _shadowSRVAllocator.get()->GetCpuHandle(srvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	_device->CreateShaderResourceView(shadowMap.Resource.Get(), &srvDesc, srvHandle);

	shadowMap.SRV = srvIndex;

	UploadManager::ExecuteUploadCommandList();

	return shadowMap;
}

void LightingManager::DeleteShadowTexture(ShadowTexture tex)
{
	UploadManager::Flush();
	tex.Resource.ReleaseAndGetAddressOf();
	_shadowDSVAllocator->Free(tex.DSV);
	_shadowSRVAllocator->Free(tex.SRV);
}

std::vector<int> LightingManager::FrustumCulling(std::vector<std::shared_ptr<EditableRenderItem>>& objects, DirectX::BoundingBox lightAABB)
{
	std::vector<int> visibleObjects;

	for (int i = 0; i < objects.size(); i++)
	{
		BoundingBox worldBounds;

		DirectX::XMMATRIX translation = XMMatrixTranslationFromVector(XMLoadFloat3(&objects[i]->transform[BasicUtil::EnumIndex(Transform::Translation)]));
		DirectX::XMMATRIX rotation = XMMatrixRotationRollPitchYawFromVector(XMLoadFloat3(&objects[i]->transform[BasicUtil::EnumIndex(Transform::Rotation)]));
		DirectX::XMMATRIX scale = XMMatrixScalingFromVector(XMLoadFloat3(&objects[i]->transform[BasicUtil::EnumIndex(Transform::Scale)]));
		DirectX::XMMATRIX world = scale * rotation * translation;

		objects[i]->Bounds.Transform(worldBounds, world);
		if (lightAABB.Contains(objects[i]->Bounds) != DirectX::DISJOINT)
		{
			visibleObjects.push_back(i);
		}
	}

	return visibleObjects;
}

std::vector<int> LightingManager::FrustumCulling(std::vector<std::shared_ptr<EditableRenderItem>>& objects, DirectX::BoundingSphere lightAABB)
{
	std::vector<int> visibleObjects;

	for (int i = 0; i < objects.size(); i++)
	{
		BoundingBox worldBounds;

		DirectX::XMMATRIX translation = XMMatrixTranslationFromVector(XMLoadFloat3(&objects[i]->transform[BasicUtil::EnumIndex(Transform::Translation)]));
		DirectX::XMMATRIX rotation = XMMatrixRotationRollPitchYawFromVector(XMLoadFloat3(&objects[i]->transform[BasicUtil::EnumIndex(Transform::Rotation)]));
		DirectX::XMMATRIX scale = XMMatrixScalingFromVector(XMLoadFloat3(&objects[i]->transform[BasicUtil::EnumIndex(Transform::Scale)]));
		DirectX::XMMATRIX world = scale * rotation * translation;

		objects[i]->Bounds.Transform(worldBounds, world);
		if (lightAABB.Contains(objects[i]->Bounds) != DirectX::DISJOINT)
		{
			visibleObjects.push_back(i);
		}
	}

	return visibleObjects;
}

BoundingBox LightingManager::CalculateDirLightAABB()
{
	//get the center of a camera frustum
	DirectX::XMFLOAT3 corners[8];
	DirectX::XMFLOAT3 minV{ FLT_MAX, FLT_MAX, FLT_MAX };
	DirectX::XMFLOAT3 maxV{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
	_camera->CameraFrustum().GetCorners(corners);
	for (int i = 0; i < 8; i++)
	{
		minV.x = std::min(corners[i].x, minV.x);
		minV.y = std::min(corners[i].y, minV.y);
		minV.z = std::min(corners[i].z, minV.z);
		maxV.x = std::max(corners[i].x, maxV.x);
		maxV.y = std::max(corners[i].y, maxV.y);
		maxV.z = std::max(corners[i].z, maxV.z);
	}
	float lightDistance = 0.5 * _camera->GetFarZ();

	minV.x = std::min(minV.x, minV.x - lightDistance * _mainLightDirection.x);
	minV.y = std::min(minV.y, minV.y - lightDistance * _mainLightDirection.y);
	minV.z = std::min(minV.z, minV.z - lightDistance * _mainLightDirection.z);
	maxV.x = std::min(maxV.x, maxV.x - lightDistance * _mainLightDirection.x);
	maxV.y = std::min(maxV.y, maxV.y - lightDistance * _mainLightDirection.y);
	maxV.z = std::min(maxV.z, maxV.z - lightDistance * _mainLightDirection.z);

	BoundingBox aabb;
	BoundingBox::CreateFromPoints(aabb, XMLoadFloat3(&minV), XMLoadFloat3(&maxV));
	return aabb;
}

void LightingManager::ShadowPass(FrameResource* currFrameResource, ID3D12GraphicsCommandList* cmdList, std::vector<int> visibleObjects, std::vector<std::shared_ptr<EditableRenderItem>>& objects)
{
	for (auto& idx : visibleObjects)
	{
		auto& ri = *objects[idx];
		auto objectCB = currFrameResource->OpaqueObjCB[ri.uid]->Resource();
		int curLODIdx = ri.currentLODIdx;

		MeshGeometry* curLODGeo = ri.Geo->at(curLODIdx).get();
		cmdList->IASetVertexBuffers(0, 1, &curLODGeo->VertexBufferView());
		cmdList->IASetIndexBuffer(&curLODGeo->IndexBufferView());

		auto currentLOD = ri.lodsData[curLODIdx];
		for (size_t i = 0; i < currentLOD.meshes.size(); i++)
		{
			auto& meshData = currentLOD.meshes.at(i);
			D3D12_GPU_VIRTUAL_ADDRESS meshCBAddress = objectCB->GetGPUVirtualAddress() + meshData.cbOffset;
			cmdList->SetGraphicsRootConstantBufferView(0, meshCBAddress);
			cmdList->DrawIndexedInstanced(meshData.indexCount, 1, meshData.indexStart, meshData.vertexStart, 0);
		}
	}
}

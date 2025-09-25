#include "LightingManager.h"

using namespace Microsoft::WRL;
using namespace DirectX;

LightingManager::LightingManager(ID3D12Device* device)
{
	_device = device;

	//creating shadow textures arrays
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = _shadowMapResolution;
	texDesc.Height = _shadowMapResolution;
	texDesc.DepthOrArraySize = (UINT16)gCascadesCount; // first cascades
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R32_TYPELESS; // for depth
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_D32_FLOAT;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

	ThrowIfFailed(_device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
		&clearValue, IID_PPV_ARGS(&_cascadeShadowTextureArray.textureArray)));

	texDesc.DepthOrArraySize = (UINT16)MaxLights; // then local lights
	ThrowIfFailed(_device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
		&clearValue, IID_PPV_ARGS(&_localLightsShadowTextureArray.textureArray)
	));

	//create SRVs for texture arrays
	auto& allocator = TextureManager::srvHeapAllocator;
	UINT srvIndex = allocator->Allocate();
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = allocator->GetCpuHandle(srvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Texture2DArray.ArraySize = gCascadesCount;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = 1;
	srvDesc.Texture2DArray.PlaneSlice = 0;
	_device->CreateShaderResourceView(_cascadeShadowTextureArray.textureArray.Get(), &srvDesc, srvHandle);
	_cascadeShadowTextureArray.SRV = srvIndex;

	srvDesc.Texture2DArray.ArraySize = MaxLights;
	srvIndex = allocator->Allocate();
	srvHandle = allocator->GetCpuHandle(srvIndex);
	_device->CreateShaderResourceView(_localLightsShadowTextureArray.textureArray.Get(), &srvDesc, srvHandle);
	_localLightsShadowTextureArray.SRV = srvIndex;

	//make dsvs for cascades
	for (int i = 0; i < gCascadesCount; i++)
		_cascades[i].shadowMapDSV = CreateShadowTextureDSV(true, i);

	//rects for shadows should be different
	_shadowViewport.Height = _shadowMapResolution;
	_shadowViewport.Width = _shadowMapResolution;
	_shadowScissorRect.right = _shadowMapResolution;
	_shadowScissorRect.bottom = _shadowMapResolution;
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
		if (NextAvailableLightIndex >= MaxLights) {
			throw std::runtime_error("Maximum number of lights exceeded!");
		}
		lightIndex = NextAvailableLightIndex++;
	}

	auto lightItem = std::make_unique<LightRenderItem>();
	lightItem->LightIndex = lightIndex;
	lightItem->NumFramesDirty = gNumFrameResources;
	lightItem->LightData = Light();
	float radius = lightItem->LightData.radius;
	float diameter = radius * 2.f;
	lightItem->LightData.world = XMMatrixScalingFromVector(XMVectorSet(diameter, diameter, diameter, 1));
	lightItem->Bounds = BoundingSphere(lightItem->LightData.position, lightItem->LightData.radius);
	lightItem->ShadowMapDSV = CreateShadowTextureDSV(false, lightIndex);

	_localLights.push_back(std::move(lightItem));
}

void LightingManager::DeleteLight(int deletedLight)
{
	FreeLightIndices.push_back(_localLights[deletedLight]->LightIndex);
	DeleteShadowTexture(_localLights[deletedLight]->ShadowMapDSV);
	_localLights.erase(_localLights.begin() + deletedLight);
}

void LightingManager::UpdateDirectionalLightCB(FrameResource* currFrameResource)
{
	//for lighting pass and shadow pass
	DirectionalLightConstants dirLightCB;
	dirLightCB.mainLightIsOn = _isMainLightOn;
	dirLightCB.gLightColor = _dirLightColor;

	DirectX::XMVECTOR direction = DirectX::XMLoadFloat3(&_mainLightDirection);
	direction = DirectX::XMVector3Normalize(direction);
	DirectX::XMStoreFloat3(&dirLightCB.mainLightDirection, direction);
	dirLightCB.mainSpotlight = _handSpotlight;
	dirLightCB.lightsContainingFrustum = _lightsContainingFrustum.size();

	auto currShadowDirLightCB = currFrameResource->ShadowDirLightCB.get();

	CalculateCascadesViewProjs();

	for (int i = 0; i < gCascadesCount; i++)
	{
		Cascade cascadeForCB = _cascades[i].cascade;
		cascadeForCB.viewProj = XMMatrixTranspose(cascadeForCB.viewProj);

		dirLightCB.cascades[i] = cascadeForCB;

		//storing in shadow right away for a better locality
		ShadowLightConstants shadowCB;
		shadowCB.lightMatrix = cascadeForCB.viewProj;
		currShadowDirLightCB->CopyData(i, shadowCB);
	}

	auto currDirLightCB = currFrameResource->DirLightCB.get();
	currDirLightCB->CopyData(0, dirLightCB);
}

void LightingManager::UpdateLightCBs(FrameResource* currFrameResource)
{
	//for update
	auto currLightsCB = currFrameResource->LocalLightCB.get();
	auto currShadowLightsCB = currFrameResource->ShadowLocalLightCB.get();

	//for frustum culling
	XMMATRIX invView = _camera->GetInvView();

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

void LightingManager::CalculateCascadesViewProjs()
{
	//get the center of a camera frustum
	XMFLOAT3 corners[8];
	_camera->CameraFrustum().GetCorners(corners);

	XMMATRIX camWorld = _camera->GetInvView(); // camera world matrix
	for (int i = 0; i < 8; ++i)
	{
		XMVECTOR worldPos = XMVector3Transform(XMLoadFloat3(&corners[i]), camWorld);
		XMStoreFloat3(&corners[i], worldPos);
	}

	float lightDistance = 100.0f;

	XMVECTOR lightDir = XMLoadFloat3(&_mainLightDirection);
	lightDir = XMVector3Normalize(lightDir);

	XMVECTOR up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
	if (fabsf(XMVectorGetX(XMVector3Dot(XMLoadFloat3(&_mainLightDirection), up))) > 0.9f)
		up = XMVectorSet(1.f, 0.f, 0.f, 0.f);

	XMVECTOR frustumCorners[8];
	for (int i = 0; i < 8; i++)
		frustumCorners[i] = XMLoadFloat3(&corners[i]);

	//calculating cascades
	float nearZ = _camera->GetNearZ();
	float farZ = _camera->GetFarZ();

	float zPad = 10.0f; //to avoid clipping

	for (int i = 0; i < gCascadesCount; i++)
	{
		float splitNearRatio = i / (float)gCascadesCount;
		float splitFarRatio = (i + 1) / (float)gCascadesCount;

		_cascades[i].cascade.splitNear = nearZ + (farZ - nearZ) * splitNearRatio;
		_cascades[i].cascade.splitFar = nearZ + (farZ - nearZ) * splitFarRatio + zPad;

		//interpolate corners in world space
		XMVECTOR cascadeCorners[8];

		for (int j = 0; j < 4; j++) {

			cascadeCorners[j] = XMVectorLerp(frustumCorners[j], frustumCorners[j + 4], splitNearRatio);
			cascadeCorners[j + 4] = XMVectorLerp(frustumCorners[j], frustumCorners[j + 4], splitFarRatio);
		}

		// also make a different view matrix
		XMVECTOR cascadeCenter = XMVectorZero();
		for (int j = 0; j < 8; ++j)
			cascadeCenter += cascadeCorners[j];
		cascadeCenter /= 8.0f;

		XMVECTOR cascadeEye = cascadeCenter - lightDir * lightDistance;
		_cascades[i].lightView = XMMatrixLookAtLH(cascadeEye, cascadeCenter, up);

		//get them into light space
		XMVECTOR lsCorners[8];
		for (int j = 0; j < 8; j++)
			lsCorners[j] = XMVector3TransformCoord(cascadeCorners[j], _cascades[i].lightView);

		//min max
		XMVECTOR cascadeMinV = lsCorners[0];
		XMVECTOR cascadeMaxV = lsCorners[0];
		for (int j = 1; j < 8; j++)
		{
			cascadeMinV = XMVectorMin(cascadeMinV, lsCorners[j]);
			cascadeMaxV = XMVectorMax(cascadeMaxV, lsCorners[j]);
		}

		XMFLOAT3 cascadeMinPt, cascadeMaxPt;
		XMStoreFloat3(&cascadeMinPt, cascadeMinV);
		XMStoreFloat3(&cascadeMaxPt, cascadeMaxV);

		//some padding
		cascadeMinPt.z -= zPad;
		cascadeMaxPt.z += zPad;

		// snap to texel to remove shimmering
		SnapToTexel(cascadeMinPt, cascadeMaxPt);

		XMMATRIX cascadeProj = XMMatrixOrthographicOffCenterLH(
			cascadeMinPt.x, cascadeMaxPt.x,
			cascadeMinPt.y, cascadeMaxPt.y,
			cascadeMinPt.z, cascadeMaxPt.z
		);

		cascadeMinV = XMLoadFloat3(&cascadeMinPt);
		cascadeMaxV = XMLoadFloat3(&cascadeMaxPt);

		_cascades[i].cascade.viewProj = _cascades[i].lightView * cascadeProj;
		BoundingBox::CreateFromPoints(_cascades[i].AABB, cascadeMinV, cascadeMaxV);
	}
}

void LightingManager::DrawDirLight(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, D3D12_GPU_DESCRIPTOR_HANDLE descTable)
{
	// Indicate a state transition on the resource usage.

	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	cmdList->SetGraphicsRootDescriptorTable(0, descTable);
	cmdList->SetGraphicsRootShaderResourceView(1, currFrameResource->LocalLightCB.get()->Resource()->GetGPUVirtualAddress());
	cmdList->SetGraphicsRootShaderResourceView(4, currFrameResource->LightsContainingFrustum.get()->Resource()->GetGPUVirtualAddress());
	cmdList->SetGraphicsRootDescriptorTable(5, TextureManager::srvHeapAllocator->GetGpuHandle(_cascadeShadowTextureArray.SRV));
	cmdList->SetGraphicsRootDescriptorTable(6, TextureManager::srvHeapAllocator->GetGpuHandle(_localLightsShadowTextureArray.SRV));

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
	if (objects.empty())
		return;

	cmdList->RSSetViewports(1, &_shadowViewport);
	cmdList->RSSetScissorRects(1, &_shadowScissorRect);

	cmdList->SetGraphicsRootSignature(_shadowRootSignature.Get());
	cmdList->SetPipelineState(_shadowPSO.Get());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	if (_isMainLightOn)
	{
		//changing state of shadow map to dsv
		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(_cascadeShadowTextureArray.textureArray.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));

		auto dsvAllocator = TextureManager::dsvHeapAllocator.get();

		//render each cascade shadow map
		for (UINT i = 0; i < gCascadesCount; i++)
		{
			CD3DX12_CPU_DESCRIPTOR_HANDLE tex(dsvAllocator->GetCpuHandle(_cascades[i].shadowMapDSV));
			cmdList->ClearDepthStencilView(tex, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			//check frustum culling so we do not need to load gpu with unnecessary commands
			std::vector<int> visibleObjects = FrustumCulling(objects, i);
			if (visibleObjects.empty())
				continue;

			cmdList->OMSetRenderTargets(0, nullptr, false, &tex);
			cmdList->SetGraphicsRootConstantBufferView(1, currFrameResource->ShadowDirLightCB->Resource()->GetGPUVirtualAddress() + i * _shadowCBSize);

			ShadowPass(currFrameResource, cmdList, visibleObjects, objects);
		}
		//changing state back to srv
		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(_cascadeShadowTextureArray.textureArray.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
	}

	//the same for local lights
	std::vector<int> visibleLights = _lightsContainingFrustum;
	visibleLights.insert(visibleLights.end(), _lightsInsideFrustum.begin(), _lightsInsideFrustum.end());

	if (visibleLights.empty())
		return;

	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(_localLightsShadowTextureArray.textureArray.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));
	
	auto dsvAllocator = TextureManager::dsvHeapAllocator.get();
	for (auto& light : _localLights)
	{
		if (!light->LightData.active || light->LightData.type == 0)
			continue;

		if (std::find(visibleLights.begin(), visibleLights.end(), light->LightIndex) == visibleLights.end())
			continue;

		BoundingSphere lightWorldAABB;
		light->Bounds.Transform(lightWorldAABB, light->LightData.world);
		std::vector<int> visibleObjects = FrustumCulling(objects, lightWorldAABB);
		if (visibleObjects.empty())
			continue;

		CD3DX12_CPU_DESCRIPTOR_HANDLE tex(dsvAllocator->GetCpuHandle(light->ShadowMapDSV));
		cmdList->OMSetRenderTargets(0, nullptr, false, &tex);
		cmdList->ClearDepthStencilView(tex, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		auto currShadowLightsCB = currFrameResource->ShadowLocalLightCB.get()->Resource();

		D3D12_GPU_VIRTUAL_ADDRESS localLightCBAddress = currShadowLightsCB->GetGPUVirtualAddress() + light->LightIndex * sizeof(ShadowLightConstants);
		cmdList->SetGraphicsRootConstantBufferView(1, localLightCBAddress);

		ShadowPass(currFrameResource, cmdList, visibleObjects, objects);

	}
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(_localLightsShadowTextureArray.textureArray.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
}

void LightingManager::Init(int srvAmount)
{
	BuildRootSignature(srvAmount);
	BuildShaders();
	BuildInputLayout();
	BuildPSO();
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
	//cascades shadow map
	CD3DX12_DESCRIPTOR_RANGE cascadesShadowTexTable;
	cascadesShadowTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, srvAmount++);
	//shadow map
	CD3DX12_DESCRIPTOR_RANGE shadowTexTable;
	shadowTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, srvAmount++);

	const int rootParameterCount = 7;

	CD3DX12_ROOT_PARAMETER lightingSlotRootParameter[rootParameterCount];

	lightingSlotRootParameter[0].InitAsDescriptorTable(1, &lightingRange);
	lightingSlotRootParameter[1].InitAsShaderResourceView(0, 1);
	lightingSlotRootParameter[2].InitAsConstantBufferView(0);
	lightingSlotRootParameter[3].InitAsConstantBufferView(1);
	lightingSlotRootParameter[4].InitAsShaderResourceView(1, 1);
	lightingSlotRootParameter[5].InitAsDescriptorTable(1, &cascadesShadowTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	lightingSlotRootParameter[6].InitAsDescriptorTable(1, &shadowTexTable, D3D12_SHADER_VISIBILITY_PIXEL);

	CD3DX12_ROOT_SIGNATURE_DESC lightingRootSigDesc(rootParameterCount, lightingSlotRootParameter, TextureManager::GetShadowSamplers().size(), TextureManager::GetShadowSamplers().data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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

	//rasterizer state with biases
	D3D12_RASTERIZER_DESC shadowRasterDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

	//useless thing
	/*shadowRasterDesc.DepthBias = 100;
	shadowRasterDesc.DepthBiasClamp = 10.0f;
	*/

	shadowRasterDesc.SlopeScaledDepthBias = 1.5f;

	shadowPSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	shadowPSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	shadowPSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	shadowPSODesc.SampleMask = UINT_MAX;
	shadowPSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	shadowPSODesc.NumRenderTargets = 0;
	shadowPSODesc.SampleDesc.Count = 1;
	shadowPSODesc.SampleDesc.Quality = 0;
	shadowPSODesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&shadowPSODesc, IID_PPV_ARGS(&_shadowPSO)));
}

int LightingManager::CreateShadowTextureDSV(bool forCascade, int index)
{
	ID3D12Resource* textureArray = forCascade ? _cascadeShadowTextureArray.textureArray.Get() : _localLightsShadowTextureArray.textureArray.Get();

	//dsv
	UINT dsvIndex = TextureManager::dsvHeapAllocator->Allocate();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = TextureManager::dsvHeapAllocator->GetCpuHandle(dsvIndex);

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.Texture2DArray.ArraySize = 1;
	dsvDesc.Texture2DArray.FirstArraySlice = index;
	dsvDesc.Texture2DArray.MipSlice = 0;
	_device->CreateDepthStencilView(textureArray, &dsvDesc, dsvHandle);

	UploadManager::ExecuteUploadCommandList();

	return dsvIndex;
}

void LightingManager::DeleteShadowTexture(int texIdx)
{
	UploadManager::Flush();
	TextureManager::dsvHeapAllocator->Free(texIdx);
}

std::vector<int> LightingManager::FrustumCulling(std::vector<std::shared_ptr<EditableRenderItem>>& objects, int cascadeIdx) const
{
	BoundingBox lightAABB = _cascades[cascadeIdx].AABB;
	std::vector<int> visibleObjects;

	for (int i = 0; i < objects.size(); i++)
	{
		BoundingBox objectLSBounds;
		BoundingBox objectWorldBounds;

		objects[i]->Bounds.Transform(objectLSBounds, objects[i]->world * _cascades[cascadeIdx].lightView);
		objects[i]->Bounds.Transform(objectWorldBounds, objects[i]->world);
		float distance;
		XMStoreFloat(&distance, XMVector3Length(XMLoadFloat3(&objectWorldBounds.Center) - _camera->GetPosition()));
		if (lightAABB.Contains(objectLSBounds) != DirectX::DISJOINT)
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

		objects[i]->Bounds.Transform(worldBounds, objects[i]->world);
		if (lightAABB.Contains(worldBounds) != DirectX::DISJOINT)
		{
			visibleObjects.push_back(i);
		}
	}

	return visibleObjects;
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
			cmdList->DrawIndexedInstanced((UINT)meshData.indexCount, 1, (UINT)meshData.indexStart, (UINT)meshData.vertexStart, 0);
		}
	}
}

void LightingManager::SnapToTexel(DirectX::XMFLOAT3& cascadeMinPt, DirectX::XMFLOAT3& cascadeMaxPt) const
{
	float worldUnitsPerTexelX = (cascadeMaxPt.x - cascadeMinPt.x) / (float)_shadowMapResolution;
	float worldUnitsPerTexelY = (cascadeMaxPt.y - cascadeMinPt.y) / (float)_shadowMapResolution;

	// compute center in light space
	float cx = (cascadeMinPt.x + cascadeMaxPt.x) * 0.5f;
	float cy = (cascadeMinPt.y + cascadeMaxPt.y) * 0.5f;

	// snap center to texel grid:
	cx = floorf(cx / worldUnitsPerTexelX) * worldUnitsPerTexelX;
	cy = floorf(cy / worldUnitsPerTexelY) * worldUnitsPerTexelY;

	// recompute min/max from snapped center
	cascadeMinPt.x = cx - (cascadeMaxPt.x - cascadeMinPt.x) * 0.5f;
	cascadeMaxPt.x = cx + (cascadeMaxPt.x - cascadeMinPt.x) * 0.5f;
	cascadeMinPt.y = cy - (cascadeMaxPt.y - cascadeMinPt.y) * 0.5f;
	cascadeMaxPt.y = cy + (cascadeMaxPt.y - cascadeMinPt.y) * 0.5f;
}

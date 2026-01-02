#include "LightingManager.h"

#include "UploadManager.h"

using namespace Microsoft::WRL;
using namespace DirectX;

LightingManager::LightingManager(ID3D12Device* device, const UINT width, const UINT height, const bool rayTracingSupported)
{
	_device = device;
	_width = width;
	_height = height;
	_rayTracingSupported = rayTracingSupported;
}

void LightingManager::AddLight(ID3D12Device* device)
{
	int lightIndex;
	if (!_freeLightIndices.empty()) {
		lightIndex = _freeLightIndices.back();
		_freeLightIndices.pop_back();
	}
	else {
		if (_nextAvailableLightIndex >= _maxLights) {
			throw std::runtime_error("Maximum number of lights exceeded!");
		}
		lightIndex = _nextAvailableLightIndex++;
	}

	auto lightItem = std::make_unique<LightRenderItem>();
	lightItem->LightIndex = lightIndex;
	lightItem->NumFramesDirty = gNumFrameResources;
	lightItem->LightData = Light();
	float radius = lightItem->LightData.Radius;
	float diameter = radius * 2.f;
	lightItem->LightData.World = XMMatrixScalingFromVector(XMVectorSet(diameter, diameter, diameter, 1));
	lightItem->Bounds = BoundingSphere(lightItem->LightData.Position, lightItem->LightData.Radius);
	lightItem->ShadowMapDsv = CreateShadowTextureDsv(false, lightIndex);

	_localLights.push_back(std::move(lightItem));
}

void LightingManager::DeleteLight(const int deletedLight)
{
	_freeLightIndices.push_back(_localLights[deletedLight]->LightIndex);
	DeleteShadowTexture(_localLights[deletedLight]->ShadowMapDsv);
	_localLights.erase(_localLights.begin() + deletedLight);
}

void LightingManager::UpdateDirectionalLightCb(const FrameResource* currFrameResource)
{
	//for lighting pass and shadow pass
	DirectionalLightConstants dirLightCb;
	dirLightCb.MainLightIsOn = _isMainLightOn;
	dirLightCb.LightColor = _dirLightColor;

	DirectX::XMVECTOR direction = DirectX::XMLoadFloat3(&_mainLightDirection);
	direction = DirectX::XMVector3Normalize(direction);
	DirectX::XMStoreFloat3(&dirLightCb.MainLightDirection, direction);
	dirLightCb.MainSpotlight = _handSpotlight;
	dirLightCb.LightsContainingFrustum = static_cast<int>(_lightsContainingFrustum.size());

	const auto currShadowDirLightCb = currFrameResource->ShadowDirLightCb.get();

	CalculateCascadesViewProjs();

	for (int i = 0; i < gCascadesCount; i++)
	{
		Cascade cascadeForCb = _cascades[i].Cascade;
		cascadeForCb.ViewProj = XMMatrixTranspose(cascadeForCb.ViewProj);

		dirLightCb.Cascades[i] = cascadeForCb;

		//storing in shadow right away for a better locality
		ShadowLightConstants shadowCb;
		shadowCb.LightMatrix = cascadeForCb.ViewProj;
		currShadowDirLightCb->CopyData(i, shadowCb);
	}

	const auto currDirLightCb = currFrameResource->DirLightCb.get();
	currDirLightCb->CopyData(0, dirLightCb);
}

void LightingManager::UpdateLightCBs(const FrameResource* currFrameResource)
{
	//for update
	const auto currLightsCb = currFrameResource->LocalLightCb.get();
	const auto currShadowLightsCb = currFrameResource->ShadowLocalLightCb.get();

	//for frustum culling
	const XMMATRIX invView = _camera->GetInvView();

	int lightsInsideFrustum = 0;
	int lightsContainingFrustum = 0;
	_lightsInsideFrustum.clear();
	_lightsContainingFrustum.clear();

	const auto currLightsInsideFrustumCb = currFrameResource->LightsInsideFrustum.get();
	const auto currLightsContainingFrustumCb = currFrameResource->LightsContainingFrustum.get();

	for (const auto& light : _localLights)
	{
		//update data if needed
		if (light->NumFramesDirty != 0)
		{
			currLightsCb->CopyData(light->LightIndex, light->LightData);
			ShadowLightConstants lightConstants;

			XMFLOAT3 eye = light->LightData.Position;
			XMFLOAT3 target = light->LightData.Direction; 
			target.x += eye.x;
			target.y += eye.y;
			target.z += eye.z;

			XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&eye), XMLoadFloat3(&target), XMVectorSet(0.f, 1.f, 0.f, 0.f));
			const XMMATRIX proj = XMMatrixPerspectiveFovLH(0.25f * XM_PI, 1.f, 1.f, light->LightData.Radius * 2.f);
			lightConstants.LightMatrix = XMMatrixTranspose(view * proj);

			currShadowLightsCb->CopyData(light->LightIndex, lightConstants);
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
			//we are inside the light
			currLightsContainingFrustumCb->CopyData(lightsContainingFrustum++, LightIndex(light->LightIndex));
			_lightsContainingFrustum.push_back(light->LightIndex);
		}
		else
		{
			//the light is inside the frustum
			currLightsInsideFrustumCb->CopyData(lightsInsideFrustum++, LightIndex(light->LightIndex));
			_lightsInsideFrustum.push_back(light->LightIndex);
		}
	}
}

void LightingManager::UpdateWorld(const int lightIndex) const
{
	const Light data = _localLights[lightIndex]->LightData;
	XMMATRIX world;
	//if it is a sphere
	if (data.Type == 0)
	{
		const float diameter = data.Radius * 2.f;
		const XMMATRIX scale = XMMatrixScaling(diameter, diameter, diameter);
		const XMMATRIX translation = XMMatrixTranslationFromVector(XMLoadFloat3(&data.Position));

		world = scale * translation;
		_localLights[lightIndex]->Bounds = BoundingSphere(data.Position, data.Radius);
	}
	//or it is a cone
	else
	{
		const float diameter = 2.f * data.Radius * tanf(data.Angle);
		const XMMATRIX scale = XMMatrixScaling(diameter, diameter, data.Radius);
		const XMVECTOR dir = XMLoadFloat3(&data.Direction);
		if (XMVector3Less(XMVector3LengthSq(dir), XMVectorReplicate(1e-6f)))
		{
			return;
		}
		const XMVECTOR direction = XMVector3Normalize(dir);

		XMVECTOR position = XMLoadFloat3(&data.Position);
		position += direction * data.Radius * .5f;
		const XMMATRIX translation = XMMatrixTranslationFromVector(position);
		
		XMVECTOR up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
		if (fabs(XMVectorGetX(XMVector3Dot(direction, up))) > 0.999f)
		{
			up = XMVectorSet(0.f, 0.f, 1.f, 0.f);
		}

		const XMMATRIX rotation = XMMatrixTranspose(XMMatrixLookToLH(XMVectorZero(), direction, up));
		world = scale * rotation * translation;

		XMStoreFloat3(&_localLights[lightIndex]->Bounds.Center, position);
		_localLights[lightIndex]->Bounds.Radius = data.Radius * .5f;
	}
	_localLights[lightIndex]->LightData.World = XMMatrixTranspose(world);
	_localLights[lightIndex]->NumFramesDirty = gNumFrameResources;
}

int LightingManager::LightsCount() const
{
	return static_cast<int>(_localLights.size());
}

LightRenderItem* LightingManager::GetLight(const int i) const
{
	return _localLights[i].get();
}

void LightingManager::CalculateCascadesViewProjs()
{
	//get the center of a camera frustum
	XMFLOAT3 corners[8];
	_camera->CameraFrustum().GetCorners(corners);

	XMMATRIX camWorld = _camera->GetInvView(); // camera world matrix
	for (auto& corner : corners)
	{
		XMVECTOR worldPos = XMVector3Transform(XMLoadFloat3(&corner), camWorld);
		XMStoreFloat3(&corner, worldPos);
	}

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

	for (int i = 0; i < gCascadesCount; i++)
	{
		float zPad = 10.0f;
		float lightDistance = 100.0f;
		float splitNearRatio = static_cast<float>(i) / static_cast<float>(gCascadesCount);
		float splitFarRatio = static_cast<float>(i + 1) / static_cast<float>(gCascadesCount);

		_cascades[i].Cascade.SplitNear = nearZ + (farZ - nearZ) * splitNearRatio;
		_cascades[i].Cascade.SplitFar = nearZ + (farZ - nearZ) * splitFarRatio + zPad;

		//interpolate corners in world space
		XMVECTOR cascadeCorners[8];

		for (int j = 0; j < 4; j++) {

			cascadeCorners[j] = XMVectorLerp(frustumCorners[j], frustumCorners[j + 4], splitNearRatio);
			cascadeCorners[j + 4] = XMVectorLerp(frustumCorners[j], frustumCorners[j + 4], splitFarRatio);
		}

		// also make a different view matrix
		XMVECTOR cascadeCenter = XMVectorZero();
		for (auto& cascadeCorner : cascadeCorners)
			cascadeCenter += cascadeCorner;
		cascadeCenter /= 8.0f;

		XMVECTOR cascadeEye = cascadeCenter - lightDir * lightDistance;
		_cascades[i].LightView = XMMatrixLookAtLH(cascadeEye, cascadeCenter, up);

		//get them into light space
		XMVECTOR lsCorners[8];
		for (int j = 0; j < 8; j++)
			lsCorners[j] = XMVector3TransformCoord(cascadeCorners[j], _cascades[i].LightView);

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

		_cascades[i].Cascade.ViewProj = _cascades[i].LightView * cascadeProj;
		BoundingBox::CreateFromPoints(_cascades[i].Aabb, cascadeMinV, cascadeMaxV);
	}
}

void LightingManager::DrawDirLight(ID3D12GraphicsCommandList4* cmdList, const FrameResource* currFrameResource, const bool rayTracingEnabled)
{
	// Indicate a state transition on the resource usage.
	
	cmdList->SetGraphicsRootSignature(rayTracingEnabled ? _rootSignatureRt.Get() : _rootSignatureCsm.Get());

	const auto& srvAllocator = TextureManager::SrvHeapAllocator;

	cmdList->SetGraphicsRootDescriptorTable(0, _gbuffer->SrvGpuHandle());
	cmdList->SetGraphicsRootShaderResourceView(1, currFrameResource->LocalLightCb.get()->Resource()->GetGPUVirtualAddress());
	cmdList->SetGraphicsRootShaderResourceView(4, currFrameResource->LightsContainingFrustum.get()->Resource()->GetGPUVirtualAddress());
	cmdList->SetGraphicsRootDescriptorTable(5, _cubeMapManager->GetIblMapsGpuHandle());
	//set current depth
	cmdList->SetGraphicsRootDescriptorTable(6, _gbuffer->GetGBufferDepthSrv(true));
	
	const int shadowMaskSrvIndex = _shadowMasks.empty() ? 0 : _shadowMasks[_selectedShadowMask].index;
	cmdList->SetGraphicsRootDescriptorTable(7, srvAllocator->GetGpuHandle(shadowMaskSrvIndex));
	constexpr float shadowMaskIntensity = 0.0f;
	cmdList->SetGraphicsRoot32BitConstants(8, 1, _shadowMasks.empty() ? &shadowMaskIntensity : &ShadowMaskUvScale, 0);
	
	if (rayTracingEnabled)
	{
		cmdList->SetGraphicsRootDescriptorTable(9, srvAllocator->GetGpuHandle(_rayTracingManager->ShadowMaskSrv()));
	}
	else
	{
		cmdList->SetGraphicsRootDescriptorTable(9, srvAllocator->GetGpuHandle(_cascadeShadowTextureArray.Srv));
		cmdList->SetGraphicsRootDescriptorTable(10, srvAllocator->GetGpuHandle(_localLightsShadowTextureArray.Srv));
	}

	cmdList->SetPipelineState(rayTracingEnabled ? _dirLightPsoRt.Get() : _dirLightPsoCsm.Get());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	const auto lightingPassCb = currFrameResource->LightingPassCb->Resource();
	cmdList->SetGraphicsRootConstantBufferView(2, lightingPassCb->GetGPUVirtualAddress());

	const auto dirLightCb = currFrameResource->DirLightCb->Resource();
	cmdList->SetGraphicsRootConstantBufferView(3, dirLightCb->GetGPUVirtualAddress());

	ChangeMiddlewareState(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const auto& middlewareRtv = TextureManager::RtvHeapAllocator->GetCpuHandle(_middlewareTexture.OtherIndex);
	cmdList->ClearRenderTargetView(middlewareRtv, Colors::Black, 0, nullptr);
	const auto& dsv = _gbuffer->DepthStencilView();
	cmdList->OMSetRenderTargets(1, &middlewareRtv, true, &dsv);

	//draw directional light
	cmdList->DrawInstanced(3, 1, 0, 0);

	_finalTextureSrvIndex = _middlewareTexture.SrvIndex;
}

void LightingManager::DrawLocalLights(ID3D12GraphicsCommandList4* cmdList, const FrameResource* currFrameResource, const bool rayTracingEnabled) const
{
	if (_lightsInsideFrustum.empty())
		return;
	
	//draw local lights
	MeshGeometry* geo = GeometryManager::Geometries()["shapeGeo"].begin()->get();

	const auto& vertexBuffer = geo->VertexBufferView();
	cmdList->IASetVertexBuffers(0, 1, &vertexBuffer);
	const auto& indexBuffer = geo->IndexBufferView();
	cmdList->IASetIndexBuffer(&indexBuffer);

	cmdList->SetGraphicsRootShaderResourceView(4, currFrameResource->LightsInsideFrustum.get()->Resource()->GetGPUVirtualAddress());

	cmdList->SetPipelineState(rayTracingEnabled ? _localLightsPsoRt.Get() : _localLightsPsoCsm.Get());

	const SubmeshGeometry mesh = geo->DrawArgs["box"];
	cmdList->DrawIndexedInstanced(mesh.IndexCount, static_cast<UINT>(_lightsInsideFrustum.size()), mesh.StartIndexLocation,
		mesh.BaseVertexLocation, 0);
}

void LightingManager::DrawDebug(ID3D12GraphicsCommandList4* cmdList, FrameResource* currFrameResource) const
{
	if (_lightsInsideFrustum.empty())
		return;
	const auto geo = GeometryManager::Geometries()["shapeGeo"].begin()->get();
	const SubmeshGeometry mesh = geo->DrawArgs["box"];
	cmdList->SetPipelineState(_localLightsWireframePso.Get());
	cmdList->DrawIndexedInstanced(mesh.IndexCount, static_cast<UINT>(_lightsInsideFrustum.size()),
	                              mesh.StartIndexLocation, mesh.BaseVertexLocation, 0);
}

void LightingManager::DrawEmissive(ID3D12GraphicsCommandList4* cmdList, FrameResource* currFrameResource) const
{
	cmdList->SetPipelineState(_emissivePso.Get());

	cmdList->DrawInstanced(3, 1, 0, 0);
}

void LightingManager::DrawShadows(ID3D12GraphicsCommandList4* cmdList, FrameResource* currFrameResource,
                                  const std::vector<std::shared_ptr<EditableRenderItem>>& objects)
{
	cmdList->RSSetViewports(1, &_shadowViewport);
	cmdList->RSSetScissorRects(1, &_shadowScissorRect);

	cmdList->SetGraphicsRootSignature(_shadowRootSignature.Get());
	cmdList->SetPipelineState(_shadowPso.Get());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	if (_isMainLightOn)
	{
	
		const auto& barrier = CD3DX12_RESOURCE_BARRIER::Transition(_cascadeShadowTextureArray.TextureArray.Get(), D3D12_RESOURCE_STATE_GENERIC_READ,
                              			                         D3D12_RESOURCE_STATE_DEPTH_WRITE);
		//changing state of shadow map to dsv
		cmdList->ResourceBarrier(1, &barrier);

		const auto dsvAllocator = TextureManager::DsvHeapAllocator.get();

		//render each cascade shadow map
		for (UINT i = 0; i < gCascadesCount; i++)
		{
			CD3DX12_CPU_DESCRIPTOR_HANDLE tex(dsvAllocator->GetCpuHandle(_cascades[i].ShadowMapDsv));
			cmdList->ClearDepthStencilView(tex, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			//check frustum culling so we do not need to load gpu with unnecessary commands
			std::vector<int> visibleObjects = FrustumCulling(objects, i);
			if (visibleObjects.empty())
				continue;

			cmdList->OMSetRenderTargets(0, nullptr, false, &tex);
			cmdList->SetGraphicsRootConstantBufferView(
				1, currFrameResource->ShadowDirLightCb->Resource()->GetGPUVirtualAddress() + i * _shadowCbSize);

			ShadowPass(currFrameResource, cmdList, visibleObjects, objects);
		}
		//changing state back to srv
		const auto& barrier2 = CD3DX12_RESOURCE_BARRIER::Transition(_cascadeShadowTextureArray.TextureArray.Get(),
		                                                            D3D12_RESOURCE_STATE_DEPTH_WRITE,
		                                                            D3D12_RESOURCE_STATE_GENERIC_READ);
		cmdList->ResourceBarrier(1, &barrier2);
	}

	//the same for local lights
	std::vector<int> visibleLights = _lightsContainingFrustum;
	visibleLights.insert(visibleLights.end(), _lightsInsideFrustum.begin(), _lightsInsideFrustum.end());

	if (visibleLights.empty())
		return;
	
	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(_localLightsShadowTextureArray.TextureArray.Get(),
	                                                           D3D12_RESOURCE_STATE_GENERIC_READ,
	                                                           D3D12_RESOURCE_STATE_DEPTH_WRITE);

	cmdList->ResourceBarrier(1, &barrier);

	const auto dsvAllocator = TextureManager::DsvHeapAllocator.get();
	for (const auto& light : _localLights)
	{
		if (!light->LightData.Active || light->LightData.Type == 0)
			continue;

		if (std::find(visibleLights.begin(), visibleLights.end(), light->LightIndex) == visibleLights.end())
			continue;

		BoundingSphere lightWorldAabb;
		light->Bounds.Transform(lightWorldAabb, light->LightData.World);
		std::vector<int> visibleObjects = FrustumCulling(objects, lightWorldAabb);
		if (visibleObjects.empty())
			continue;

		CD3DX12_CPU_DESCRIPTOR_HANDLE tex(dsvAllocator->GetCpuHandle(light->ShadowMapDsv));
		cmdList->OMSetRenderTargets(0, nullptr, false, &tex);
		cmdList->ClearDepthStencilView(tex, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		const auto currShadowLightsCb = currFrameResource->ShadowLocalLightCb.get()->Resource();

		const D3D12_GPU_VIRTUAL_ADDRESS localLightCbAddress = currShadowLightsCb->GetGPUVirtualAddress() + light->
			LightIndex * sizeof(ShadowLightConstants);
		cmdList->SetGraphicsRootConstantBufferView(1, localLightCbAddress);

		ShadowPass(currFrameResource, cmdList, visibleObjects, objects);

	}
	
	barrier = CD3DX12_RESOURCE_BARRIER::Transition(_localLightsShadowTextureArray.TextureArray.Get(),
	                                                           D3D12_RESOURCE_STATE_DEPTH_WRITE,
	                                                           D3D12_RESOURCE_STATE_GENERIC_READ);
	cmdList->ResourceBarrier(1, &barrier);
}

void LightingManager::DrawIntoBackBuffer(ID3D12GraphicsCommandList4* cmdList, FrameResource* currFrameResource)
{
	//middleware to srv
	if (_finalTextureSrvIndex == _middlewareTexture.SrvIndex)
	{
		ChangeMiddlewareState(cmdList, D3D12_RESOURCE_STATE_GENERIC_READ);
	}
	cmdList->SetGraphicsRootSignature(_finalPassRootSignature.Get());
	cmdList->SetGraphicsRootDescriptorTable(0, TextureManager::SrvHeapAllocator->GetGpuHandle(_finalTextureSrvIndex));
	cmdList->SetPipelineState(_finalPassPso.Get());
	cmdList->DrawInstanced(3, 1, 0, 0);
}

void LightingManager::Init()
{
	BuildRootSignature();
	BuildShaders();
	BuildInputLayout();
	BuildPso();
	BuildDescriptors();
}

void LightingManager::BindToOtherData(
	GBuffer* gbuffer,
	CubeMapManager* cubeMapManager,
	Camera* camera,
	const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputLayout,
	RayTracingManager* rayTracingManager)
{
	_gbuffer = gbuffer;
	_cubeMapManager = cubeMapManager;
	_camera = camera;
	_shadowInputLayout = inputLayout;
	_rayTracingManager = rayTracingManager;
}

void LightingManager::OnResize(const UINT newWidth, const UINT newHeight)
{
	_width = newWidth;
	_height = newHeight;
	CreateMiddlewareTexture();
}

ID3DBlob* LightingManager::GetFullScreenVsWithSamplers() const
{
	return _dirLightVsShader.Get();
}

void LightingManager::ChangeMiddlewareState(ID3D12GraphicsCommandList4* cmdList, const D3D12_RESOURCE_STATES newState)
{
	if (_middlewareTexture.PrevState == newState)
		return;
	const auto& barrier = CD3DX12_RESOURCE_BARRIER::Transition(_middlewareTexture.Resource.Get(), _middlewareTexture.PrevState, newState);
	cmdList->ResourceBarrier(1, &barrier);
	_middlewareTexture.PrevState = newState;
}

void LightingManager::AddShadowMask(const TextureHandle& handle)
{
	_selectedShadowMask = _shadowMasks.size();
	_shadowMasks.push_back(handle);
}

void LightingManager::DeleteShadowMask(const size_t i)
{
	const std::string texName = _shadowMasks[i].Name;
	TextureManager::DeleteTexture(std::wstring(texName.begin(), texName.end()), 1);
	_shadowMasks.erase(_shadowMasks.begin() + i);
	if (_selectedShadowMask >= _shadowMasks.size())
	{
		_selectedShadowMask = _shadowMasks.size() - 1;
	}
}

void LightingManager::BuildInputLayout()
{
	_localLightsInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
}

void LightingManager::BuildRootSignature()
{
#pragma region CSMRootSignature
	const int srvAmount = GBuffer::InfoCount();
	int srvIndex = srvAmount;
	//lighting root signature
	CD3DX12_DESCRIPTOR_RANGE lightingRange;
	lightingRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvIndex, 0);
	//depth is separate
	CD3DX12_DESCRIPTOR_RANGE depthTexTable;
	depthTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, srvIndex++);
	//shadow mask
	CD3DX12_DESCRIPTOR_RANGE shadowMaskTexTable;
	shadowMaskTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, srvIndex++);

	const int afterShadowMaskIndex = srvIndex;
	//cascades shadow map
	CD3DX12_DESCRIPTOR_RANGE cascadesShadowTexTable;
	cascadesShadowTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, srvIndex++);
	//shadow map
	CD3DX12_DESCRIPTOR_RANGE shadowTexTable;
	shadowTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, srvIndex++);
	//sky
	CD3DX12_DESCRIPTOR_RANGE skyTexTable;
	skyTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 2);

	constexpr int rootParameterCountCsm = 11;

	CD3DX12_ROOT_PARAMETER lightingSlotRootParameterCsm[rootParameterCountCsm];

	lightingSlotRootParameterCsm[0].InitAsDescriptorTable(1, &lightingRange);
	lightingSlotRootParameterCsm[1].InitAsShaderResourceView(0, 1);
	lightingSlotRootParameterCsm[2].InitAsConstantBufferView(0);
	lightingSlotRootParameterCsm[3].InitAsConstantBufferView(1);
	lightingSlotRootParameterCsm[4].InitAsShaderResourceView(1, 1);
	lightingSlotRootParameterCsm[5].InitAsDescriptorTable(1, &skyTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	lightingSlotRootParameterCsm[6].InitAsDescriptorTable(1, &depthTexTable);
	lightingSlotRootParameterCsm[7].InitAsDescriptorTable(1, &shadowMaskTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	lightingSlotRootParameterCsm[8].InitAsConstants(1, 2);
	lightingSlotRootParameterCsm[9].InitAsDescriptorTable(1, &cascadesShadowTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	lightingSlotRootParameterCsm[10].InitAsDescriptorTable(1, &shadowTexTable, D3D12_SHADER_VISIBILITY_PIXEL);

	CD3DX12_ROOT_SIGNATURE_DESC lightingRootSigDesc(rootParameterCountCsm, lightingSlotRootParameterCsm,
	                                                static_cast<UINT>(TextureManager::GetLinearSamplers().size()),
	                                                TextureManager::GetLinearSamplers().data(),
	                                                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&lightingRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(_device->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(_rootSignatureCsm.GetAddressOf())));
		
#pragma endregion

#pragma region RTRootSignature
	if (_rayTracingSupported)
	{
		CD3DX12_DESCRIPTOR_RANGE rtShadowTexTable;
		rtShadowTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, afterShadowMaskIndex);
        	
        constexpr int rtRootParameterCount = 10;
    
        CD3DX12_ROOT_PARAMETER lightingSlotRootParameterRt[rtRootParameterCount];
    
        lightingSlotRootParameterRt[0].InitAsDescriptorTable(1, &lightingRange);
        lightingSlotRootParameterRt[1].InitAsShaderResourceView(0, 1);
        lightingSlotRootParameterRt[2].InitAsConstantBufferView(0);
        lightingSlotRootParameterRt[3].InitAsConstantBufferView(1);
        lightingSlotRootParameterRt[4].InitAsShaderResourceView(1, 1);
        lightingSlotRootParameterRt[5].InitAsDescriptorTable(1, &skyTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
        lightingSlotRootParameterRt[6].InitAsDescriptorTable(1, &depthTexTable);
		lightingSlotRootParameterRt[7].InitAsDescriptorTable(1, &shadowMaskTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
		lightingSlotRootParameterRt[8].InitAsConstants(1, 2);
		lightingSlotRootParameterRt[9].InitAsDescriptorTable(1, &rtShadowTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
    
        CD3DX12_ROOT_SIGNATURE_DESC lightingRootSigDescRt(rtRootParameterCount, lightingSlotRootParameterRt,
                                                        static_cast<UINT>(TextureManager::GetLinearSamplers().size()),
                                                        TextureManager::GetLinearSamplers().data(),
                                                        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    
        serializedRootSig = nullptr;
        errorBlob = nullptr;
        hr = D3D12SerializeRootSignature(&lightingRootSigDescRt, D3D_ROOT_SIGNATURE_VERSION_1,
        	serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());
    
        if (errorBlob != nullptr)
        {
        	::OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
        }
        ThrowIfFailed(hr);
    
        ThrowIfFailed(_device->CreateRootSignature(
        	0,
        	serializedRootSig->GetBufferPointer(),
        	serializedRootSig->GetBufferSize(),
        	IID_PPV_ARGS(_rootSignatureRt.GetAddressOf())));
	}
#pragma endregion

#pragma region ShadowRootSignature
	//shadow root signature
	CD3DX12_ROOT_PARAMETER shadowSlotRootParameter[2];
	shadowSlotRootParameter[0].InitAsConstantBufferView(0);
	shadowSlotRootParameter[1].InitAsConstantBufferView(1);
	CD3DX12_ROOT_SIGNATURE_DESC shadowRootSigDesc(2, shadowSlotRootParameter, 0, nullptr,
	                                              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	ComPtr<ID3DBlob> shadowSerializedRootSig = nullptr;
	ComPtr<ID3DBlob> shadowErrorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&shadowRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		shadowSerializedRootSig.GetAddressOf(), shadowErrorBlob.GetAddressOf());
	if (shadowErrorBlob != nullptr)
	{
		::OutputDebugStringA(static_cast<char*>(shadowErrorBlob->GetBufferPointer()));
	}
	ThrowIfFailed(hr);
	ThrowIfFailed(_device->CreateRootSignature(
		0,
		shadowSerializedRootSig->GetBufferPointer(),
		shadowSerializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(_shadowRootSignature.GetAddressOf())));
#pragma endregion

#pragma region FinalPassRootSignature
	//final pass root signature
	CD3DX12_DESCRIPTOR_RANGE middlewareTexRange;
	middlewareTexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_ROOT_PARAMETER finalPassSlotRootParameter[1];

	finalPassSlotRootParameter[0].InitAsDescriptorTable(1, &middlewareTexRange);

	CD3DX12_ROOT_SIGNATURE_DESC finalRootSigDesc(1, finalPassSlotRootParameter, 0, nullptr,
	                                             D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	serializedRootSig = nullptr;
	errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&finalRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(_device->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(_finalPassRootSignature.GetAddressOf())));
		
#pragma endregion
}

void LightingManager::BuildShaders()
{
	constexpr D3D_SHADER_MACRO defines[] =
    {
    	"CSM", "1",
    	nullptr, nullptr
    };
	
	_dirLightVsShader = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "DirLightingVS", "vs_5_1");
	_dirLightPsShaderCsm = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", defines, "DirLightingPS", "ps_5_1");
	if (_rayTracingSupported)
		_dirLightPsShaderRt = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "DirLightingPS", "ps_5_1");
	
	_localLightsVsShader = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "LocalLightingVS", "vs_5_1");
	_localLightsPsShaderCsm = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", defines, "LocalLightingPS", "ps_5_1");
	_localLightsWireframePsShader = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "LocalLightingWireframePS", "ps_5_1");
	if (_rayTracingSupported)
		_localLightsPsShaderRt = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "LocalLightingPS", "ps_5_1");
	
	_emissivePsShader = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "EmissivePS", "ps_5_1");

	_shadowVsShader = d3dUtil::CompileShader(L"Shaders\\ShadowPass.hlsl", nullptr, "ShadowVS", "vs_5_1");

//TODO: make a different manager for that
	_finalPassVsShader = d3dUtil::CompileShader(L"Shaders\\MiddlewareToBackBuffer.hlsl", nullptr, "VS", "vs_5_1");
	_finalPassPsShader = d3dUtil::CompileShader(L"Shaders\\MiddlewareToBackBuffer.hlsl", nullptr, "PS", "ps_5_1");
}

void LightingManager::BuildPso()
{
#pragma region CSMDirLightPSO
	//directional light pso
	D3D12_GRAPHICS_PIPELINE_STATE_DESC lightingPsoDesc;

	ZeroMemory(&lightingPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	lightingPsoDesc.pRootSignature = _rootSignatureCsm.Get();
	lightingPsoDesc.VS =
	{
		static_cast<BYTE*>(_dirLightVsShader->GetBufferPointer()),
		_dirLightVsShader->GetBufferSize()
	};
	lightingPsoDesc.PS =
	{
		static_cast<BYTE*>(_dirLightPsShaderCsm->GetBufferPointer()),
		_dirLightPsShaderCsm->GetBufferSize()
	};
	lightingPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	lightingPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	lightingPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	lightingPsoDesc.SampleMask = UINT_MAX;
	lightingPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	lightingPsoDesc.NumRenderTargets = 1;
	lightingPsoDesc.RTVFormats[0] = _middlewareTextureFormat;
	lightingPsoDesc.DepthStencilState.DepthEnable = false;
	lightingPsoDesc.DepthStencilState.StencilEnable = false;
	lightingPsoDesc.SampleDesc.Count = 1;
	lightingPsoDesc.SampleDesc.Quality = 0;
	lightingPsoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&lightingPsoDesc, IID_PPV_ARGS(&_dirLightPsoCsm)));
	
#pragma endregion
	
#pragma region CSMLocalLightsPSO

	//local lights pso
	D3D12_GRAPHICS_PIPELINE_STATE_DESC localLightsPsoDesc = lightingPsoDesc;
	localLightsPsoDesc.InputLayout = { _localLightsInputLayout.data(), static_cast<UINT>(_localLightsInputLayout.size()) };

	localLightsPsoDesc.VS =
	{
		static_cast<BYTE*>(_localLightsVsShader->GetBufferPointer()),
		_localLightsVsShader->GetBufferSize()
	};
	localLightsPsoDesc.PS =
	{
		static_cast<BYTE*>(_localLightsPsShaderCsm->GetBufferPointer()),
		_localLightsPsShaderCsm->GetBufferSize()
	};

	localLightsPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	localLightsPsoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	localLightsPsoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	localLightsPsoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	localLightsPsoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	localLightsPsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	localLightsPsoDesc.DepthStencilState.DepthEnable = TRUE;
	localLightsPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	localLightsPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	ThrowIfFailed(_device->CreateGraphicsPipelineState(&localLightsPsoDesc, IID_PPV_ARGS(&_localLightsPsoCsm)));
#pragma endregion

#pragma region WireFramePSO

	//wireframe pso
	D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframePso = localLightsPsoDesc;
	wireframePso.PS =
	{
		static_cast<BYTE*>(_localLightsWireframePsShader->GetBufferPointer()),
		_localLightsWireframePsShader->GetBufferSize()
	};
	wireframePso.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;

	ThrowIfFailed(_device->CreateGraphicsPipelineState(&wireframePso, IID_PPV_ARGS(&_localLightsWireframePso)));
	
#pragma endregion
	
#pragma region EmissivePSO

	//emissive pso
	D3D12_GRAPHICS_PIPELINE_STATE_DESC emissivePsoDesc = lightingPsoDesc;
	emissivePsoDesc.PS =
	{
		static_cast<BYTE*>(_emissivePsShader->GetBufferPointer()),
		_emissivePsShader->GetBufferSize()
	};

	emissivePsoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	emissivePsoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	emissivePsoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	emissivePsoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	emissivePsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	ThrowIfFailed(_device->CreateGraphicsPipelineState(&emissivePsoDesc, IID_PPV_ARGS(&_emissivePso)));
#pragma endregion

#pragma region ShadowPSO
	//shadow pso
	D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc;
	ZeroMemory(&shadowPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	shadowPsoDesc.InputLayout = { _shadowInputLayout.data(), static_cast<UINT>(_shadowInputLayout.size()) };
	shadowPsoDesc.pRootSignature = _shadowRootSignature.Get();
	shadowPsoDesc.VS =
	{
		static_cast<BYTE*>(_shadowVsShader->GetBufferPointer()),
		_shadowVsShader->GetBufferSize()
	};

	//rasterizer state with biases
	D3D12_RASTERIZER_DESC shadowRasterDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

	//useless thing for now
	//todo: make them parameters so I can finally know what they should be
	/*shadowRasterDesc.DepthBias = 100;
	shadowRasterDesc.DepthBiasClamp = 10.0f;
	*/

	shadowRasterDesc.SlopeScaledDepthBias = 1.5f;

	shadowPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	shadowPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	shadowPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	shadowPsoDesc.SampleMask = UINT_MAX;
	shadowPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	shadowPsoDesc.NumRenderTargets = 0;
	shadowPsoDesc.SampleDesc.Count = 1;
	shadowPsoDesc.SampleDesc.Quality = 0;
	shadowPsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&shadowPsoDesc, IID_PPV_ARGS(&_shadowPso)));

#pragma endregion
#pragma region FinalPassPSO

	//final pass
	D3D12_GRAPHICS_PIPELINE_STATE_DESC finalPsoDesc = lightingPsoDesc;

	finalPsoDesc.pRootSignature = _finalPassRootSignature.Get();
	finalPsoDesc.VS =
	{
		static_cast<BYTE*>(_finalPassVsShader->GetBufferPointer()),
		_finalPassVsShader->GetBufferSize()
	};
	finalPsoDesc.PS =
	{
		static_cast<BYTE*>(_finalPassPsShader->GetBufferPointer()),
		_finalPassPsShader->GetBufferSize()
	};
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&finalPsoDesc, IID_PPV_ARGS(&_finalPassPso)));
#pragma endregion

	if (_rayTracingSupported)
	{
#pragma region RTDirLightPSO
		D3D12_GRAPHICS_PIPELINE_STATE_DESC rtDirPsoDesc = lightingPsoDesc;
		rtDirPsoDesc.PS =
		{
			static_cast<BYTE*>(_dirLightPsShaderRt->GetBufferPointer()),
			_dirLightPsShaderRt->GetBufferSize()
		};
		rtDirPsoDesc.pRootSignature = _rootSignatureRt.Get();
		ThrowIfFailed(_device->CreateGraphicsPipelineState(&rtDirPsoDesc, IID_PPV_ARGS(&_dirLightPsoRt)));

#pragma endregion
#pragma region RTLocalLightsPSO
		D3D12_GRAPHICS_PIPELINE_STATE_DESC rtLocalLightsPsoDesc = localLightsPsoDesc;
		rtLocalLightsPsoDesc.PS =
		{
			static_cast<BYTE*>(_localLightsPsShaderRt->GetBufferPointer()),
			_localLightsPsShaderRt->GetBufferSize()
		};
		rtLocalLightsPsoDesc.pRootSignature = _rootSignatureRt.Get();
		ThrowIfFailed(_device->CreateGraphicsPipelineState(&rtLocalLightsPsoDesc, IID_PPV_ARGS(&_localLightsPsoRt)));
#pragma endregion
		
	}
}

void LightingManager::BuildDescriptors()
{
	//creating shadow textures arrays
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = static_cast<UINT>(_shadowMapResolution);
	texDesc.Height = static_cast<UINT>(_shadowMapResolution);
	texDesc.DepthOrArraySize = static_cast<UINT16>(gCascadesCount); // first cascades
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
		&clearValue, IID_PPV_ARGS(&_cascadeShadowTextureArray.TextureArray)));

	texDesc.DepthOrArraySize = static_cast<UINT16>(_maxLights); // then local lights
	ThrowIfFailed(_device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
		&clearValue, IID_PPV_ARGS(&_localLightsShadowTextureArray.TextureArray)
	));

	//create SRVs for texture arrays
	auto& allocator = TextureManager::SrvHeapAllocator;
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
	_device->CreateShaderResourceView(_cascadeShadowTextureArray.TextureArray.Get(), &srvDesc, srvHandle);
	_cascadeShadowTextureArray.Srv = srvIndex;

	srvDesc.Texture2DArray.ArraySize = _maxLights;
	srvIndex = allocator->Allocate();
	srvHandle = allocator->GetCpuHandle(srvIndex);
	_device->CreateShaderResourceView(_localLightsShadowTextureArray.TextureArray.Get(), &srvDesc, srvHandle);
	_localLightsShadowTextureArray.Srv = srvIndex;

	//make dsvs for cascades
	for (int i = 0; i < gCascadesCount; i++)
		_cascades[i].ShadowMapDsv = CreateShadowTextureDsv(true, i);

	//rects for shadows should be different
	_shadowViewport.Height = _shadowMapResolution;
	_shadowViewport.Width = _shadowMapResolution;
	_shadowScissorRect.right = static_cast<LONG>(_shadowMapResolution);
	_shadowScissorRect.bottom = static_cast<LONG>(_shadowMapResolution);

	//allocating indices for middleware texture once
	_middlewareTexture.SrvIndex = TextureManager::SrvHeapAllocator->Allocate();
	_middlewareTexture.OtherIndex = TextureManager::RtvHeapAllocator->Allocate();
	//but remaking it every time we resize
	CreateMiddlewareTexture();
}

int LightingManager::CreateShadowTextureDsv(const bool forCascade, const int index) const
{
	ID3D12Resource* textureArray = forCascade ? _cascadeShadowTextureArray.TextureArray.Get() : _localLightsShadowTextureArray.TextureArray.Get();

	//dsv
	const UINT dsvIndex = TextureManager::DsvHeapAllocator->Allocate();
	const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = TextureManager::DsvHeapAllocator->GetCpuHandle(dsvIndex);

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

void LightingManager::DeleteShadowTexture(const int texDsv)
{
	UploadManager::Flush();
	TextureManager::DsvHeapAllocator->Free(texDsv);
}

std::vector<int> LightingManager::FrustumCulling(const std::vector<std::shared_ptr<EditableRenderItem>>& objects, const int cascadeIdx) const
{
	const BoundingBox lightAabb = _cascades[cascadeIdx].Aabb;
	std::vector<int> visibleObjects;

	for (int i = 0; i < objects.size(); i++)
	{
		BoundingBox objectLsBounds;
		BoundingBox objectWorldBounds;

		objects[i]->Bounds.Transform(objectLsBounds, objects[i]->World * _cascades[cascadeIdx].LightView);
		objects[i]->Bounds.Transform(objectWorldBounds, objects[i]->World);
		float distance;
		XMStoreFloat(&distance, XMVector3Length(XMLoadFloat3(&objectWorldBounds.Center) - _camera->GetPosition()));
		if (lightAabb.Contains(objectLsBounds) != DirectX::DISJOINT)
		{
			visibleObjects.push_back(i);
		}
	}

	return visibleObjects;
}

std::vector<int> LightingManager::FrustumCulling(const std::vector<std::shared_ptr<EditableRenderItem>>& objects, const DirectX::BoundingSphere lightAabb)
{
	std::vector<int> visibleObjects;

	for (int i = 0; i < objects.size(); i++)
	{
		BoundingBox worldBounds;

		objects[i]->Bounds.Transform(worldBounds, objects[i]->World);
		if (lightAabb.Contains(worldBounds) != DirectX::DISJOINT)
		{
			visibleObjects.push_back(i);
		}
	}

	return visibleObjects;
}

void LightingManager::ShadowPass(FrameResource* currFrameResource, ID3D12GraphicsCommandList4* cmdList,
                                 const std::vector<int>& visibleObjects,
                                 const std::vector<std::shared_ptr<EditableRenderItem>>& objects)
{
	for (auto& idx : visibleObjects)
	{
		auto& ri = *objects[idx];
		const auto objectCb = currFrameResource->OpaqueObjCb[ri.Uid]->Resource();
		const int curLodIdx = ri.CurrentLodIdx;

		const MeshGeometry* curLodGeo = ri.Geo->at(curLodIdx).get();
		const auto& vertexBuffer = curLodGeo->VertexBufferView();
		cmdList->IASetVertexBuffers(0, 1, &vertexBuffer);
		const auto& indexBuffer = curLodGeo->IndexBufferView();
		cmdList->IASetIndexBuffer(&indexBuffer);

		auto currentLod = ri.LodsData[curLodIdx];
		for (size_t i = 0; i < currentLod.Meshes.size(); i++)
		{
			const auto& meshData = currentLod.Meshes.at(i);
			const D3D12_GPU_VIRTUAL_ADDRESS meshCbAddress = objectCb->GetGPUVirtualAddress() + meshData.CbOffset;
			cmdList->SetGraphicsRootConstantBufferView(0, meshCbAddress);
			cmdList->DrawIndexedInstanced(static_cast<UINT>(meshData.IndexCount), 1, static_cast<UINT>(meshData.IndexStart),
			                              static_cast<UINT>(meshData.VertexStart), 0);
		}
	}
}

void LightingManager::SnapToTexel(DirectX::XMFLOAT3& minPt, DirectX::XMFLOAT3& maxPt) const
{
	const float worldUnitsPerTexelX = (maxPt.x - minPt.x) / static_cast<float>(_shadowMapResolution);
	const float worldUnitsPerTexelY = (maxPt.y - minPt.y) / static_cast<float>(_shadowMapResolution);

	// compute center in light space
	float cx = (minPt.x + maxPt.x) * 0.5f;
	float cy = (minPt.y + maxPt.y) * 0.5f;

	// snap center to texel grid:
	cx = floorf(cx / worldUnitsPerTexelX) * worldUnitsPerTexelX;
	cy = floorf(cy / worldUnitsPerTexelY) * worldUnitsPerTexelY;

	// recompute min/max from snapped center
	minPt.x = cx - (maxPt.x - minPt.x) * 0.5f;
	maxPt.x = cx + (maxPt.x - minPt.x) * 0.5f;
	minPt.y = cy - (maxPt.y - minPt.y) * 0.5f;
	maxPt.y = cy + (maxPt.y - minPt.y) * 0.5f;
}

void LightingManager::CreateMiddlewareTexture()
{
	//middleware texture
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = _width;
	texDesc.Height = _height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = _middlewareTextureFormat;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = _middlewareTextureFormat;
	clearValue.Color[0] = 0.f;
	clearValue.Color[1] = 0.f;
	clearValue.Color[2] = 0.f;
	clearValue.Color[3] = 1.f;

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

	ThrowIfFailed(_device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
		&clearValue, IID_PPV_ARGS(&_middlewareTexture.Resource)));

	//create SRV
	auto& srvAllocator = TextureManager::SrvHeapAllocator;
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = srvAllocator->GetCpuHandle(_middlewareTexture.SrvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = _middlewareTextureFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	_device->CreateShaderResourceView(_middlewareTexture.Resource.Get(), &srvDesc, srvHandle);

	//create RTV
	auto& rtvAllocator = TextureManager::RtvHeapAllocator;
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvAllocator->GetCpuHandle(_middlewareTexture.OtherIndex);

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = _middlewareTextureFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;
	_device->CreateRenderTargetView(_middlewareTexture.Resource.Get(), &rtvDesc, rtvHandle);
}

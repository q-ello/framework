#include "OpaqueObjectManager.h"
#include "UploadManager.h"
#include "../../../Common/GBuffer.h"

void EditableObjectManager::UpdateObjectCBs(FrameResource* currFrameResource)
{
	auto& currObjectsCb = currFrameResource->OpaqueObjCb;
	auto& currMaterialCb = currFrameResource->MaterialCb;

	//for frustum culling
	_visibleTesselatedObjects.clear();
	_visibleUntesselatedObjects.clear();

	XMMATRIX view = _camera->GetView();
	XMMATRIX invView = XMMatrixInverse(nullptr, view);

	for (int i = 0; i < _objects.size(); i++)
	{
		auto& ri = _objects[i];
		XMMATRIX scale = XMMatrixScalingFromVector(XMLoadFloat3(&ri->Transform[BasicUtil::EnumIndex(Transform::Scale)]));
		XMMATRIX rotation = XMMatrixRotationRollPitchYawFromVector(XMLoadFloat3(&ri->Transform[BasicUtil::EnumIndex(Transform::Rotation)]) * XM_PI / 180.f);
		XMMATRIX translation = XMMatrixTranslationFromVector(XMLoadFloat3(&ri->Transform[BasicUtil::EnumIndex(Transform::Translation)]));

		XMMATRIX world = scale * rotation * translation;
		ri->PrevWorld = ri->World;
		ri->World = world;
		
		XMFLOAT4X4 worldF;
		XMFLOAT4X4 prevWorldF;
		
		XMStoreFloat4x4(&worldF, ri->World);
		XMStoreFloat4x4(&prevWorldF, ri->PrevWorld);
		
		if (worldF.m != prevWorldF.m)
		{
			ri->NumFramesDirty = gNumFrameResources;
		}
		
		//updating object data
		if (ri->NumFramesDirty > 0)
		{
			size_t j = 0;
			for (auto currentLod = ri->LodsData[ri->CurrentLodIdx]; j < currentLod.Meshes.size(); j++)
			{
				XMMATRIX meshWorld = currentLod.Meshes.at(j).DefaultWorld * world;
				XMMATRIX prevMeshWorld = currentLod.Meshes.at(j).DefaultWorld * ri->PrevWorld;
				
				OpaqueObjectConstants objConstants;
				XMStoreFloat4x4(&objConstants.PrevWorld, XMMatrixTranspose(prevMeshWorld));
				XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(meshWorld));
				XMStoreFloat4x4(&objConstants.WorldInvTranspose, XMMatrixInverse(nullptr, meshWorld));

				currObjectsCb[ri->Uid].get()->CopyData(static_cast<int>(j), objConstants);
			}

			//last piece for AABB
			{
				XMMATRIX scaleAabb = XMMatrixScaling(ri->Bounds.Extents.x * 2.0f, ri->Bounds.Extents.y * 2.0f, ri->Bounds.Extents.z * 2.0f);
				XMMATRIX translationAabb = XMMatrixTranslation(ri->Bounds.Center.x, ri->Bounds.Center.y, ri->Bounds.Center.z);
				XMMATRIX aabbWorld = scaleAabb * translationAabb * world;

				OpaqueObjectConstants objConstants;
				XMStoreFloat4x4(&objConstants.World, DirectX::XMMatrixTranspose(aabbWorld));
				XMStoreFloat4x4(&objConstants.WorldInvTranspose, XMMatrixInverse(nullptr, aabbWorld));

				currObjectsCb[ri->Uid].get()->CopyData(static_cast<int>(j), objConstants);
			}

			ri->NumFramesDirty--;
		}

		for (size_t j = 0; j < ri->Materials.size(); j++)
		{
			Material* material = ri->Materials[j].get();
			if (material->numFramesDirty > 0)
			{
				MaterialConstants materialConstants;
				materialConstants.BaseColor = material->properties[BasicUtil::EnumIndex(MatProp::BaseColor)].value;
				materialConstants.DisplacementScale = material->additionalInfo[BasicUtil::EnumIndex(MatAddInfo::Displacement)];
				materialConstants.Emissive = material->properties[BasicUtil::EnumIndex(MatProp::Emissive)].value;
				materialConstants.EmissiveIntensity = material->additionalInfo[BasicUtil::EnumIndex(MatAddInfo::Emissive)];
				materialConstants.Metallic = material->properties[BasicUtil::EnumIndex(MatProp::Metallic)].value.x;
				materialConstants.Opacity = material->properties[BasicUtil::EnumIndex(MatProp::Opacity)].value.x;
				materialConstants.Roughness = material->properties[BasicUtil::EnumIndex(MatProp::Roughness)].value.x;
				materialConstants.UseAoMap = material->textures[BasicUtil::EnumIndex(MatTex::AmbOcc)].UseTexture;
				materialConstants.UseBaseColorMap = material->properties[BasicUtil::EnumIndex(MatProp::BaseColor)].texture.UseTexture;
				materialConstants.UseDisplacementMap = material->textures[BasicUtil::EnumIndex(MatTex::Displacement)].UseTexture;
				materialConstants.UseEmissiveMap = material->properties[BasicUtil::EnumIndex(MatProp::Emissive)].texture.UseTexture;
				materialConstants.UseMetallicMap = material->properties[BasicUtil::EnumIndex(MatProp::Metallic)].texture.UseTexture;
				materialConstants.UseNormalMap = material->textures[BasicUtil::EnumIndex(MatTex::Normal)].UseTexture;
				materialConstants.UseOpacityMap = material->properties[BasicUtil::EnumIndex(MatProp::Opacity)].texture.UseTexture;
				materialConstants.UseRoughnessMap = material->properties[BasicUtil::EnumIndex(MatProp::Roughness)].texture.UseTexture;
				materialConstants.UseArmMap = material->textures[BasicUtil::EnumIndex(MatTex::ARM)].UseTexture && material->useARMTexture;
				materialConstants.ArmLayout = static_cast<int>(material->armLayout);

				currMaterialCb[ri->Uid].get()->CopyData(static_cast<int>(j), materialConstants);

				material->numFramesDirty--;
			}
		}

		//frustum culling
		XMMATRIX invWorld = XMMatrixInverse(nullptr, world);

		XMMATRIX viewToLocal = XMMatrixMultiply(invView, invWorld);

		BoundingFrustum localSpaceFrustum;
		_camera->CameraFrustum().Transform(localSpaceFrustum, viewToLocal);
		if ((localSpaceFrustum.Contains(ri->Bounds) != DirectX::DISJOINT))
		{
			(_tesselatedObjects.find(ri->Uid) != _tesselatedObjects.end() ? _visibleTesselatedObjects : _visibleUntesselatedObjects).push_back(ri->Uid);
		}
	}
}

void EditableObjectManager::SetCamera(Camera* camera)
{
	_camera = camera;
}

void EditableObjectManager::BuildInputLayout()
{
	_inputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{ "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
}

void EditableObjectManager::BuildRootSignature()
{
	//first gbuffer root signature
	//two tables for 
	CD3DX12_DESCRIPTOR_RANGE baseColorTexTable;
	baseColorTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	CD3DX12_DESCRIPTOR_RANGE emissTexTable;
	emissTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
	CD3DX12_DESCRIPTOR_RANGE roughTexTable;
	roughTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
	CD3DX12_DESCRIPTOR_RANGE metallTexTable;
	metallTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
	CD3DX12_DESCRIPTOR_RANGE normTexTable;
	normTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);
	CD3DX12_DESCRIPTOR_RANGE aoTexTable;
	aoTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5);
	CD3DX12_DESCRIPTOR_RANGE dispTexTable;
	dispTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6);
	CD3DX12_DESCRIPTOR_RANGE armTexTable;
	armTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7);

	constexpr int rootParamCount = 11;

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[rootParamCount];

	// Performance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &baseColorTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsDescriptorTable(1, &emissTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[2].InitAsDescriptorTable(1, &roughTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[3].InitAsDescriptorTable(1, &metallTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[4].InitAsDescriptorTable(1, &normTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[5].InitAsDescriptorTable(1, &aoTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[6].InitAsDescriptorTable(1, &dispTexTable, D3D12_SHADER_VISIBILITY_DOMAIN);
	slotRootParameter[7].InitAsDescriptorTable(1, &armTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[8].InitAsConstantBufferView(0);
	slotRootParameter[9].InitAsConstantBufferView(1);
	slotRootParameter[10].InitAsConstantBufferView(2);

	const auto staticSamplers = TextureManager::GetStaticSamplers();

	// A root signature is an array of root parameters.
	const CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(rootParamCount, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	const HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
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

void EditableObjectManager::BuildPso()
{
	//for untesselated objects
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.InputLayout = { _inputLayout.data(), static_cast<UINT>(_inputLayout.size()) };
	psoDesc.pRootSignature = _rootSignature.Get();
	psoDesc.VS =
	{
		static_cast<BYTE*>(_vsShader->GetBufferPointer()),
		_vsShader->GetBufferSize()
	};
	psoDesc.PS =
	{
		static_cast<BYTE*>(_psShader->GetBufferPointer()),
		_psShader->GetBufferSize()
	};
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = GBuffer::InfoCount();
	for (int i = 0; i < GBuffer::InfoCount(); i++)
	{
		psoDesc.RTVFormats[i] = GBuffer::infoFormats[i];
	}
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pso)));

	//wireframe untesselated
	D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframePsoDesc = psoDesc;
	wireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	wireframePsoDesc.PS =
	{
		static_cast<BYTE*>(_wireframePsShader->GetBufferPointer()),
		_wireframePsShader->GetBufferSize()
	};
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&wireframePsoDesc, IID_PPV_ARGS(&_wireframePso)));

	//tesselated
	D3D12_GRAPHICS_PIPELINE_STATE_DESC tesselatedPsoDesc = psoDesc;
	tesselatedPsoDesc.VS =
	{
		static_cast<BYTE*>(_tessVsShader->GetBufferPointer()),
		_tessVsShader->GetBufferSize()
	};
	tesselatedPsoDesc.HS =
	{
		static_cast<BYTE*>(_tessHsShader->GetBufferPointer()),
		_tessHsShader->GetBufferSize()
	};
	tesselatedPsoDesc.DS =
	{
		static_cast<BYTE*>(_tessDsShader->GetBufferPointer()),
		_tessDsShader->GetBufferSize()
	};
	tesselatedPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&tesselatedPsoDesc, IID_PPV_ARGS(&_tesselatedPso)));

	//wireframe tesselated
	D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframeTesselatedPsoDesc = tesselatedPsoDesc;
	wireframeTesselatedPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	wireframeTesselatedPsoDesc.PS =
	{
		static_cast<BYTE*>(_wireframePsShader->GetBufferPointer()),
		_wireframePsShader->GetBufferSize()
	};

	ThrowIfFailed(_device->CreateGraphicsPipelineState(&wireframeTesselatedPsoDesc, IID_PPV_ARGS(&_wireframeTesselatedPso)));
}

void EditableObjectManager::BuildShaders()
{
	_vsShader = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "GBufferVS", "vs_5_1");
	_psShader = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "GBufferPS", "ps_5_1");
	_tessVsShader = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "TessVS", "vs_5_1");
	_tessHsShader = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "TessHS", "hs_5_1");
	_tessDsShader = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "TessDS", "ds_5_1");
	_wireframePsShader = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "WireframePS", "ps_5_1");
}

void EditableObjectManager::CountLodOffsets(LodData* lod) const
{
	for (int j = 0; j < lod->Meshes.size(); j++)
	{
		auto& meshData = lod->Meshes[j];
		meshData.CbOffset = j * _cbMeshElementSize;
		meshData.MatOffset = static_cast<int>(meshData.MaterialIndex * _cbMaterialElementSize);
	}
}

void EditableObjectManager::CountLodIndex(EditableRenderItem* ri, const float screenHeight) const
{
	if (ri->LodsData.size() == 1)
		return;

	BoundingSphere riSphere;
	riSphere.Center = ri->Bounds.Center;
	riSphere.Radius = XMVectorGetX(XMVector3Length(XMLoadFloat3(&ri->Bounds.Extents)));

	BoundingSphere worldSphere;
	riSphere.Transform(worldSphere, ri->World);
	XMVECTOR worldPos = XMLoadFloat3(&worldSphere.Center);

	const float size = ComputeScreenSize(worldPos, worldSphere.Radius, screenHeight);

	int preferableLod;

	if (size > 200.0f) preferableLod = 0;
	else if (size > 150.0f)  preferableLod = 1;
	else if (size > 100.0f)  preferableLod = 2;
	else if (size > 70.0f)  preferableLod = 3; 
	else if (size > 30.0f)  preferableLod = 4;
	else preferableLod = 5;

	const int maxIndex = static_cast<int>(ri->LodsData.size()) - 1;

	ri->CurrentLodIdx = std::min(preferableLod, maxIndex);
}

float EditableObjectManager::ComputeScreenSize(XMVECTOR& center, const float radius, const float screenHeight) const
{
	center = XMVector3TransformCoord(center, _camera->GetView());

	const float dist = XMVectorGetZ(center);
	if (dist <= 0.0f)
		return FLT_MAX; // behind camera treat as very large

	const float projectedSize = (radius / dist) * (screenHeight / (2.0f * tanf(_camera->GetFovY() * 0.5f)));

	return projectedSize; // in pixels (height of sphere on screen)
}

void EditableObjectManager::AddObjectToResource(const Microsoft::WRL::ComPtr<ID3D12Device> device, FrameResource* currFrameResource)
{
	for (const auto& obj : _objects)
		currFrameResource->AddOpaqueObjectBuffer(device.Get(), obj->Uid, static_cast<int>(obj->LodsData.begin()->Meshes.size()), static_cast<int>(obj->Materials.size()));
}

int EditableObjectManager::AddRenderItem(ID3D12Device* device, ModelData&& modelData)  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
{
	// Correct: actually move out the string
	const std::string itemName = modelData.CroppedName;
	const std::string name = itemName;

	// Do NOT move a bool — moving a POD does nothing
	const bool isTesselated = modelData.IsTesselated;

	auto modelRitem = std::make_shared<EditableRenderItem>();
	_objectLoaded[itemName]++;

	modelRitem->Uid = _uidCount++;
	modelRitem->Name = name;
	modelRitem->NameCount = _objectCounters[itemName]++;

	modelRitem->Geo = &GeometryManager::Geometries()[itemName];
	modelRitem->PrimitiveType = isTesselated ?
		D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST :
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	modelRitem->IsTesselated = isTesselated;

	// Correct moves
	modelRitem->Materials = std::move(modelData.Materials);
	for (const auto& material : modelRitem->Materials)
	{
		material->numFramesDirty = gNumFrameResources;
	}
	modelRitem->Bounds = std::move(modelData.Aabb);
	modelRitem->LodsData = std::move(modelData.LodsData);
	modelRitem->Transform = std::move(modelData.Transform);

	const auto& lod = modelRitem->LodsData.begin();

	for (int j = 0; j < lod->Meshes.size(); j++)
	{
		const auto& meshData = lod->Meshes[j];
		modelRitem->Materials[meshData.MaterialIndex]->isUsed = true;
	}

	for (int i = 0; i < modelRitem->LodsData.size(); i++)
	{
		auto& lodData = modelRitem->LodsData[i];
		CountLodOffsets(&lodData);
	}

	for (int i = 0; i < FrameResource::FrameResources().size(); ++i)
	{
		FrameResource::FrameResources()[i]->AddOpaqueObjectBuffer(device, modelRitem->Uid,
		                                                          static_cast<int>(modelRitem->LodsData.begin()->Meshes.
			                                                          size()) + 1, static_cast<int>(modelRitem->Materials.size()));
	}

	(isTesselated ? _tesselatedObjects : _untesselatedObjects)[modelRitem->Uid] = modelRitem.get();

	_objects.push_back(std::move(modelRitem));

	UploadManager::ExecuteUploadCommandList();

	return static_cast<int>(_objects.size()) - 1;
}

int EditableObjectManager::AddLod(ID3D12Device* device, LodData lod, EditableRenderItem* ri) const
{
	int i = 0;
	for (; i < ri->LodsData.size(); i++)
	{
		if (lod.TriangleCount > ri->LodsData[i].TriangleCount)
			break;
	}
	CountLodOffsets(&lod);

	ri->LodsData.emplace(ri->LodsData.begin() + i, lod);
	return i;
}

void EditableObjectManager::DeleteLod(EditableRenderItem* ri, const int index)
{
	ri->LodsData.erase(ri->LodsData.begin() + index);
	ri->CurrentLodIdx = std::min(ri->CurrentLodIdx, static_cast<int>(ri->LodsData.size()) - 1);
	GeometryManager::DeleteLodGeometry(ri->Name, index);
}

bool EditableObjectManager::DeleteObject(const int selectedObject)
{
	const EditableRenderItem* objectToDelete = _objects[selectedObject].get();

	for (auto& materialToDelete : objectToDelete->Materials)
	{
		for (int i = 0; i < BasicUtil::EnumIndex(MatProp::Count); i++)
		{
			const std::string texName = materialToDelete->properties[i].texture.Name;
			TextureManager::DeleteTexture(std::wstring(texName.begin(), texName.end()));
		}
		for (int i = 0; i < BasicUtil::EnumIndex(MatTex::Count); i++)
		{
			const std::string texName = materialToDelete->textures[i].Name;
			TextureManager::DeleteTexture(std::wstring(texName.begin(), texName.end()));
		}
	}

	const std::string name = objectToDelete->Name;
	_objectLoaded[name]--;

	//need for deleting from frame resource
	std::uint32_t uid = objectToDelete->Uid;

	if (_tesselatedObjects.find(uid) != _tesselatedObjects.end())
	{
		_tesselatedObjects.erase(uid);
		for (int i = 0; i < _visibleTesselatedObjects.size(); i++)
		{
			if (_visibleTesselatedObjects[i] == uid)
			{
				_visibleTesselatedObjects.erase(_visibleTesselatedObjects.begin() + i);
				break;
			}
		}
	}
	else
	{
		_untesselatedObjects.erase(uid);
		for (int i = 0; i < _visibleUntesselatedObjects.size(); i++)
		{
			if (_visibleUntesselatedObjects[i] == uid)
			{
				_visibleUntesselatedObjects.erase(_visibleUntesselatedObjects.begin() + i);
				break;
			}
		}
	}

	_objects.erase(_objects.begin() + selectedObject);

	UploadManager::Flush();

	for (int i = 0; i < gNumFrameResources; ++i)
	{
		FrameResource::FrameResources()[i]->RemoveOpaqueObjectBuffer(UploadManager::Device, uid);
	}

	if (_objectLoaded[name] == 0)
	{
		GeometryManager::UnloadModel(name);
		_objectLoaded.erase(name);

		return true;
	}

	return false;
}

int EditableObjectManager::ObjectsCount()
{
	return static_cast<int>(_objects.size());
}

std::string EditableObjectManager::ObjectName(const int i)
{
	return _objects[i]->Name +
		(_objects[i]->NameCount == 0 ? "" : std::to_string(_objects[i]->NameCount));
}

EditableRenderItem* EditableObjectManager::Object(const int i)
{
	return _objects[i].get();
}

void EditableObjectManager::Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, const float screenHeight, const bool isWireframe, const bool fixedLod) const
{
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	//draw without tesselation
	cmdList->SetPipelineState(isWireframe ? _wireframePso.Get() : _pso.Get());

	const auto passCb = currFrameResource->GBufferPassCb->Resource();
	cmdList->SetGraphicsRootConstantBufferView(10, passCb->GetGPUVirtualAddress());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	DrawObjects(cmdList, currFrameResource, _visibleUntesselatedObjects, _untesselatedObjects, screenHeight, fixedLod);

	//draw with tesselation
	cmdList->SetPipelineState(isWireframe ? _wireframeTesselatedPso.Get() : _tesselatedPso.Get());

	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

	DrawObjects(cmdList, currFrameResource, _visibleTesselatedObjects, _tesselatedObjects, screenHeight, fixedLod);

	if (_drawDebug)
	{
		cmdList->SetPipelineState(_wireframePso.Get());
		cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		DrawAabBs(cmdList, currFrameResource);
	}
}

void EditableObjectManager::DrawObjects(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource,
                                        const std::vector<uint32_t>& indices,
                                        std::unordered_map<uint32_t, EditableRenderItem*> objects,
                                        const float screenHeight, const bool fixedLod) const
{
	for (auto& idx : indices)
	{
		const auto& ri = objects[idx];
		const auto objectCb = currFrameResource->OpaqueObjCb[ri->Uid]->Resource();
		if (!fixedLod)
			CountLodIndex(ri, screenHeight);

		const int curLodIdx = ri->CurrentLodIdx;
		const MeshGeometry* curLodGeo = ri->Geo->at(curLodIdx).get();
		const auto vertexBuffer = curLodGeo->VertexBufferView();
		cmdList->IASetVertexBuffers(0, 1, &vertexBuffer);
		const auto indexBuffer = curLodGeo->IndexBufferView();
		cmdList->IASetIndexBuffer(&indexBuffer);
		const auto materialCb = currFrameResource->MaterialCb[ri->Uid]->Resource();

		auto currentLod = ri->LodsData[curLodIdx];

		for (size_t i = 0; i < currentLod.Meshes.size(); i++)
		{
			const auto& meshData = currentLod.Meshes.at(i);
			const D3D12_GPU_VIRTUAL_ADDRESS meshCbAddress = objectCb->GetGPUVirtualAddress() + meshData.CbOffset;

			cmdList->SetGraphicsRootConstantBufferView(8, meshCbAddress);

			const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddress = materialCb->GetGPUVirtualAddress() + meshData.MatOffset;
			cmdList->SetGraphicsRootConstantBufferView(9, materialCbAddress);

			const auto heapAlloc = TextureManager::SrvHeapAllocator.get();
			const auto material = ri->Materials[meshData.MaterialIndex].get();
			constexpr size_t offset = BasicUtil::EnumIndex(MatProp::Count) - 1;
			for (int index = 0; index < offset; index++)
			{
				if (index == BasicUtil::EnumIndex(MatProp::Opacity)) index++;
				const CD3DX12_GPU_DESCRIPTOR_HANDLE tex(heapAlloc->GetGpuHandle(material->properties[index].texture.Index));
				cmdList->SetGraphicsRootDescriptorTable(index, tex);
			}
			for (int i1 = 0; i1 < BasicUtil::EnumIndex(MatTex::Count); i1++)
			{
				const CD3DX12_GPU_DESCRIPTOR_HANDLE tex(heapAlloc->GetGpuHandle(material->textures[i1].Index));
				cmdList->SetGraphicsRootDescriptorTable(offset + i1, tex);
			}

			cmdList->DrawIndexedInstanced(static_cast<UINT>(meshData.IndexCount), 1,
			                              static_cast<UINT>(meshData.IndexStart),
			                              static_cast<UINT>(meshData.VertexStart), 0);
		}
	}
}

void EditableObjectManager::DrawAabBs(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource) const
{
	for (const auto& ri : _objects)
	{
		const auto objectCb = currFrameResource->OpaqueObjCb[ri->Uid]->Resource();
		const D3D12_GPU_VIRTUAL_ADDRESS aabbcbAddress = objectCb->GetGPUVirtualAddress() + ri->LodsData.begin()->Meshes.size() * _cbMeshElementSize;

		//draw local lights
		MeshGeometry* geo = (GeometryManager::Geometries()["shapeGeo"].begin())->get();

		const auto vertexBuffer = geo->VertexBufferView();
		cmdList->IASetVertexBuffers(0, 1, &vertexBuffer);
		const auto indexBuffer = geo->IndexBufferView();
		cmdList->IASetIndexBuffer(&indexBuffer);

		const SubmeshGeometry mesh = geo->DrawArgs["box"];

		cmdList->SetGraphicsRootConstantBufferView(8, aabbcbAddress);

		cmdList->DrawIndexedInstanced(mesh.IndexCount, 1, mesh.StartIndexLocation,
			mesh.BaseVertexLocation, 0);
	}
}

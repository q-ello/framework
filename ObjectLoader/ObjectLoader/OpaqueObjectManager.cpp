#pragma once
#include "OpaqueObjectManager.h"

void EditableObjectManager::UpdateObjectCBs(FrameResource* currFrameResource, Camera* camera)
{
	auto& currObjectsCB = currFrameResource->OpaqueObjCB;
	auto& currMaterialCB = currFrameResource->MaterialCB;

	//for frustum culling
	_visibleTesselatedObjects.clear();
	_visibleUntesselatedObjects.clear();

	XMMATRIX view = camera->GetView();
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);

	for (int i = 0; i < _objects.size(); i++)
	{
		auto& ri = _objects[i];
		XMMATRIX scale = XMMatrixScalingFromVector(XMLoadFloat3(&ri->transform[BasicUtil::EnumIndex(Transform::Scale)]));
		XMMATRIX rotation = XMMatrixRotationRollPitchYawFromVector(XMLoadFloat3(&ri->transform[BasicUtil::EnumIndex(Transform::Rotation)]) * XM_PI / 180.f);
		XMMATRIX translation = XMMatrixTranslationFromVector(XMLoadFloat3(&ri->transform[BasicUtil::EnumIndex(Transform::Translation)]));

		XMMATRIX world = scale * rotation * translation;
		//updating object data
		if (ri->NumFramesDirty > 0)
		{
			size_t j = 0;
			auto currentLod = ri->lodsData[ri->currentLODIdx];
			for (; j < currentLod.meshes.size(); j++)
			{
				XMMATRIX meshWorld = currentLod.meshes.at(j).defaultWorld * world;

				OpaqueObjectConstants objConstants;
				XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(meshWorld));
				XMStoreFloat4x4(&objConstants.WorldInvTranspose, XMMatrixInverse(&XMMatrixDeterminant(meshWorld), meshWorld));

				currObjectsCB[ri->uid].get()->CopyData((int)j, objConstants);
			}

			//last piece for AABB
			{
				XMMATRIX scaleAABB = XMMatrixScaling(ri->Bounds.Extents.x * 2.0f, ri->Bounds.Extents.y * 2.0f, ri->Bounds.Extents.z * 2.0f);
				XMMATRIX translationAABB = XMMatrixTranslation(ri->Bounds.Center.x, ri->Bounds.Center.y, ri->Bounds.Center.z);
				XMMATRIX AABBWorld = scaleAABB * translationAABB * world;

				OpaqueObjectConstants objConstants;
				XMStoreFloat4x4(&objConstants.World, DirectX::XMMatrixTranspose(AABBWorld));
				XMStoreFloat4x4(&objConstants.WorldInvTranspose, XMMatrixInverse(&XMMatrixDeterminant(AABBWorld), AABBWorld));

				currObjectsCB[ri->uid].get()->CopyData((int)j, objConstants);
			}

			ri->NumFramesDirty--;
		}

		for (size_t j = 0; j < ri->materials.size(); j++)
		{
			Material* material = ri->materials[j].get();
			if (material->numFramesDirty > 0)
			{
				MaterialConstants materialConstants;
				materialConstants.baseColor = material->properties[BasicUtil::EnumIndex(MatProp::BaseColor)].value;
				materialConstants.displacementScale = material->additionalInfo[BasicUtil::EnumIndex(MatAddInfo::Displacement)];
				materialConstants.emissive = material->properties[BasicUtil::EnumIndex(MatProp::Emissive)].value;
				materialConstants.emissiveIntensity = material->additionalInfo[BasicUtil::EnumIndex(MatAddInfo::Emissive)];
				materialConstants.metallic = material->properties[BasicUtil::EnumIndex(MatProp::Metallic)].value.x;
				materialConstants.opacity = material->properties[BasicUtil::EnumIndex(MatProp::Opacity)].value.x;
				materialConstants.roughness = material->properties[BasicUtil::EnumIndex(MatProp::Roughness)].value.x;
				materialConstants.useAOMap = material->textures[BasicUtil::EnumIndex(MatTex::AmbOcc)].useTexture;
				materialConstants.useBaseColorMap = material->properties[BasicUtil::EnumIndex(MatProp::BaseColor)].texture.useTexture;
				materialConstants.useDisplacementMap = material->textures[BasicUtil::EnumIndex(MatTex::Displacement)].useTexture;
				materialConstants.useEmissiveMap = material->properties[BasicUtil::EnumIndex(MatProp::Emissive)].texture.useTexture;
				materialConstants.useMetallicMap = material->properties[BasicUtil::EnumIndex(MatProp::Metallic)].texture.useTexture;
				materialConstants.useNormalMap = material->textures[BasicUtil::EnumIndex(MatTex::Normal)].useTexture;
				materialConstants.useOpacityMap = material->properties[BasicUtil::EnumIndex(MatProp::Opacity)].texture.useTexture;
				materialConstants.useRoughnessMap = material->properties[BasicUtil::EnumIndex(MatProp::Roughness)].texture.useTexture;
				materialConstants.useARMMap = material->textures[BasicUtil::EnumIndex(MatTex::ARM)].useTexture && material->useARMTexture;
				materialConstants.ARMLayout = (int)material->armLayout;

				currMaterialCB[ri->uid].get()->CopyData((int)j, materialConstants);

				material->numFramesDirty--;
			}
		}

		//frustum culling
		XMMATRIX invWorld = XMMatrixInverse(&XMMatrixDeterminant(world), world);

		XMMATRIX viewToLocal = XMMatrixMultiply(invView, invWorld);

		BoundingFrustum localSpaceFrustum;
		camera->CameraFrustum().Transform(localSpaceFrustum, viewToLocal);
		if ((localSpaceFrustum.Contains(ri->Bounds) != DirectX::DISJOINT))
		{
			if (_tesselatedObjects.find(ri->uid) != _tesselatedObjects.end())
			{
				_visibleTesselatedObjects.push_back(ri->uid);
			}
			else
			{
				_visibleUntesselatedObjects.push_back(ri->uid);
			}
		}
	}
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
	CD3DX12_DESCRIPTOR_RANGE AOTexTable;
	AOTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5);
	CD3DX12_DESCRIPTOR_RANGE dispTexTable;
	dispTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6);
	CD3DX12_DESCRIPTOR_RANGE ARMTexTable;
	ARMTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7);

	const int rootParamCount = 11;

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[rootParamCount];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &baseColorTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsDescriptorTable(1, &emissTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[2].InitAsDescriptorTable(1, &roughTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[3].InitAsDescriptorTable(1, &metallTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[4].InitAsDescriptorTable(1, &normTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[5].InitAsDescriptorTable(1, &AOTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[6].InitAsDescriptorTable(1, &dispTexTable, D3D12_SHADER_VISIBILITY_DOMAIN);
	slotRootParameter[7].InitAsDescriptorTable(1, &ARMTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[8].InitAsConstantBufferView(0);
	slotRootParameter[9].InitAsConstantBufferView(1);
	slotRootParameter[10].InitAsConstantBufferView(2);

	auto staticSamplers = TextureManager::GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(rootParamCount, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
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

void EditableObjectManager::BuildPSO()
{
	//for untesselated objects
	D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc;
	ZeroMemory(&PSODesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	PSODesc.InputLayout = { _inputLayout.data(), (UINT)_inputLayout.size() };
	PSODesc.pRootSignature = _rootSignature.Get();
	PSODesc.VS =
	{
		reinterpret_cast<BYTE*>(_vsShader->GetBufferPointer()),
		_vsShader->GetBufferSize()
	};
	PSODesc.PS =
	{
		reinterpret_cast<BYTE*>(_psShader->GetBufferPointer()),
		_psShader->GetBufferSize()
	};
	PSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	PSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	PSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	PSODesc.SampleMask = UINT_MAX;
	PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	PSODesc.NumRenderTargets = GBuffer::InfoCount();
	for (int i = 0; i < GBuffer::InfoCount(); i++)
	{
		PSODesc.RTVFormats[i] = GBuffer::infoFormats[i];
	}
	PSODesc.SampleDesc.Count = 1;
	PSODesc.SampleDesc.Quality = 0;
	PSODesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&PSODesc, IID_PPV_ARGS(&_pso)));

	//wireframe untesselated
	D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframePSODesc = PSODesc;
	wireframePSODesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	wireframePSODesc.PS =
	{
		reinterpret_cast<BYTE*>(_wireframePSShader->GetBufferPointer()),
		_wireframePSShader->GetBufferSize()
	};
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&wireframePSODesc, IID_PPV_ARGS(&_wireframePSO)));

	//tesselated
	D3D12_GRAPHICS_PIPELINE_STATE_DESC tesselatedPSODesc = PSODesc;
	tesselatedPSODesc.VS =
	{
		reinterpret_cast<BYTE*>(_tessVSShader->GetBufferPointer()),
		_tessVSShader->GetBufferSize()
	};
	tesselatedPSODesc.HS =
	{
		reinterpret_cast<BYTE*>(_tessHSShader->GetBufferPointer()),
		_tessHSShader->GetBufferSize()
	};
	tesselatedPSODesc.DS =
	{
		reinterpret_cast<BYTE*>(_tessDSShader->GetBufferPointer()),
		_tessDSShader->GetBufferSize()
	};
	tesselatedPSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&tesselatedPSODesc, IID_PPV_ARGS(&_tesselatedPSO)));

	//wireframe tesselated
	D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframeTesselatedPSODesc = tesselatedPSODesc;
	wireframeTesselatedPSODesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	wireframeTesselatedPSODesc.PS =
	{
		reinterpret_cast<BYTE*>(_wireframePSShader->GetBufferPointer()),
		_wireframePSShader->GetBufferSize()
	};

	ThrowIfFailed(_device->CreateGraphicsPipelineState(&wireframeTesselatedPSODesc, IID_PPV_ARGS(&_wireframeTesselatedPSO)));
}

void EditableObjectManager::BuildShaders()
{
	_vsShader = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "GBufferVS", "vs_5_1");
	_psShader = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "GBufferPS", "ps_5_1");
	_tessVSShader = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "TessVS", "vs_5_1");
	_tessHSShader = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "TessHS", "hs_5_1");
	_tessDSShader = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "TessDS", "ds_5_1");
	_wireframePSShader = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "WireframePS", "ps_5_1");
}

void EditableObjectManager::CountLODOffsets(LODData* lod, int i)
{
	for (int j = 0; j < lod->meshes.size(); j++)
	{
		auto& meshData = lod->meshes[j];
		meshData.cbOffset = i * _cbMeshElementSize;
		meshData.matOffset = meshData.materialIndex * _cbMaterialElementSize;
	}
}

void EditableObjectManager::AddObjectToResource(Microsoft::WRL::ComPtr<ID3D12Device> device, FrameResource* currFrameResource)
{
	for (auto& obj : _objects)
		currFrameResource->addOpaqueObjectBuffer(device.Get(), obj->uid, (int)obj->lodsData.size(), (int)obj->materials.size());
}

int EditableObjectManager::addRenderItem(ID3D12Device* device, ModelData&& modelData)
{
	const std::string& itemName = modelData.croppedName;
	std::string name(itemName.begin(), itemName.end());
	bool isTesselated = modelData.isTesselated;

	auto modelRitem = std::make_unique<EditableRenderItem>();
	_objectLoaded[itemName]++;
	modelRitem->uid = uidCount++;
	modelRitem->Name = name;
	modelRitem->nameCount = _objectCounters[itemName]++;
	modelRitem->Geo = &GeometryManager::geometries()[itemName];
	modelRitem->PrimitiveType = isTesselated ? D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	modelRitem->isTesselated = isTesselated;
	modelRitem->materials = std::move(modelData.materials);
	for (auto& material : modelRitem->materials)
	{
		material->numFramesDirty = gNumFrameResources;
	}
	modelRitem->Bounds = modelData.AABB;
	modelRitem->lodsData = modelData.lodsData;
	modelRitem->transform = modelData.transform;

	auto& lod = modelRitem->lodsData.begin();

	for (int j = 0; j < lod->meshes.size(); j++)
	{
		auto& meshData = lod->meshes[j];
		modelRitem->materials[meshData.materialIndex]->isUsed = true;
	}

	for (int i = 0; i < modelRitem->lodsData.size(); i++)
	{
		auto& lodData = modelRitem->lodsData[i];
		CountLODOffsets(&lodData, i);
	}

	for (int i = 0; i < FrameResource::frameResources().size(); ++i)
	{
		FrameResource::frameResources()[i]->addOpaqueObjectBuffer(device, modelRitem->uid, modelRitem->lodsData.size() + 1, modelRitem->materials.size());
	}

	if (isTesselated)
	{
		_tesselatedObjects[modelRitem->uid] = modelRitem.get();
	}
	else
	{
		_untesselatedObjects[modelRitem->uid] = modelRitem.get();
	}

	_objects.push_back(std::move(modelRitem));

	UploadManager::ExecuteUploadCommandList();

	return (int)_objects.size() - 1;
}

int EditableObjectManager::addLOD(ID3D12Device* device, LODData lod, EditableRenderItem* ri)
{
	int i = 0;
	for (; i < ri->lodsData.size(); i++)
	{
		if (lod.vertexCount > ri->lodsData[i].vertexCount)
			break;
	}
	CountLODOffsets(&lod, i);

	ri->lodsData.emplace(ri->lodsData.begin() + i, lod);
	return i;
}

bool EditableObjectManager::deleteObject(int selectedObject)
{
	EditableRenderItem* objectToDelete = _objects[selectedObject].get();

	for (auto& materialToDelete : objectToDelete->materials)
	{
		for (int i = 0; i < BasicUtil::EnumIndex(MatProp::Count); i++)
		{
			const std::string texName = materialToDelete->properties[i].texture.name;
			TextureManager::deleteTexture(std::wstring(texName.begin(), texName.end()));
		}
		for (int i = 0; i < BasicUtil::EnumIndex(MatTex::Count); i++)
		{
			const std::string texName = materialToDelete->textures[i].name;
			TextureManager::deleteTexture(std::wstring(texName.begin(), texName.end()));
		}
	}

	std::string name = objectToDelete->Name;
	_objectLoaded[name]--;

	//need for deleting from frame resource
	std::uint32_t uid = objectToDelete->uid;

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
		FrameResource::frameResources()[i]->removeOpaqueObjectBuffer(UploadManager::device, uid);
	}

	if (_objectLoaded[name] == 0)
	{
		GeometryManager::UnloadModel(name);
		_objectLoaded.erase(name);

		return true;
	}

	return false;
}

int EditableObjectManager::objectsCount()
{
	return (int)_objects.size();
}

std::string EditableObjectManager::objectName(int i)
{
	return _objects[i]->Name +
		(_objects[i]->nameCount == 0 ? "" : std::to_string(_objects[i]->nameCount));
}

EditableRenderItem* EditableObjectManager::object(int i)
{
	return _objects[i].get();
}

void EditableObjectManager::Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, bool isWireframe)
{
	if (_objects.size() == 0)
	{
		int tes = (int)_tesselatedObjects.size();
		int untes = (int)_untesselatedObjects.size();
	}

	ID3D12DescriptorHeap* descriptorHeaps[] = { TextureManager::srvDescriptorHeap.Get() };
	cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	//draw without tesselation
	cmdList->SetPipelineState(isWireframe ? _wireframePSO.Get() : _pso.Get());

	auto passCB = currFrameResource->GBufferPassCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(10, passCB->GetGPUVirtualAddress());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	DrawObjects(cmdList, currFrameResource, _visibleUntesselatedObjects, _untesselatedObjects);
	
	//draw with tesselation
	cmdList->SetPipelineState(isWireframe ? _wireframeTesselatedPSO.Get() : _tesselatedPSO.Get());

	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

	DrawObjects(cmdList, currFrameResource, _visibleTesselatedObjects, _tesselatedObjects);

	if (_drawDebug)
	{
		cmdList->SetPipelineState(_wireframePSO.Get());
		cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		DrawAABBs(cmdList, currFrameResource);
	}
}

void EditableObjectManager::DrawObjects(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, std::vector<uint32_t> indices, std::unordered_map<uint32_t, EditableRenderItem*> objects)
{
	for (auto& idx : indices)
	{
		auto& ri = objects[idx];
		auto objectCB = currFrameResource->OpaqueObjCB[ri->uid]->Resource();

		int curLODIdx = ri->currentLODIdx;
		MeshGeometry* curLODGeo = ri->Geo->at(curLODIdx).get();
		cmdList->IASetVertexBuffers(0, 1, &curLODGeo->VertexBufferView());
		cmdList->IASetIndexBuffer(&curLODGeo->IndexBufferView());
		auto materialCB = currFrameResource->MaterialCB[ri->uid]->Resource();

		auto currentLOD = ri->lodsData[curLODIdx];

		for (size_t i = 0; i < currentLOD.meshes.size(); i++)
		{
			auto& meshData = currentLOD.meshes.at(i);
			D3D12_GPU_VIRTUAL_ADDRESS meshCBAddress = objectCB->GetGPUVirtualAddress() + meshData.cbOffset;

			cmdList->SetGraphicsRootConstantBufferView(8, meshCBAddress);

			D3D12_GPU_VIRTUAL_ADDRESS materialCBAddress = materialCB->GetGPUVirtualAddress() + meshData.matOffset;
			cmdList->SetGraphicsRootConstantBufferView(9, materialCBAddress);

			auto heapAlloc = TextureManager::srvHeapAllocator.get();
			auto material = ri->materials[meshData.materialIndex].get();
			constexpr size_t offset = BasicUtil::EnumIndex(MatProp::Count) - 1;
			for (int i = 0; i < offset; i++)
			{
				if (i == BasicUtil::EnumIndex(MatProp::Opacity)) i++;
				CD3DX12_GPU_DESCRIPTOR_HANDLE tex(heapAlloc->GetGpuHandle(material->properties[i].texture.index));
				cmdList->SetGraphicsRootDescriptorTable(i, tex);
			}
			for (int i = 0; i < BasicUtil::EnumIndex(MatTex::Count); i++)
			{
				CD3DX12_GPU_DESCRIPTOR_HANDLE tex(heapAlloc->GetGpuHandle(material->textures[i].index));
				cmdList->SetGraphicsRootDescriptorTable(offset + i, tex);
			}

			cmdList->DrawIndexedInstanced(meshData.indexCount, 1, meshData.indexStart, meshData.vertexStart, 0);
		}
	}
}

void EditableObjectManager::DrawAABBs(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource)
{
	for (auto& ri : _objects)
	{
		auto objectCB = currFrameResource->OpaqueObjCB[ri->uid]->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS AABBCBAddress = objectCB->GetGPUVirtualAddress() + ri->lodsData.size() * _cbMeshElementSize;

		//draw local lights
		MeshGeometry* geo = (GeometryManager::geometries()["shapeGeo"].begin())->get();

		cmdList->IASetVertexBuffers(0, 1, &geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&geo->IndexBufferView());

		SubmeshGeometry mesh = geo->DrawArgs["box"];

		cmdList->SetGraphicsRootConstantBufferView(8, AABBCBAddress);

		cmdList->DrawIndexedInstanced(mesh.IndexCount, 1, mesh.StartIndexLocation,
			mesh.BaseVertexLocation, 0);
	}
}

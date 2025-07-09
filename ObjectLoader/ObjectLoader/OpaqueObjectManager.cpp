#pragma once
#include "OpaqueObjectManager.h"

void OpaqueObjectManager::UpdateObjectCBs(FrameResource* currFrameResource)
{
	auto& currObjectsCB = currFrameResource->OpaqueObjCB;
	auto& currMaterialCB = currFrameResource->MaterialCB;
	for (int i = 0; i < _objects.size(); i++)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		auto& ri = _objects[i];
		if (ri->NumFramesDirty > 0)
		{
			XMMATRIX scale = XMMatrixScalingFromVector(XMLoadFloat3(&ri->transform[2]));
			XMMATRIX rotation = XMMatrixRotationRollPitchYawFromVector(XMLoadFloat3(&ri->transform[1]) * XM_PI / 180.f);
			XMMATRIX translation = XMMatrixTranslationFromVector(XMLoadFloat3(&ri->transform[0]));

			XMMATRIX world = scale * rotation * translation;
			XMStoreFloat4x4(&ri->World, world);

			OpaqueObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.WorldInvTranspose, XMMatrixInverse(&XMMatrixDeterminant(world), world));

			currObjectsCB[ri->uid].get()->CopyData(0, objConstants);

			ri->NumFramesDirty--;
		}

		auto& material = ri->material;

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

			currMaterialCB[ri->uid].get()->CopyData(0, materialConstants);

			material->numFramesDirty--;
		}
	}
}

void OpaqueObjectManager::BuildInputLayout()
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

void OpaqueObjectManager::BuildRootSignature()
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

void OpaqueObjectManager::BuildPSO()
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

void OpaqueObjectManager::BuildShaders()
{
	_vsShader = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "GBufferVS", "vs_5_1");
	_psShader = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "GBufferPS", "ps_5_1");
	_tessVSShader = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "TessVS", "vs_5_1");
	_tessHSShader = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "TessHS", "hs_5_1");
	_tessDSShader = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "TessDS", "ds_5_1");
	_wireframePSShader = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "WireframePS", "ps_5_1");
}

void OpaqueObjectManager::AddObjectToResource(Microsoft::WRL::ComPtr<ID3D12Device> device, FrameResource* currFrameResource)
{
	for (auto& obj : _objects)
		currFrameResource->addOpaqueObjectBuffer(device.Get(), obj->uid);
}

int OpaqueObjectManager::addRenderItem(ID3D12Device* device, const std::string& itemName, bool isTesselated, std::unique_ptr<Material> material)
{
	std::string name(itemName.begin(), itemName.end());

	auto modelRitem = std::make_unique<EditableRenderItem>();
	_objectLoaded[itemName]++;
	modelRitem->uid = uidCount++;
	modelRitem->Name = name;
	modelRitem->nameCount = _objectCounters[itemName]++;
	modelRitem->Geo = GeometryManager::geometries()[itemName].get();
	modelRitem->PrimitiveType = isTesselated ? D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	modelRitem->IndexCount = modelRitem->Geo->DrawArgs[itemName].IndexCount;
	modelRitem->material = std::move(material);
	modelRitem->material->numFramesDirty = gNumFrameResources;

	for (int i = 0; i < FrameResource::frameResources().size(); ++i)
	{
		FrameResource::frameResources()[i]->addOpaqueObjectBuffer(device, modelRitem->uid);
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

bool OpaqueObjectManager::deleteObject(int selectedObject)
{
	EditableRenderItem* objectToDelete = _objects[selectedObject].get();

	Material* materialToDelete = objectToDelete->material.get();
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

	std::string name = objectToDelete->Name;
	_objectLoaded[name]--;

	//need for deleting from frame resource
	std::uint32_t uid = objectToDelete->uid;

	if (objectToDelete->PrimitiveType == D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST)
	{

		_tesselatedObjects.erase(uid);
	}
	else
	{
		_untesselatedObjects.erase(uid);
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

int OpaqueObjectManager::objectsCount()
{
	return (int)_objects.size();
}

std::string OpaqueObjectManager::objectName(int i)
{
	return _objects[i]->Name +
		(_objects[i]->nameCount == 0 ? "" : std::to_string(_objects[i]->nameCount));
}

EditableRenderItem* OpaqueObjectManager::object(int i)
{
	return _objects[i].get();
}

void OpaqueObjectManager::Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, bool isWireframe)
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

	DrawObjects(cmdList, currFrameResource, _untesselatedObjects);
	
	//draw with tesselation
	cmdList->SetPipelineState(isWireframe ? _wireframeTesselatedPSO.Get() : _tesselatedPSO.Get());

	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

	DrawObjects(cmdList, currFrameResource, _tesselatedObjects);
}

void OpaqueObjectManager::DrawObjects(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, std::unordered_map<uint32_t, EditableRenderItem*> objects)
{
	for (auto& obj : objects)
	{
		auto& ri = obj.second;
		auto objectCB = currFrameResource->OpaqueObjCB[ri->uid]->Resource();

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress();

		cmdList->SetGraphicsRootConstantBufferView(8, objCBAddress);
		
		auto materialCB = currFrameResource->MaterialCB[ri->uid]->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS materialCBAddress = materialCB->GetGPUVirtualAddress();
		cmdList->SetGraphicsRootConstantBufferView(9, materialCBAddress);

		auto heapAlloc = TextureManager::srvHeapAllocator.get();
		auto material = ri->material.get();
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

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, 0, 0, 0);
	}
}

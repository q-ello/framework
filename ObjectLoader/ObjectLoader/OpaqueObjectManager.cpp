#pragma once
#include "OpaqueObjectManager.h"

void OpaqueObjectManager::UpdateObjectCBs(FrameResource* currFrameResource)
{
	auto& currObjectsCB = currFrameResource->OpaqueObjCB;
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
			objConstants.useNormalMap = ri->normalHandle.isRelevant;
			if (ri->normalHandle.isRelevant)
			{
				const std::wstring& texName = std::wstring(ri->normalHandle.name.begin(), ri->normalHandle.name.end());
				objConstants.normalMapSize = { TextureManager::textures()[texName]->size[0], TextureManager::textures()[texName]->size[1] };
			}
			objConstants.useDisplacementMap = ri->displacementHandle.isRelevant;
			objConstants.displacementScale = ri->dispScale;

			currObjectsCB[ri->uid].get()->CopyData(0, objConstants);

			ri->NumFramesDirty--;
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
	CD3DX12_DESCRIPTOR_RANGE diffTexTable;
	diffTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	CD3DX12_DESCRIPTOR_RANGE normTexTable;
	normTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
	CD3DX12_DESCRIPTOR_RANGE dispTexTable;
	dispTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);

	const int rootParamCount = 5;

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[rootParamCount];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &diffTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsDescriptorTable(1, &normTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[2].InitAsConstantBufferView(0);
	slotRootParameter[3].InitAsConstantBufferView(1);
	slotRootParameter[4].InitAsDescriptorTable(1, &dispTexTable, D3D12_SHADER_VISIBILITY_DOMAIN);

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

int OpaqueObjectManager::addRenderItem(ID3D12Device* device, const std::wstring& itemName, bool isTesselated)
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
	modelRitem->diffuseHandle.index = 0;

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

	const std::string diffName = objectToDelete->diffuseHandle.name;
	TextureManager::deleteTexture(std::wstring(diffName.begin(), diffName.end()));
	const std::string normalName = objectToDelete->normalHandle.name;
	TextureManager::deleteTexture(std::wstring(normalName.begin(), normalName.end()));
	const std::string dispName = objectToDelete->displacementHandle.name;
	TextureManager::deleteTexture(std::wstring(dispName.begin(), dispName.end()));

	std::wstring name(objectToDelete->Name.begin(), objectToDelete->Name.end());
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
		int tes = _tesselatedObjects.size();
		int untes = _untesselatedObjects.size();
	}

	ID3D12DescriptorHeap* descriptorHeaps[] = { TextureManager::srvDescriptorHeap.Get() };
	cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	//draw without tesselation
	cmdList->SetPipelineState(isWireframe ? _wireframePSO.Get() : _pso.Get());

	auto passCB = currFrameResource->GBufferPassCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(3, passCB->GetGPUVirtualAddress());
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

		cmdList->SetGraphicsRootConstantBufferView(2, objCBAddress);

		//textures
		CD3DX12_GPU_DESCRIPTOR_HANDLE diffuseTex(TextureManager::srvHeapAllocator.get()->GetGpuHandle(ri->diffuseHandle.index));
		cmdList->SetGraphicsRootDescriptorTable(0, diffuseTex);
		CD3DX12_GPU_DESCRIPTOR_HANDLE normalTex(TextureManager::srvHeapAllocator.get()->GetGpuHandle(ri->normalHandle.index));
		cmdList->SetGraphicsRootDescriptorTable(1, normalTex);
		CD3DX12_GPU_DESCRIPTOR_HANDLE dispTex(TextureManager::srvHeapAllocator.get()->GetGpuHandle(ri->displacementHandle.index));
		cmdList->SetGraphicsRootDescriptorTable(4, dispTex);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, 0, 0, 0);
	}
}

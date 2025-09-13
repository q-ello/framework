#include "UnlitObjectManager.h"

void UnlitObjectManager::UpdateObjectCBs(FrameResource* currFrameResource, Camera* camera)
{
	auto& currObjectsCB = currFrameResource->UnlitObjCB;
	for (int i = 0; i < _objects.size(); i++)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		auto& ri = _objects[i];
		if (ri->NumFramesDirty > 0)
		{
			UnlitObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixIdentity());

			currObjectsCB[ri->uid].get()->CopyData(0, objConstants);

			ri->NumFramesDirty--;
		}
	}
}

void UnlitObjectManager::BuildInputLayout()
{
	_inputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
}

void UnlitObjectManager::BuildRootSignature()
{
	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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

void UnlitObjectManager::BuildPSO()
{
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
	PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	PSODesc.NumRenderTargets = GBuffer::InfoCount();
	for (int i = 0; i < GBuffer::InfoCount(); i++)
	{
		PSODesc.RTVFormats[i] = GBuffer::infoFormats[i];
	}
	PSODesc.SampleDesc.Count = 1;
	PSODesc.SampleDesc.Quality = 0;
	PSODesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	ThrowIfFailed(UploadManager::device->CreateGraphicsPipelineState(&PSODesc, IID_PPV_ARGS(&_pso)));
}

void UnlitObjectManager::BuildShaders()
{
	_vsShader = d3dUtil::CompileShader(L"Shaders\\Unlit.hlsl", nullptr, "UnlitVS", "vs_5_1");
	_psShader = d3dUtil::CompileShader(L"Shaders\\Unlit.hlsl", nullptr, "UnlitPS", "ps_5_1");
}

void UnlitObjectManager::AddObjectToResource(Microsoft::WRL::ComPtr<ID3D12Device> device, FrameResource* currFrameResource)
{
	for (auto& obj : _objects)
		currFrameResource->addUnlitObjectBuffer(device.Get(), obj->uid);
}

int UnlitObjectManager::addRenderItem(ID3D12Device* device, ModelData&& modelData)
{
	auto renderItem = std::make_unique<UnlitRenderItem>();
	renderItem->uid = uidCount++;
	const std::string& itemName = modelData.croppedName;
	std::string name(itemName.begin(), itemName.end());

	renderItem->Name = name;
	renderItem->nameCount = _objectCounters[itemName]++;
	renderItem->Geo = &GeometryManager::geometries()["shapeGeo"];
	renderItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
	renderItem->IndexCount = renderItem->Geo->begin()->get()->DrawArgs[itemName].IndexCount;

	for (int i = 0; i < FrameResource::frameResources().size(); ++i)
	{
		AddObjectToResource(device, FrameResource::frameResources()[i].get());
	}

	_objects.push_back(std::move(renderItem));

	UploadManager::ExecuteUploadCommandList();

	return (int)_objects.size() - 1;
}

bool UnlitObjectManager::deleteObject(int selectedObject)
{
	std::string name = _objects[selectedObject]->Name;
	_objectLoaded[name]--;

	//need for deleting from frame resource
	std::uint32_t uid = _objects[selectedObject]->uid;

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

int UnlitObjectManager::objectsCount()
{
	return (int)_objects.size();
}

std::string UnlitObjectManager::objectName(int i)
{
	return _objects[i]->Name +
		(_objects[i]->nameCount == 0 ? "" : std::to_string(_objects[i]->nameCount));
}

void UnlitObjectManager::Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, bool isWireframe)
{
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());
	cmdList->SetPipelineState(_pso.Get());

	auto passCB = currFrameResource->GBufferPassCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(UnlitObjectConstants));

	// For each render item...
	for (size_t i = 0; i < _objects.size(); i++)
	{
		auto& ri = _objects[i];
		auto objectCB = currFrameResource->UnlitObjCB[ri->uid]->Resource();

		auto geo = ri->Geo->begin()->get();
		cmdList->IASetVertexBuffers(0, 1, &geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress();

		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, 0, 0, 0);
	}
}

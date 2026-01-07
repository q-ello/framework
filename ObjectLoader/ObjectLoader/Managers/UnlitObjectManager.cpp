#include "UnlitObjectManager.h"
#include "../../../Common/GBuffer.h"
#include "UploadManager.h"

void UnlitObjectManager::UpdateObjectCBs(FrameResource* currFrameResource)
{
	const auto& currObjectsCb = currFrameResource->StaticObjCb;
	for (int i = 0; i < _objects.size(); i++)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		const auto& ri = _objects[i];
		if (ri->NumFramesDirty > 0)
		{
			StaticObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixIdentity());

			currObjectsCb.get()->CopyData(ri->Uid, objConstants);

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

	// Performance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);

	// A root signature is an array of root parameters.
	const CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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

void UnlitObjectManager::BuildPso()
{
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
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	psoDesc.NumRenderTargets = GBuffer::InfoCount();
	for (int i = 0; i < GBuffer::InfoCount(); i++)
	{
		psoDesc.RTVFormats[i] = GBuffer::infoFormats[i];
	}
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	ThrowIfFailed(UploadManager::Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pso)));
}

void UnlitObjectManager::BuildShaders()
{
	_vsShader = d3dUtil::CompileShader(L"Shaders\\Unlit.hlsl", nullptr, "UnlitVS", "vs_5_1");
	_psShader = d3dUtil::CompileShader(L"Shaders\\Unlit.hlsl", nullptr, "UnlitPS", "ps_5_1");
}

void UnlitObjectManager::AddObjectToResource(Microsoft::WRL::ComPtr<ID3D12Device> device, FrameResource* currFrameResource)
{
}

int UnlitObjectManager::AddRenderItem(ID3D12Device* device, ModelData&& modelData)
{
	auto renderItem = std::make_unique<UnlitRenderItem>();
	renderItem->Uid = FrameResource::StaticObjectCount++;
	const std::string& itemName = modelData.CroppedName;
	const std::string name(itemName.begin(), itemName.end());

	renderItem->Name = name;
	renderItem->NameCount = _objectCounters[itemName]++;
	renderItem->Geo = &GeometryManager::Geometries()["shapeGeo"];
	renderItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;

	for (int i = 0; i < FrameResource::FrameResources().size(); ++i)
	{
		AddObjectToResource(device, FrameResource::FrameResources()[i].get());
	}

	_objects.push_back(std::move(renderItem));

	UploadManager::ExecuteUploadCommandList();

	return static_cast<int>(_objects.size()) - 1;
}

bool UnlitObjectManager::DeleteObject(const int selectedObject)
{
	const std::string name = _objects[selectedObject]->Name;
	_objectLoaded[name]--;

	//need for deleting from frame resource
	std::uint32_t uid = _objects[selectedObject]->Uid;

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

int UnlitObjectManager::ObjectsCount()
{
	return static_cast<int>(_objects.size());
}

std::string UnlitObjectManager::ObjectName(const int i)
{
	return _objects[i]->Name +
		(_objects[i]->NameCount == 0 ? "" : std::to_string(_objects[i]->NameCount));
}

void UnlitObjectManager::Draw(ID3D12GraphicsCommandList* cmdList, const FrameResource* currFrameResource, float screenHeight, bool isWireframe) const
{
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());
	cmdList->SetPipelineState(_pso.Get());

	const auto passCb = currFrameResource->GBufferPassCb->Resource();
	cmdList->SetGraphicsRootConstantBufferView(1, passCb->GetGPUVirtualAddress());

	// For each render item...
	for (size_t i = 0; i < _objects.size(); i++)
	{
		const auto& ri = _objects[i];
		const auto objectCb = currFrameResource->StaticObjCb->Resource();

		const auto geo = ri->Geo->begin()->get();
		const auto vertexBuffer = geo->VertexBufferView();
		cmdList->IASetVertexBuffers(0, 1, &vertexBuffer);
		const auto& indexBuffer = geo->IndexBufferView();
		cmdList->IASetIndexBuffer(&indexBuffer);
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		const D3D12_GPU_VIRTUAL_ADDRESS objCbAddress = objectCb->GetGPUVirtualAddress() + ri->Uid * _cbSize;

		cmdList->SetGraphicsRootConstantBufferView(0, objCbAddress);

		cmdList->DrawIndexedInstanced(geo->DrawArgs["grid"].IndexCount, 1, 0, 0, 0);
	}
}

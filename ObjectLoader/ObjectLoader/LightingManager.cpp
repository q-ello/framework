#include "LightingManager.h"

using namespace Microsoft::WRL;

LightingManager::LightingManager()
{
	
}

LightingManager::~LightingManager()
{
}

void LightingManager::addLight(ID3D12Device* device)
{
}

void LightingManager::deleteLight(int deletedLight)
{
}

void LightingManager::UpdateDirectionalLightCB(FrameResource* currFrameResource)
{
	_dirLightCB.mainLightIsOn = _isMainLightOn;
	_dirLightCB.gLightColor = DirectX::XMFLOAT3(_dirLightColor[0], _dirLightColor[1], _dirLightColor[2]);

	DirectX::XMVECTOR direction = DirectX::XMVectorSet(_mainLightDirection[0], _mainLightDirection[1], _mainLightDirection[2], 0);
	direction = DirectX::XMVector3Normalize(direction);
	DirectX::XMStoreFloat3(&_dirLightCB.mainLightDirection, direction);

	auto currDirLightCB = currFrameResource->DirLightCB.get();
	currDirLightCB->CopyData(0, _dirLightCB);
}

void LightingManager::UpdateLightCBs(FrameResource* currFrameResource)
{
}

void LightingManager::AddLightToResource(Microsoft::WRL::ComPtr<ID3D12Device> device, FrameResource* currFrameResource)
{
}

int LightingManager::lightsCount()
{
	return 0;
}

Light* LightingManager::light(int i)
{
	return nullptr;
}

void LightingManager::Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, D3D12_GPU_DESCRIPTOR_HANDLE descTable)
{
	// Indicate a state transition on the resource usage.
	
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	cmdList->SetGraphicsRootDescriptorTable(0, descTable);
	cmdList->SetPipelineState(_dirLightPSO.Get());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	auto lightingPassCB = currFrameResource->LightingPassCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(1, lightingPassCB->GetGPUVirtualAddress());

	auto dirLightCB = currFrameResource->DirLightCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(2, dirLightCB->GetGPUVirtualAddress());

	cmdList->DrawInstanced( 3, 1, 0, 0);
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

	CD3DX12_ROOT_PARAMETER lightingSlotRootParameter[3];

	lightingSlotRootParameter[0].InitAsDescriptorTable(1, &lightingRange);
	lightingSlotRootParameter[1].InitAsConstantBufferView(0);
	lightingSlotRootParameter[2].InitAsConstantBufferView(1);

	CD3DX12_ROOT_SIGNATURE_DESC lightingRootSigDesc(3, lightingSlotRootParameter);

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
	_dirLightVSShader = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "LightingVS", "vs_5_1");
	_dirLightPSShader = d3dUtil::CompileShader(L"Shaders\\Lighting.hlsl", nullptr, "LightingPS", "ps_5_1");
}

void LightingManager::BuildPSO()
{
	//lighting pso
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
	ThrowIfFailed(UploadManager::device ->CreateGraphicsPipelineState(&lightingPSODesc, IID_PPV_ARGS(&_dirLightPSO)));
}

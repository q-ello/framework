#include "TAAManager.h"

#include "../Helpers/DescriptorHeapAllocator.h"

void TaaManager::Init(const int width, const int height)
{
	_width = width;
	_height = height;
	// Allocate descriptors
	for (int i = 0; i < 2; i++)
	{
		_historyTextures[i].OtherIndex = TextureManager::RtvHeapAllocator->Allocate();
		_historyTextures[i].SrvIndex = TextureManager::SrvHeapAllocator->Allocate();
	}
	
	BuildRootSignature();
	BuildShaders();
	BuildPso();
	BuildTextures();
}

void TaaManager::BindToManagers(LightingManager* lightingManager, GBuffer* buffer, Camera* camera)
{
	_lightingManager = lightingManager;
	_fullscreenVs = _lightingManager->GetFullScreenVs();
	_gBuffer = buffer;
	_camera = camera;
}


void TaaManager::BuildShaders()
{
	_ps = d3dUtil::CompileShader(L"Shaders\\TAA.hlsl", nullptr, "TAAPS", "ps_5_1");
}

void TaaManager::BuildPso()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.InputLayout = { nullptr, 0 };
	psoDesc.pRootSignature = _rootSignature.Get();
	psoDesc.VS = { static_cast<BYTE*>(_fullscreenVs->GetBufferPointer()), _fullscreenVs->GetBufferSize() };
	psoDesc.PS = { static_cast<BYTE*>(_ps->GetBufferPointer()), _ps->GetBufferSize() };
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = _format;
	psoDesc.SampleDesc.Count = 1;
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pso)));
}

void TaaManager::OnResize(const int newWidth, const int newHeight)
{
	_width = newWidth;
	_height = newHeight;
	BuildTextures();
}

void TaaManager::BuildRootSignature()
{
	{
		CD3DX12_DESCRIPTOR_RANGE litSceneTexTable;
		litSceneTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		CD3DX12_DESCRIPTOR_RANGE historyTexTable;
		historyTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
		CD3DX12_DESCRIPTOR_RANGE currentDepthTexTable;
		currentDepthTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
		CD3DX12_DESCRIPTOR_RANGE previousDepthTexTable;
		previousDepthTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
		CD3DX12_DESCRIPTOR_RANGE velocityTexTable;
		velocityTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);

		constexpr int rootParameterCount = 6;

		CD3DX12_ROOT_PARAMETER slotRootParameter[rootParameterCount];

		slotRootParameter[0].InitAsDescriptorTable(1, &litSceneTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[1].InitAsDescriptorTable(1, &historyTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[2].InitAsDescriptorTable(1, &currentDepthTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[3].InitAsDescriptorTable(1, &previousDepthTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[4].InitAsConstants(2, 0);
		slotRootParameter[5].InitAsDescriptorTable(1, &velocityTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
		
		const CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(rootParameterCount, slotRootParameter,
		                                              static_cast<UINT>(TextureManager::GetLinearSamplers().size()),
		                                              TextureManager::GetLinearSamplers().data(),
		                                              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
		Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
		const HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
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
			IID_PPV_ARGS(_rootSignature.GetAddressOf())));
	}
}

void TaaManager::BuildTextures()
{
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = _width;
	texDesc.Height = _height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = _format;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = _format;
	clearValue.Color[0] = 0.0f;
	clearValue.Color[1] = 0.0f;
	clearValue.Color[2] = 0.0f;
	clearValue.Color[3] = 0.0f;

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

	// Create RTV
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;
	rtvDesc.Format = _format;

	//create SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = _format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	for (int i = 0; i < 2; i++)
	{
		ThrowIfFailed(_device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, i == 0 ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_RENDER_TARGET,
			&clearValue, IID_PPV_ARGS(&_historyTextures[i].Resource)));
		_historyTextures[i].PrevState = i == 0 ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_RENDER_TARGET;

		_device->CreateRenderTargetView(_historyTextures[i].Resource.Get(), &rtvDesc,
			TextureManager::RtvHeapAllocator->GetCpuHandle(_historyTextures[i].OtherIndex));

		_device->CreateShaderResourceView(_historyTextures[i].Resource.Get(), &srvDesc,
			TextureManager::SrvHeapAllocator->GetCpuHandle(_historyTextures[i].SrvIndex));
	}
}

void TaaManager::ChangeHistoryState(ID3D12GraphicsCommandList4* cmdList, const int index, const D3D12_RESOURCE_STATES newState)
{
	RtvSrvTexture& texture = _historyTextures[index];
	BasicUtil::ChangeTextureState(cmdList, texture, newState);
}

void TaaManager::ApplyTaa(ID3D12GraphicsCommandList4* cmdList, const FrameResource* currFrameResource)
{
	_gBuffer->ChangeBothDepthState(D3D12_RESOURCE_STATE_GENERIC_READ);
	
	const D3D12_CPU_DESCRIPTOR_HANDLE destHandle = TextureManager::RtvHeapAllocator->GetCpuHandle(_historyTextures[1 - _currentHistoryIndex].OtherIndex);
	cmdList->OMSetRenderTargets(1, &destHandle, true, nullptr);
	cmdList->SetPipelineState(_pso.Get());
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	//setting up data and drawing
	
	const float constants[] = {static_cast<float>(_width), static_cast<float>(_height)};
	const auto& allocator = TextureManager::SrvHeapAllocator;
	cmdList->SetGraphicsRootDescriptorTable(0, _lightingManager->GetFinalTextureSrv());
	cmdList->SetGraphicsRootDescriptorTable(1, 
		allocator->GetGpuHandle(_historyTextures[_currentHistoryIndex].SrvIndex));
	cmdList->SetGraphicsRootDescriptorTable(2, _gBuffer->GetGBufferDepthSrv(true));
	cmdList->SetGraphicsRootDescriptorTable(3, _gBuffer->GetGBufferDepthSrv(false));
	cmdList->SetGraphicsRoot32BitConstants(4, 2, &constants, 0);
	cmdList->SetGraphicsRootDescriptorTable(5, _gBuffer->GetGBufferTextureSrv(GBufferInfo::Velocity));
	
	cmdList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(3, 1, 0, 0);

	//swapping textures
	_currentHistoryIndex = (_currentHistoryIndex + 1) % 2;
	ChangeHistoryState(cmdList, _currentHistoryIndex, D3D12_RESOURCE_STATE_GENERIC_READ);
	ChangeHistoryState(cmdList, 1 - _currentHistoryIndex, D3D12_RESOURCE_STATE_RENDER_TARGET);

	_lightingManager->SetFinalTextureIndex(_historyTextures[_currentHistoryIndex].SrvIndex);
}

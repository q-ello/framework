#include "TAAManager.h"

TAAManager::~TAAManager()
{
}

void TAAManager::Init(int width, int height)
{
	_width = width;
	_height = height;
	// Allocate descriptors
	for (int i = 0; i < 2; i++)
	{
		_historyTextures[i].otherIndex = TextureManager::rtvHeapAllocator->Allocate();
		_historyTextures[i].SrvIndex = TextureManager::srvHeapAllocator->Allocate();
		_historyVelocities[i].otherIndex = TextureManager::rtvHeapAllocator->Allocate();
		_historyVelocities[i].SrvIndex = TextureManager::srvHeapAllocator->Allocate();
	}
	BuildRootSignature();
	BuildShaders();
	BuildPSO();
	BuildTextures();
}

void TAAManager::BindToManagers(LightingManager* lightingManager, GBuffer* gBuffer, Camera* camera)
{
	_lightingManager = lightingManager;
	_fullscreenVS = _lightingManager->GetFullScreenVS();
	_gBuffer = gBuffer;
	_camera = camera;
}


void TAAManager::BuildShaders()
{
	_PS = d3dUtil::CompileShader(L"Shaders\\TAA.hlsl", nullptr, "TAAPS", "ps_5_1");
}

void TAAManager::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.InputLayout = { nullptr, 0 };
	psoDesc.pRootSignature = _rootSignature.Get();
	psoDesc.VS = { reinterpret_cast<BYTE*>(_fullscreenVS->GetBufferPointer()), _fullscreenVS->GetBufferSize() };
	psoDesc.PS = { reinterpret_cast<BYTE*>(_PS->GetBufferPointer()), _PS->GetBufferSize() };
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = _format;
	psoDesc.SampleDesc.Count = 1;
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_PSO)));
}

void TAAManager::OnResize(int newWidth, int newHeight)
{
	_width = newWidth;
	_height = newHeight;
	BuildTextures();
}

void TAAManager::UpdateTAAParameters(FrameResource* currFrame, DirectX::XMFLOAT4X4 PrevViewProj, DirectX::XMFLOAT4X4 CurrInvViewProj)
{
	//we'll just call this before updating it in lighting pass
	TAAConstants taaParams;
	taaParams.PrevViewProj = PrevViewProj;
	taaParams.CurrInvViewProj = CurrInvViewProj;
	taaParams.ScreenSize = { (float)_width, (float)_height };
	currFrame->TAACB->CopyData(0, taaParams);
}

void TAAManager::BuildRootSignature()
{
	{
		CD3DX12_DESCRIPTOR_RANGE litSceneTexTable;
		litSceneTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		CD3DX12_DESCRIPTOR_RANGE historyTexTable;
		historyTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
		CD3DX12_DESCRIPTOR_RANGE depthTexTable;
		depthTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);

		const int rootParameterCount = 4;

		CD3DX12_ROOT_PARAMETER slotRootParameter[rootParameterCount];

		slotRootParameter[0].InitAsDescriptorTable(1, &litSceneTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[1].InitAsDescriptorTable(1, &historyTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[2].InitAsDescriptorTable(1, &depthTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[3].InitAsConstantBufferView(0, 1);

		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(rootParameterCount, slotRootParameter, TextureManager::GetLinearSamplers().size(), TextureManager::GetLinearSamplers().data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
		Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
			serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

		if (errorBlob != nullptr)
		{
			::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
		}
		ThrowIfFailed(hr);

		ThrowIfFailed(_device->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(_rootSignature.GetAddressOf())));
	}
}



void TAAManager::BuildTextures()
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
		_historyTextures[i].prevState = i == 0 ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_RENDER_TARGET;
		ThrowIfFailed(_device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
			&clearValue, IID_PPV_ARGS(&_historyVelocities[i].Resource)));

		_device->CreateRenderTargetView(_historyTextures[i].Resource.Get(), &rtvDesc,
			TextureManager::rtvHeapAllocator->GetCpuHandle(_historyTextures[i].otherIndex));
		_device->CreateRenderTargetView(_historyVelocities[i].Resource.Get(), &rtvDesc,
			TextureManager::rtvHeapAllocator->GetCpuHandle(_historyVelocities[i].otherIndex));

		_device->CreateShaderResourceView(_historyTextures[i].Resource.Get(), &srvDesc,
			TextureManager::srvHeapAllocator->GetCpuHandle(_historyTextures[i].SrvIndex));
		_device->CreateShaderResourceView(_historyVelocities[i].Resource.Get(), &srvDesc,
			TextureManager::srvHeapAllocator->GetCpuHandle(_historyVelocities[i].SrvIndex));
	}
}

void TAAManager::ChangeHistoryState(ID3D12GraphicsCommandList* cmdList, int index, D3D12_RESOURCE_STATES newState)
{
	RtvSrvTexture& texture = _historyTextures[index];
	if (texture.prevState == newState)
		return;

	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		texture.Resource.Get(),
		texture.prevState, newState));
	texture.prevState = newState;
}


	
void TAAManager::ApplyTAA(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, bool taaEnabled)
{
	if (!taaEnabled)
	{
		_historyValid = false;
		return;
	}

    if (!_historyValid)
    {
        _historyValid = true;

		ChangeHistoryState(cmdList, _currentHistoryIndex, D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_TEXTURE_COPY_LOCATION destLocation = {};
        destLocation.pResource = _historyTextures[_currentHistoryIndex].Resource.Get();
        destLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destLocation.SubresourceIndex = 0;

		_lightingManager->ChangeMiddlewareState(cmdList, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
        srcLocation.pResource = _lightingManager->GetMiddlewareTexture();
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLocation.SubresourceIndex = 0;

		//copying texture for first frame while we don't have history
        cmdList->CopyTextureRegion(&destLocation, 0, 0, 0, &srcLocation, nullptr);

		ChangeHistoryState(cmdList, _currentHistoryIndex, D3D12_RESOURCE_STATE_GENERIC_READ);
    }

	cmdList->OMSetRenderTargets(1, &TextureManager::rtvHeapAllocator->GetCpuHandle(_historyTextures[1 - _currentHistoryIndex].otherIndex), true, nullptr);
	cmdList->SetPipelineState(_PSO.Get());
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	//setting up data and drawing
	cmdList->SetGraphicsRootDescriptorTable(0, _lightingManager->GetFinalTextureSRV());
	cmdList->SetGraphicsRootDescriptorTable(1, 
		TextureManager::srvHeapAllocator->GetGpuHandle(_historyTextures[_currentHistoryIndex].SrvIndex));
	cmdList->SetGraphicsRootDescriptorTable(2, _gBuffer->GetDepthSRV());
	cmdList->SetGraphicsRootConstantBufferView(3, currFrameResource->TAACB->Resource()->GetGPUVirtualAddress());
	
	cmdList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(3, 1, 0, 0);

	//swapping textures
	_currentHistoryIndex = (_currentHistoryIndex + 1) % 2;
	ChangeHistoryState(cmdList, _currentHistoryIndex, D3D12_RESOURCE_STATE_GENERIC_READ);
	ChangeHistoryState(cmdList, 1 - _currentHistoryIndex, D3D12_RESOURCE_STATE_RENDER_TARGET);

	_lightingManager->SetFinalTextureIndex(_historyTextures[_currentHistoryIndex].SrvIndex);
}
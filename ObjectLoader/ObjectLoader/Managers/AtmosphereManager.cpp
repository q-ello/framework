#include "AtmosphereManager.h"

#include "LightingManager.h"

void AtmosphereManager::Init(const int width, const int height)
{
    BuildRootSignature();
    BuildShaders();
    BuildPso();
	
	_middlewareTexture.OtherIndex = TextureManager::RtvHeapAllocator->Allocate();
	_middlewareTexture.SrvIndex = TextureManager::SrvHeapAllocator->Allocate();
	
	OnResize(width, height);
}

void AtmosphereManager::BindToManagers(LightingManager* lightingManager, GBuffer* buffer)
{
    _lightingManager = lightingManager;
    _gBuffer = buffer;
    
    _fullscreenVs = _lightingManager->GetFullScreenVs();
	const float* lightDir = _lightingManager->MainLightDirection();
	Parameters.DirToSun = {-lightDir[0], -lightDir[1], -lightDir[2]};
}

void AtmosphereManager::Draw(ID3D12GraphicsCommandList* cmdList, const FrameResource* currFrameResource)
{
	//just in case
	_lightingManager->ChangeMiddlewareState(cmdList, D3D12_RESOURCE_STATE_GENERIC_READ);
	_gBuffer->ChangeDsvState(D3D12_RESOURCE_STATE_GENERIC_READ);

	BasicUtil::ChangeTextureState(cmdList, _middlewareTexture, D3D12_RESOURCE_STATE_RENDER_TARGET);
	
	const D3D12_CPU_DESCRIPTOR_HANDLE destHandle = TextureManager::RtvHeapAllocator->GetCpuHandle(_middlewareTexture.OtherIndex);
	cmdList->OMSetRenderTargets(1, &destHandle, true, nullptr);
	cmdList->SetPipelineState(_pso.Get());
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	//setting up data and drawing
	cmdList->SetGraphicsRootDescriptorTable(0, _lightingManager->GetFinalTextureSrv());
	cmdList->SetGraphicsRootDescriptorTable(1, _gBuffer->GetGBufferDepthSrv(true));
	cmdList->SetGraphicsRootConstantBufferView(2, currFrameResource->LightingPassCb->Resource()->GetGPUVirtualAddress());
	cmdList->SetGraphicsRootConstantBufferView(3, currFrameResource->AtmosphereCb->Resource()->GetGPUVirtualAddress());
	
	cmdList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(3, 1, 0, 0);
	
	BasicUtil::ChangeTextureState(cmdList, _middlewareTexture, D3D12_RESOURCE_STATE_GENERIC_READ);
	_lightingManager->SetFinalTextureIndex(_middlewareTexture.SrvIndex);
	
}

void AtmosphereManager::UpdateParameters(const FrameResource* currFrame)
{
    if (_framesDirty > 0)
    {
        currFrame->AtmosphereCb->CopyData(0, Parameters);
        _framesDirty--;
    }
}

void AtmosphereManager::SetDirty()
{
	_framesDirty = gNumFrameResources;
}

void AtmosphereManager::OnResize(const int width, const int height)
{
	BuildTexture(width, height);
}

void AtmosphereManager::BuildTexture(int width, int height)
{
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = width;
	texDesc.Height = height;
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

	ThrowIfFailed(_device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_RENDER_TARGET,
			&clearValue, IID_PPV_ARGS(&_middlewareTexture.Resource)));
	_middlewareTexture.PrevState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	_device->CreateRenderTargetView(_middlewareTexture.Resource.Get(), &rtvDesc,
			TextureManager::RtvHeapAllocator->GetCpuHandle(_middlewareTexture.OtherIndex));

	_device->CreateShaderResourceView(_middlewareTexture.Resource.Get(), &srvDesc,
		TextureManager::SrvHeapAllocator->GetCpuHandle(_middlewareTexture.SrvIndex));
}

void AtmosphereManager::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE litSceneTexTable;
	litSceneTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	CD3DX12_DESCRIPTOR_RANGE depthTexTable;
	depthTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

	constexpr int rootParameterCount = 4;

	CD3DX12_ROOT_PARAMETER slotRootParameter[rootParameterCount];

	slotRootParameter[0].InitAsDescriptorTable(1, &litSceneTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsDescriptorTable(1, &depthTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[2].InitAsConstantBufferView(0);
	slotRootParameter[3].InitAsConstantBufferView(1);

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

void AtmosphereManager::BuildShaders()
{
	_ps = d3dUtil::CompileShader(L"Shaders\\Atmosphere.hlsl", nullptr, "PS", "ps_5_1");
}

void AtmosphereManager::BuildPso()
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
	psoDesc.DepthStencilState.DepthEnable = false;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    ThrowIfFailed(_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pso)));
}

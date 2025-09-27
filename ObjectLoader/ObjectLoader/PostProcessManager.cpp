#include "PostProcessManager.h"

using namespace Microsoft::WRL;

void PostProcessManager::Init(int width, int height)
{
	SetNewResolutionAndRects(width, height);

	//get srv and rtv indices
	_lightOcclusionMask.otherIndex = TextureManager::rtvHeapAllocator->Allocate();
	_lightOcclusionMask.SrvIndex = TextureManager::srvHeapAllocator->Allocate();

	BuildRootSignature();
	BuildShaders();
	BuildPSOs();
	BuildTextures();
}

void PostProcessManager::BindToManagers(GBuffer* gbuffer, LightingManager* lightingManager)
{
	_gBuffer = gbuffer;
	_lightingManager = lightingManager;

	_fullscreenVS = _lightingManager->GetFullScreenVS();
}

void PostProcessManager::DrawPass(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, D3D12_CPU_DESCRIPTOR_HANDLE backBuffer)
{
	if (!*_lightingManager->IsMainLightOn())
		return;

	OcclusionMaskPass(cmdList, currFrameResource);
	GodRaysPass(cmdList, currFrameResource, backBuffer);
}

void PostProcessManager::OcclusionMaskPass(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource)
{
	//occlusion mask to rtv
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(_lightOcclusionMask.Resource.Get(),
		_lightOcclusionMask.prevState, D3D12_RESOURCE_STATE_RENDER_TARGET));
	_lightOcclusionMask.prevState = D3D12_RESOURCE_STATE_RENDER_TARGET;

	cmdList->RSSetViewports(1, &_occlusionMaskViewport);
	cmdList->RSSetScissorRects(1, &_occlusionMaskScissorRect);

	auto lomRtv = TextureManager::rtvHeapAllocator->GetCpuHandle(_lightOcclusionMask.otherIndex);

	//light occlusion mask pass
	cmdList->SetPipelineState(_lightOcclusionMaskPSO.Get());
	cmdList->SetGraphicsRootSignature(_lightOcclusionMaskRootSignature.Get());
	cmdList->SetGraphicsRootDescriptorTable(0, _gBuffer->GetDepthSRV());
	cmdList->SetGraphicsRootDescriptorTable(1, _lightingManager->GetCascadeShadowSRV()); //cascades shadow map

	auto lightingPassCB = currFrameResource->LightingPassCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(2, lightingPassCB->GetGPUVirtualAddress());

	auto dirLightCB = currFrameResource->DirLightCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(3, dirLightCB->GetGPUVirtualAddress());

	FLOAT clearColor[4] = { 0.f, 0.f, 0.f, 0.f };
	cmdList->ClearRenderTargetView(lomRtv, clearColor, 0, nullptr);
	cmdList->OMSetRenderTargets(1, &lomRtv, true, nullptr);
	cmdList->DrawInstanced(3, 1, 0, 0);

	//occlusion mask to srv
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(_lightOcclusionMask.Resource.Get(),
		_lightOcclusionMask.prevState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	_lightOcclusionMask.prevState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void PostProcessManager::GodRaysPass(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, D3D12_CPU_DESCRIPTOR_HANDLE backBuffer)
{
	cmdList->RSSetViewports(1, &_viewport);
	cmdList->RSSetScissorRects(1, &_scissorRect);

	cmdList->SetPipelineState(_godRaysPSO.Get());
	cmdList->SetGraphicsRootSignature(_godRaysRootSignature.Get());
	cmdList->SetGraphicsRootDescriptorTable(0, TextureManager::srvHeapAllocator->GetGpuHandle(_lightOcclusionMask.SrvIndex));

	auto lightingPassCB = currFrameResource->LightingPassCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(1, lightingPassCB->GetGPUVirtualAddress());

	auto dirLightCB = currFrameResource->DirLightCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(2, dirLightCB->GetGPUVirtualAddress());

	auto godRaysCB = currFrameResource->GodRaysCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(3, godRaysCB->GetGPUVirtualAddress());

	cmdList->OMSetRenderTargets(1, &backBuffer, true, nullptr);
	cmdList->DrawInstanced(3, 1, 0, 0);
}

void PostProcessManager::OnResize(int newWidth, int newHeight)
{
	SetNewResolutionAndRects(newWidth, newHeight);

	BuildTextures();
}

void PostProcessManager::Update()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		FrameResource::frameResources()[i]->GodRaysCB->CopyData(0, GodRaysParameters);
	}
}

void PostProcessManager::BuildRootSignature()
{
	//mask occlusion root signature
	CD3DX12_DESCRIPTOR_RANGE depthTexTable;
	depthTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	//cascades shadow map
	CD3DX12_DESCRIPTOR_RANGE cascadesShadowTexTable;
	cascadesShadowTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

	const int rootParameterCount = 4;

	CD3DX12_ROOT_PARAMETER lightingSlotRootParameter[rootParameterCount];

	lightingSlotRootParameter[0].InitAsDescriptorTable(1, &depthTexTable);
	lightingSlotRootParameter[1].InitAsDescriptorTable(1, &cascadesShadowTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	lightingSlotRootParameter[2].InitAsConstantBufferView(0);
	lightingSlotRootParameter[3].InitAsConstantBufferView(1);

	CD3DX12_ROOT_SIGNATURE_DESC lightingRootSigDesc(rootParameterCount, lightingSlotRootParameter, TextureManager::GetShadowSamplers().size(), TextureManager::GetShadowSamplers().data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&lightingRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
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
		IID_PPV_ARGS(_lightOcclusionMaskRootSignature.GetAddressOf())));

	//god rays root signature
	CD3DX12_DESCRIPTOR_RANGE lightOcclusionMaskTexTable;
	lightOcclusionMaskTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	const int GRrootParameterCount = 4;

	CD3DX12_ROOT_PARAMETER godRaysSlotRootParameter[GRrootParameterCount];

	godRaysSlotRootParameter[0].InitAsDescriptorTable(1, &lightOcclusionMaskTexTable);
	godRaysSlotRootParameter[1].InitAsConstantBufferView(0);
	godRaysSlotRootParameter[2].InitAsConstantBufferView(1);
	godRaysSlotRootParameter[3].InitAsConstantBufferView(2);

	CD3DX12_ROOT_SIGNATURE_DESC godRaysRootSigDesc(GRrootParameterCount, godRaysSlotRootParameter, TextureManager::GetShadowSamplers().size(), TextureManager::GetShadowSamplers().data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	serializedRootSig = nullptr;
	errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&godRaysRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
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
		IID_PPV_ARGS(_godRaysRootSignature.GetAddressOf())));
}

void PostProcessManager::BuildShaders()
{ 
	_lightOcclusionMaskPS = d3dUtil::CompileShader(L"Shaders\\LightOcclusionMask.hlsl", nullptr, "LightOcclusionPS", "ps_5_1");
	_godRaysPS = d3dUtil::CompileShader(L"Shaders\\GodRays.hlsl", nullptr, "GodRaysPS", "ps_5_1");
}

void PostProcessManager::BuildPSOs()
{ 
	D3D12_GRAPHICS_PIPELINE_STATE_DESC lightOcclusionMaskPsoDesc;
	ZeroMemory(&lightOcclusionMaskPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	lightOcclusionMaskPsoDesc.InputLayout = { nullptr, 0 };
	lightOcclusionMaskPsoDesc.pRootSignature = _lightOcclusionMaskRootSignature.Get();
	lightOcclusionMaskPsoDesc.VS = 
	{
	reinterpret_cast<BYTE*>(_fullscreenVS->GetBufferPointer()),
	_fullscreenVS->GetBufferSize()
	};
	lightOcclusionMaskPsoDesc.PS = 
	{
		reinterpret_cast<BYTE*>(_lightOcclusionMaskPS->GetBufferPointer()),
		_lightOcclusionMaskPS->GetBufferSize()
	};
	lightOcclusionMaskPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	lightOcclusionMaskPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	lightOcclusionMaskPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	lightOcclusionMaskPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	lightOcclusionMaskPsoDesc.DepthStencilState.DepthEnable = FALSE;
	lightOcclusionMaskPsoDesc.DepthStencilState.StencilEnable = FALSE;
	lightOcclusionMaskPsoDesc.SampleMask = UINT_MAX;
	lightOcclusionMaskPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	lightOcclusionMaskPsoDesc.NumRenderTargets = 1;
	lightOcclusionMaskPsoDesc.RTVFormats[0] = _lightOcclusionMaskFormat;
	lightOcclusionMaskPsoDesc.SampleDesc.Count = 1;
	lightOcclusionMaskPsoDesc.SampleDesc.Quality = 0;
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&lightOcclusionMaskPsoDesc, IID_PPV_ARGS(&_lightOcclusionMaskPSO)));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC godRaysPsoDesc = lightOcclusionMaskPsoDesc;
	godRaysPsoDesc.pRootSignature = _godRaysRootSignature.Get();
	godRaysPsoDesc.PS = 
	{
		reinterpret_cast<BYTE*>(_godRaysPS->GetBufferPointer()),
		_godRaysPS->GetBufferSize()
	};
	godRaysPsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	godRaysPsoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	godRaysPsoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	godRaysPsoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	godRaysPsoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	godRaysPsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	ThrowIfFailed(_device->CreateGraphicsPipelineState(&godRaysPsoDesc, IID_PPV_ARGS(&_godRaysPSO)));
}

void PostProcessManager::BuildTextures()
{ 
	//creating occlusion mask texture
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = _lightOcclusionMaskWidth;
	texDesc.Height = _lightOcclusionMaskHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = _lightOcclusionMaskFormat;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = _lightOcclusionMaskFormat;
	clearValue.Color[0] = 0.0f;
	clearValue.Color[1] = 0.0f;
	clearValue.Color[2] = 0.0f;
	clearValue.Color[3] = 0.0f;

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

	ThrowIfFailed(_device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COMMON,
		&clearValue, IID_PPV_ARGS(&_lightOcclusionMask.Resource)));

	// Create RTV
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;
	rtvDesc.Format = _lightOcclusionMaskFormat;

	_device->CreateRenderTargetView(_lightOcclusionMask.Resource.Get(), &rtvDesc, TextureManager::rtvHeapAllocator->GetCpuHandle(_lightOcclusionMask.otherIndex));

	//create SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = _lightOcclusionMaskFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	D3D12_VIEWPORT _occlusionMaskViewport{ 0, 0, 0, 0, 0, 1 };
	D3D12_RECT _occlusionMaskScissorRect{ 0, 0, 0, 0 };

	_device->CreateShaderResourceView(_lightOcclusionMask.Resource.Get(), &srvDesc, 
		TextureManager::srvHeapAllocator->GetCpuHandle(_lightOcclusionMask.SrvIndex));
}

void PostProcessManager::SetNewResolutionAndRects(int newWidth, int newHeight)
{
	_width = newWidth;
	_height = newHeight;
	_viewport.Height = static_cast<float>(_height);
	_viewport.Width = static_cast<float>(_width);
	_scissorRect.right = _width;
	_scissorRect.bottom = _height;

	_lightOcclusionMaskWidth = _width / 2;
	_lightOcclusionMaskHeight = _height / 2;
	_occlusionMaskViewport.Height = static_cast<float>(_lightOcclusionMaskHeight);
	_occlusionMaskViewport.Width = static_cast<float>(_lightOcclusionMaskWidth);
	_occlusionMaskScissorRect.right = _lightOcclusionMaskWidth;
	_occlusionMaskScissorRect.bottom = _lightOcclusionMaskHeight;
}

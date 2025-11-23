#include "PostProcessManager.h"

using namespace Microsoft::WRL;

void PostProcessManager::Init(int width, int height)
{
	SetNewResolutionAndRects(width, height);

	//get srv and rtv indices
	{
		auto& rtvAllocator = TextureManager::rtvHeapAllocator;
		auto& srvAllocator = TextureManager::srvHeapAllocator;\

		_lightOcclusionMask.otherIndex = rtvAllocator->Allocate();
		_lightOcclusionMask.SrvIndex = srvAllocator->Allocate();

		_ssrTexture.otherIndex = rtvAllocator->Allocate();
		_ssrTexture.SrvIndex = srvAllocator->Allocate();

		_chromaticAberrationTexture.otherIndex = rtvAllocator->Allocate();
		_chromaticAberrationTexture.SrvIndex = srvAllocator->Allocate();

		_vignettingTexture.otherIndex = rtvAllocator->Allocate();
		_vignettingTexture.SrvIndex = srvAllocator->Allocate();
	}

	BuildRootSignature();
	BuildShaders();
	BuildPSOs();
	BuildTextures();
}

void PostProcessManager::BindToManagers(GBuffer* gbuffer, LightingManager* lightingManager, Camera* camera)
{
	_gBuffer = gbuffer;
	_lightingManager = lightingManager;
	_camera = camera;

	_fullscreenLightVS = _lightingManager->GetFullScreenVSWithSamplers();
	_fullscreenVS = _lightingManager->GetFullScreenVS();
}

void PostProcessManager::DrawGodRaysPass(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource)
{
	if (!*_lightingManager->IsMainLightOn())
		return;

	OcclusionMaskPass(cmdList, currFrameResource);
	GodRaysPass(cmdList, currFrameResource);
}

void PostProcessManager::OcclusionMaskPass(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource)
{
	//occlusion mask to rtv
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(_lightOcclusionMask.Resource.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));

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
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
}

void PostProcessManager::GodRaysPass(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource)
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

	cmdList->OMSetRenderTargets(1, &_lightingManager->GetMiddlewareRTV(), true, nullptr);
	cmdList->DrawInstanced(3, 1, 0, 0);
}

void PostProcessManager::DrawSSR(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource)
{
	cmdList->SetPipelineState(_ssrPSO.Get());
	cmdList->SetGraphicsRootSignature(_ssrRootSignature.Get());

	_lightingManager->ChangeMiddlewareState(cmdList, D3D12_RESOURCE_STATE_GENERIC_READ);
	cmdList->SetGraphicsRootDescriptorTable(0, _lightingManager->GetFinalTextureSRV());

	cmdList->SetGraphicsRootDescriptorTable(1, _gBuffer->GetNormalSRV());
	
	auto lightingPassCB = currFrameResource->LightingPassCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(2, lightingPassCB->GetGPUVirtualAddress());

	auto ssrCB = currFrameResource->SSRCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(3, ssrCB->GetGPUVirtualAddress());

	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(_ssrTexture.Resource.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));

	cmdList->OMSetRenderTargets(1, &TextureManager::rtvHeapAllocator->GetCpuHandle(_ssrTexture.otherIndex), true, nullptr);
	cmdList->DrawInstanced(3, 1, 0, 0);

	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(_ssrTexture.Resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));

	_lightingManager->SetFinalTextureIndex(_ssrTexture.SrvIndex);
}

void PostProcessManager::DrawChromaticAberration(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource)
{
	//just in case
	_lightingManager->ChangeMiddlewareState(cmdList, D3D12_RESOURCE_STATE_GENERIC_READ);

	cmdList->SetPipelineState(_chromaticaAberrationPSO.Get());
	cmdList->SetGraphicsRootSignature(_chromaticAndVignettingRootSignature.Get());

	cmdList->SetGraphicsRootDescriptorTable(0, _lightingManager->GetFinalTextureSRV());

	auto lightingPassCB = currFrameResource->LightingPassCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(1, lightingPassCB->GetGPUVirtualAddress());

	cmdList->SetGraphicsRoot32BitConstants(2, 1, &ChromaticAberrationStrength, 0);

	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(_chromaticAberrationTexture.Resource.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));

	cmdList->OMSetRenderTargets(1, &TextureManager::rtvHeapAllocator->GetCpuHandle(_chromaticAberrationTexture.otherIndex), true, nullptr);
	cmdList->DrawInstanced(3, 1, 0, 0);

	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(_chromaticAberrationTexture.Resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));

	_lightingManager->SetFinalTextureIndex(_chromaticAberrationTexture.SrvIndex);
}

void PostProcessManager::DrawVignetting(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource)
{
	//just in case
	_lightingManager->ChangeMiddlewareState(cmdList, D3D12_RESOURCE_STATE_GENERIC_READ);

	cmdList->SetPipelineState(_vignettingPSO.Get());
	cmdList->SetGraphicsRootSignature(_chromaticAndVignettingRootSignature.Get());

	cmdList->SetGraphicsRootDescriptorTable(0, _lightingManager->GetFinalTextureSRV());

	auto lightingPassCB = currFrameResource->LightingPassCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(1, lightingPassCB->GetGPUVirtualAddress());

	cmdList->SetGraphicsRoot32BitConstants(2, 1, &VignettingPower, 0);

	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(_vignettingTexture.Resource.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));

	cmdList->OMSetRenderTargets(1, &TextureManager::rtvHeapAllocator->GetCpuHandle(_vignettingTexture.otherIndex), true, nullptr);
	cmdList->DrawInstanced(3, 1, 0, 0);

	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(_vignettingTexture.Resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));

	_lightingManager->SetFinalTextureIndex(_vignettingTexture.SrvIndex);
}

void PostProcessManager::OnResize(int newWidth, int newHeight)
{
	SetNewResolutionAndRects(newWidth, newHeight);
	BuildTextures();
}

void PostProcessManager::UpdateGodRaysParameters()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		FrameResource::frameResources()[i]->GodRaysCB->CopyData(0, GodRaysParameters);
	}
}

void PostProcessManager::UpdateSSRParameters(FrameResource* currFrame)
{
	//transpose right away to just store it into upload buffer
	//SSRParameters.InvProj = DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, _camera->GetProj()));
	SSRParameters.InvProj = DirectX::XMMatrixInverse(nullptr, _camera->GetProj());
	currFrame->SSRCB->CopyData(0, SSRParameters);
}

void PostProcessManager::BuildRootSignature()
{
	//mask occlusion root signature
	{
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

		CD3DX12_ROOT_SIGNATURE_DESC lightingRootSigDesc(rootParameterCount, lightingSlotRootParameter, (UINT)TextureManager::GetLinearSamplers().size(), TextureManager::GetLinearSamplers().data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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
	}
	//god rays root signature
	{
		CD3DX12_DESCRIPTOR_RANGE lightOcclusionMaskTexTable;
		lightOcclusionMaskTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

		const int GRrootParameterCount = 4;

		CD3DX12_ROOT_PARAMETER godRaysSlotRootParameter[GRrootParameterCount];

		godRaysSlotRootParameter[0].InitAsDescriptorTable(1, &lightOcclusionMaskTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
		godRaysSlotRootParameter[1].InitAsConstantBufferView(0);
		godRaysSlotRootParameter[2].InitAsConstantBufferView(1);
		godRaysSlotRootParameter[3].InitAsConstantBufferView(2);

		CD3DX12_ROOT_SIGNATURE_DESC godRaysRootSigDesc(GRrootParameterCount, godRaysSlotRootParameter, (UINT)TextureManager::GetLinearSamplers().size(), TextureManager::GetLinearSamplers().data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3D12SerializeRootSignature(&godRaysRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
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
	//ssr root signature
	{
		CD3DX12_DESCRIPTOR_RANGE litSceneTexTable;
		litSceneTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		CD3DX12_DESCRIPTOR_RANGE gbufferTexTable;
		gbufferTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1);

		const int ssrRootParameterCount = 4;

		CD3DX12_ROOT_PARAMETER ssrSlotRootParameter[ssrRootParameterCount];

		ssrSlotRootParameter[0].InitAsDescriptorTable(1, &litSceneTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
		ssrSlotRootParameter[1].InitAsDescriptorTable(1, &gbufferTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
		ssrSlotRootParameter[2].InitAsConstantBufferView(0);
		ssrSlotRootParameter[3].InitAsConstantBufferView(1);

		CD3DX12_ROOT_SIGNATURE_DESC ssrRootSigDesc(ssrRootParameterCount, ssrSlotRootParameter, TextureManager::GetLinearSamplers().size(), TextureManager::GetLinearSamplers().data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3D12SerializeRootSignature(&ssrRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
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
			IID_PPV_ARGS(_ssrRootSignature.GetAddressOf())));
	}
	//chromatic aberration root signature
	{
		CD3DX12_DESCRIPTOR_RANGE litSceneTexTable;
		litSceneTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

		const int chromaticAberrationRootParameterCount = 3;

		CD3DX12_ROOT_PARAMETER chromaticAberrationSlotRootParameter[chromaticAberrationRootParameterCount];

		chromaticAberrationSlotRootParameter[0].InitAsDescriptorTable(1, &litSceneTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
		chromaticAberrationSlotRootParameter[1].InitAsConstantBufferView(0);
		chromaticAberrationSlotRootParameter[2].InitAsConstants(1, 1);

		CD3DX12_ROOT_SIGNATURE_DESC chromaticAberrationRootSigDesc(chromaticAberrationRootParameterCount, chromaticAberrationSlotRootParameter, TextureManager::GetLinearSamplers().size(), TextureManager::GetLinearSamplers().data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3D12SerializeRootSignature(&chromaticAberrationRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
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
			IID_PPV_ARGS(_chromaticAndVignettingRootSignature.GetAddressOf())));
	}
}

void PostProcessManager::BuildShaders()
{ 
	_lightOcclusionMaskPS = d3dUtil::CompileShader(L"Shaders\\LightOcclusionMask.hlsl", nullptr, "LightOcclusionPS", "ps_5_1");
	_godRaysPS = d3dUtil::CompileShader(L"Shaders\\GodRays.hlsl", nullptr, "GodRaysPS", "ps_5_1");
	_ssrPS = d3dUtil::CompileShader(L"Shaders\\ScreenSpaceReflection.hlsl", nullptr, "PS", "ps_5_1");
	_chromaticAberrationPS = d3dUtil::CompileShader(L"Shaders\\ChromaticAndVignetting.hlsl", nullptr, "ChromaticPS", "ps_5_1");
	_vignettingPS = d3dUtil::CompileShader(L"Shaders\\ChromaticAndVignetting.hlsl", nullptr, "VignettingPS", "ps_5_1");
}

void PostProcessManager::BuildPSOs()
{ 
	D3D12_GRAPHICS_PIPELINE_STATE_DESC lightOcclusionMaskPsoDesc;
	ZeroMemory(&lightOcclusionMaskPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	lightOcclusionMaskPsoDesc.InputLayout = { nullptr, 0 };
	lightOcclusionMaskPsoDesc.pRootSignature = _lightOcclusionMaskRootSignature.Get();
	lightOcclusionMaskPsoDesc.VS = 
	{
	reinterpret_cast<BYTE*>(_fullscreenLightVS->GetBufferPointer()),
	_fullscreenLightVS->GetBufferSize()
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

	//god rays
	D3D12_GRAPHICS_PIPELINE_STATE_DESC godRaysPsoDesc = lightOcclusionMaskPsoDesc;
	godRaysPsoDesc.pRootSignature = _godRaysRootSignature.Get();
	godRaysPsoDesc.PS = 
	{
		reinterpret_cast<BYTE*>(_godRaysPS->GetBufferPointer()),
		_godRaysPS->GetBufferSize()
	};
	godRaysPsoDesc.RTVFormats[0] = _format;
	godRaysPsoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	godRaysPsoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	godRaysPsoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	godRaysPsoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	godRaysPsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	ThrowIfFailed(_device->CreateGraphicsPipelineState(&godRaysPsoDesc, IID_PPV_ARGS(&_godRaysPSO)));

	//ssr
	D3D12_GRAPHICS_PIPELINE_STATE_DESC ssrPSODesc = lightOcclusionMaskPsoDesc;
	ssrPSODesc.pRootSignature = _ssrRootSignature.Get();
	ssrPSODesc.VS =
	{
	reinterpret_cast<BYTE*>(_fullscreenVS->GetBufferPointer()),
	_fullscreenVS->GetBufferSize()
	};
	ssrPSODesc.PS =
	{
		reinterpret_cast<BYTE*>(_ssrPS->GetBufferPointer()),
		_ssrPS->GetBufferSize()
	};
	ssrPSODesc.RTVFormats[0] = _format;
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&ssrPSODesc, IID_PPV_ARGS(&_ssrPSO)));

	//chromatic aberration
	D3D12_GRAPHICS_PIPELINE_STATE_DESC chromaticAberrationPSODesc = ssrPSODesc;
	chromaticAberrationPSODesc.pRootSignature = _chromaticAndVignettingRootSignature.Get();
	chromaticAberrationPSODesc.PS =
	{
		reinterpret_cast<BYTE*>(_chromaticAberrationPS->GetBufferPointer()),
		_chromaticAberrationPS->GetBufferSize()
	};
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&chromaticAberrationPSODesc, IID_PPV_ARGS(&_chromaticaAberrationPSO)));

	//vignetting
	D3D12_GRAPHICS_PIPELINE_STATE_DESC vignettingPSODesc = chromaticAberrationPSODesc;
	vignettingPSODesc.PS =
	{
		reinterpret_cast<BYTE*>(_vignettingPS->GetBufferPointer()),
		_vignettingPS->GetBufferSize()
	};
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&vignettingPSODesc, IID_PPV_ARGS(&_vignettingPSO)));
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
		&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
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

	//most textures are kinda identical
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

		ThrowIfFailed(_device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
			&clearValue, IID_PPV_ARGS(&_ssrTexture.Resource)));

		ThrowIfFailed(_device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
			&clearValue, IID_PPV_ARGS(&_chromaticAberrationTexture.Resource)));

		ThrowIfFailed(_device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
			&clearValue, IID_PPV_ARGS(&_vignettingTexture.Resource)));

		// Create RTV
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		rtvDesc.Texture2D.PlaneSlice = 0;
		rtvDesc.Format = _format;

		_device->CreateRenderTargetView(_ssrTexture.Resource.Get(), &rtvDesc,
			TextureManager::rtvHeapAllocator->GetCpuHandle(_ssrTexture.otherIndex));
		_device->CreateRenderTargetView(_chromaticAberrationTexture.Resource.Get(), &rtvDesc,
			TextureManager::rtvHeapAllocator->GetCpuHandle(_chromaticAberrationTexture.otherIndex));
		_device->CreateRenderTargetView(_vignettingTexture.Resource.Get(), &rtvDesc,
			TextureManager::rtvHeapAllocator->GetCpuHandle(_vignettingTexture.otherIndex));

		//create SRV
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = _format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		_device->CreateShaderResourceView(_ssrTexture.Resource.Get(), &srvDesc,
			TextureManager::srvHeapAllocator->GetCpuHandle(_ssrTexture.SrvIndex));
		_device->CreateShaderResourceView(_chromaticAberrationTexture.Resource.Get(), &srvDesc,
			TextureManager::srvHeapAllocator->GetCpuHandle(_chromaticAberrationTexture.SrvIndex));
		_device->CreateShaderResourceView(_vignettingTexture.Resource.Get(), &srvDesc,
			TextureManager::srvHeapAllocator->GetCpuHandle(_vignettingTexture.SrvIndex));
	}
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

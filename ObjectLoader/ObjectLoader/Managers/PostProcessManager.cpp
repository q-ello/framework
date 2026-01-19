#include "PostProcessManager.h"

using namespace Microsoft::WRL;

void PostProcessManager::Init(const int width, const int height)
{
	SetNewResolutionAndRects(width, height);

	//get srv and rtv indices
	{
		const auto& rtvAllocator = TextureManager::RtvHeapAllocator;
		const auto& srvAllocator = TextureManager::SrvHeapAllocator;

		_lightOcclusionMask.OtherIndex = rtvAllocator->Allocate();
		_lightOcclusionMask.SrvIndex = srvAllocator->Allocate();

		_ssrTexture.OtherIndex = rtvAllocator->Allocate();
		_ssrTexture.SrvIndex = srvAllocator->Allocate();

		_chromaticAberrationTexture.OtherIndex = rtvAllocator->Allocate();
		_chromaticAberrationTexture.SrvIndex = srvAllocator->Allocate();

		_vignettingTexture.OtherIndex = rtvAllocator->Allocate();
		_vignettingTexture.SrvIndex = srvAllocator->Allocate();
	}

	BuildRootSignature();
	BuildShaders();
	BuildPsOs();
	BuildTextures();
}

void PostProcessManager::BindToManagers(GBuffer* gbuffer, LightingManager* lightingManager, Camera* camera)
{
	_gBuffer = gbuffer;
	_lightingManager = lightingManager;
	_camera = camera;

	_fullscreenLightVs = _lightingManager->GetFullScreenVsWithSamplers();
	_fullscreenVs = _lightingManager->GetFullScreenVs();
}

void PostProcessManager::DrawGodRaysPass(ID3D12GraphicsCommandList4* cmdList, const FrameResource* currFrameResource)
{
	if (!*_lightingManager->IsMainLightOn())
		return;

	OcclusionMaskPass(cmdList, currFrameResource);
	GodRaysPass(cmdList, currFrameResource);
}

void PostProcessManager::OcclusionMaskPass(ID3D12GraphicsCommandList4* cmdList, const FrameResource* currFrameResource)
{
	//occlusion mask to rtv
	BasicUtil::ChangeTextureState(cmdList, _lightOcclusionMask, D3D12_RESOURCE_STATE_RENDER_TARGET);

	cmdList->RSSetViewports(1, &_occlusionMaskViewport);
	cmdList->RSSetScissorRects(1, &_occlusionMaskScissorRect);

	const auto lomRtv = TextureManager::RtvHeapAllocator->GetCpuHandle(_lightOcclusionMask.OtherIndex);

	//light occlusion mask pass
	cmdList->SetPipelineState(_lightOcclusionMaskPso.Get());
	cmdList->SetGraphicsRootSignature(_lightOcclusionMaskRootSignature.Get());
	cmdList->SetGraphicsRootDescriptorTable(0, _gBuffer->GetGBufferTextureSrv(GBufferInfo::Depth));
	cmdList->SetGraphicsRootDescriptorTable(1, _lightingManager->GetCascadeShadowSrv()); //cascades shadow map

	const auto lightingPassCb = currFrameResource->LightingPassCb->Resource();
	cmdList->SetGraphicsRootConstantBufferView(2, lightingPassCb->GetGPUVirtualAddress());

	const auto dirLightCb = currFrameResource->DirLightCb->Resource();
	cmdList->SetGraphicsRootConstantBufferView(3, dirLightCb->GetGPUVirtualAddress());

	constexpr FLOAT clearColor[4] = { 0.f, 0.f, 0.f, 0.f };
	cmdList->ClearRenderTargetView(lomRtv, clearColor, 0, nullptr);
	cmdList->OMSetRenderTargets(1, &lomRtv, true, nullptr);
	cmdList->DrawInstanced(3, 1, 0, 0);

	//occlusion mask to srv
	BasicUtil::ChangeTextureState(cmdList, _lightOcclusionMask, D3D12_RESOURCE_STATE_GENERIC_READ);
}

void PostProcessManager::GodRaysPass(ID3D12GraphicsCommandList* cmdList, const FrameResource* currFrameResource) const
{
	cmdList->RSSetViewports(1, &_viewport);
	cmdList->RSSetScissorRects(1, &_scissorRect);

	cmdList->SetPipelineState(_godRaysPso.Get());
	cmdList->SetGraphicsRootSignature(_godRaysRootSignature.Get());
	cmdList->SetGraphicsRootDescriptorTable(0, TextureManager::SrvHeapAllocator->GetGpuHandle(_lightOcclusionMask.SrvIndex));

	const auto lightingPassCb = currFrameResource->LightingPassCb->Resource();
	cmdList->SetGraphicsRootConstantBufferView(1, lightingPassCb->GetGPUVirtualAddress());

	const auto dirLightCb = currFrameResource->DirLightCb->Resource();
	cmdList->SetGraphicsRootConstantBufferView(2, dirLightCb->GetGPUVirtualAddress());

	const auto godRaysCb = currFrameResource->GodRaysCb->Resource();
	cmdList->SetGraphicsRootConstantBufferView(3, godRaysCb->GetGPUVirtualAddress());

	const auto rtDesc = _lightingManager->GetMiddlewareRtv();

	cmdList->OMSetRenderTargets(1, &rtDesc, true, nullptr);
	cmdList->DrawInstanced(3, 1, 0, 0);
}

void PostProcessManager::DrawSsr(ID3D12GraphicsCommandList4* cmdList, const FrameResource* currFrameResource)
{
	cmdList->SetPipelineState(_ssrPso.Get());
	cmdList->SetGraphicsRootSignature(_ssrRootSignature.Get());

	_lightingManager->ChangeMiddlewareState(cmdList, D3D12_RESOURCE_STATE_GENERIC_READ);
	cmdList->SetGraphicsRootDescriptorTable(0, _lightingManager->GetFinalTextureSrv());

	cmdList->SetGraphicsRootDescriptorTable(1, _gBuffer->GetGBufferTextureSrv(GBufferInfo::Normals));

	const auto lightingPassCb = currFrameResource->LightingPassCb->Resource();
	cmdList->SetGraphicsRootConstantBufferView(2, lightingPassCb->GetGPUVirtualAddress());

	const auto ssrCb = currFrameResource->SsrCb->Resource();
	cmdList->SetGraphicsRootConstantBufferView(3, ssrCb->GetGPUVirtualAddress());

	BasicUtil::ChangeTextureState(cmdList, _ssrTexture, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const auto rtDesc = TextureManager::RtvHeapAllocator->GetCpuHandle(_ssrTexture.OtherIndex);

	cmdList->OMSetRenderTargets(1, &rtDesc, true, nullptr);
	cmdList->DrawInstanced(3, 1, 0, 0);

	BasicUtil::ChangeTextureState(cmdList, _ssrTexture, D3D12_RESOURCE_STATE_GENERIC_READ);

	_lightingManager->SetFinalTextureIndex(_ssrTexture.SrvIndex);
}

void PostProcessManager::DrawChromaticAberration(ID3D12GraphicsCommandList4* cmdList, const FrameResource* currFrameResource)
{
	//just in case
	_lightingManager->ChangeMiddlewareState(cmdList, D3D12_RESOURCE_STATE_GENERIC_READ);

	cmdList->SetPipelineState(_chromaticAberrationPso.Get());
	cmdList->SetGraphicsRootSignature(_chromaticAndVignettingRootSignature.Get());

	cmdList->SetGraphicsRootDescriptorTable(0, _lightingManager->GetFinalTextureSrv());

	const auto lightingPassCb = currFrameResource->LightingPassCb->Resource();
	cmdList->SetGraphicsRootConstantBufferView(1, lightingPassCb->GetGPUVirtualAddress());

	cmdList->SetGraphicsRoot32BitConstants(2, 1, &ChromaticAberrationStrength, 0);

	BasicUtil::ChangeTextureState(cmdList, _chromaticAberrationTexture, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const auto rtDesc = TextureManager::RtvHeapAllocator->GetCpuHandle(_chromaticAberrationTexture.OtherIndex);

	cmdList->OMSetRenderTargets(1, &rtDesc, true, nullptr);
	cmdList->DrawInstanced(3, 1, 0, 0);

	BasicUtil::ChangeTextureState(cmdList, _chromaticAberrationTexture, D3D12_RESOURCE_STATE_GENERIC_READ);

	_lightingManager->SetFinalTextureIndex(_chromaticAberrationTexture.SrvIndex);
}

void PostProcessManager::DrawVignetting(ID3D12GraphicsCommandList4* cmdList, const FrameResource* currFrameResource)
{
	//just in case
	_lightingManager->ChangeMiddlewareState(cmdList, D3D12_RESOURCE_STATE_GENERIC_READ);

	cmdList->SetPipelineState(_vignettingPso.Get());
	cmdList->SetGraphicsRootSignature(_chromaticAndVignettingRootSignature.Get());

	cmdList->SetGraphicsRootDescriptorTable(0, _lightingManager->GetFinalTextureSrv());

	const auto lightingPassCb = currFrameResource->LightingPassCb->Resource();
	cmdList->SetGraphicsRootConstantBufferView(1, lightingPassCb->GetGPUVirtualAddress());

	cmdList->SetGraphicsRoot32BitConstants(2, 1, &VignettingPower, 0);

	BasicUtil::ChangeTextureState(cmdList, _vignettingTexture, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const auto rtDesc = TextureManager::RtvHeapAllocator->GetCpuHandle(_vignettingTexture.OtherIndex);
	cmdList->OMSetRenderTargets(1, &rtDesc, true, nullptr);
	cmdList->DrawInstanced(3, 1, 0, 0);

	BasicUtil::ChangeTextureState(cmdList, _vignettingTexture, D3D12_RESOURCE_STATE_GENERIC_READ);

	_lightingManager->SetFinalTextureIndex(_vignettingTexture.SrvIndex);
}

void PostProcessManager::OnResize(const int newWidth, const int newHeight)
{
	SetNewResolutionAndRects(newWidth, newHeight);
	BuildTextures();
}

void PostProcessManager::UpdateGodRaysParameters() const
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		FrameResource::FrameResources()[i]->GodRaysCb->CopyData(0, GodRaysParameters);
	}
}

void PostProcessManager::UpdateSsrParameters(const FrameResource* currFrame)
{
	//transpose right away to just store it into upload buffer
	//SSRParameters.InvProj = DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, _camera->GetProj()));
	SsrParameters.InvProj = DirectX::XMMatrixInverse(nullptr, _camera->GetProj());
	currFrame->SsrCb->CopyData(0, SsrParameters);
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

		constexpr int rootParameterCount = 4;

		CD3DX12_ROOT_PARAMETER lightingSlotRootParameter[rootParameterCount];

		lightingSlotRootParameter[0].InitAsDescriptorTable(1, &depthTexTable);
		lightingSlotRootParameter[1].InitAsDescriptorTable(1, &cascadesShadowTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
		lightingSlotRootParameter[2].InitAsConstantBufferView(0);
		lightingSlotRootParameter[3].InitAsConstantBufferView(1);

		CD3DX12_ROOT_SIGNATURE_DESC lightingRootSigDesc(rootParameterCount, lightingSlotRootParameter,
		                                                static_cast<UINT>(TextureManager::GetLinearSamplers().size()),
		                                                TextureManager::GetLinearSamplers().data(),
		                                                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3D12SerializeRootSignature(&lightingRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
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
			IID_PPV_ARGS(_lightOcclusionMaskRootSignature.GetAddressOf())));
	}
	//god rays root signature
	{
		CD3DX12_DESCRIPTOR_RANGE lightOcclusionMaskTexTable;
		lightOcclusionMaskTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

		constexpr int rootParameterCount = 4;

		CD3DX12_ROOT_PARAMETER godRaysSlotRootParameter[rootParameterCount];

		godRaysSlotRootParameter[0].InitAsDescriptorTable(1, &lightOcclusionMaskTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
		godRaysSlotRootParameter[1].InitAsConstantBufferView(0);
		godRaysSlotRootParameter[2].InitAsConstantBufferView(1);
		godRaysSlotRootParameter[3].InitAsConstantBufferView(2);

		CD3DX12_ROOT_SIGNATURE_DESC godRaysRootSigDesc(rootParameterCount, godRaysSlotRootParameter,
		                                               static_cast<UINT>(TextureManager::GetLinearSamplers().size()),
		                                               TextureManager::GetLinearSamplers().data(),
		                                               D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3D12SerializeRootSignature(&godRaysRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
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
			IID_PPV_ARGS(_godRaysRootSignature.GetAddressOf())));
	}
	//ssr root signature
	{
		CD3DX12_DESCRIPTOR_RANGE litSceneTexTable;
		litSceneTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		CD3DX12_DESCRIPTOR_RANGE gbufferTexTable;
		gbufferTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1);

		constexpr int ssrRootParameterCount = 4;

		CD3DX12_ROOT_PARAMETER ssrSlotRootParameter[ssrRootParameterCount];

		ssrSlotRootParameter[0].InitAsDescriptorTable(1, &litSceneTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
		ssrSlotRootParameter[1].InitAsDescriptorTable(1, &gbufferTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
		ssrSlotRootParameter[2].InitAsConstantBufferView(0);
		ssrSlotRootParameter[3].InitAsConstantBufferView(1);

		CD3DX12_ROOT_SIGNATURE_DESC ssrRootSigDesc(ssrRootParameterCount, ssrSlotRootParameter, static_cast<UINT>(TextureManager::GetLinearSamplers().size()), TextureManager::GetLinearSamplers().data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3D12SerializeRootSignature(&ssrRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
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
			IID_PPV_ARGS(_ssrRootSignature.GetAddressOf())));
	}
	//chromatic aberration root signature
	{
		CD3DX12_DESCRIPTOR_RANGE litSceneTexTable;
		litSceneTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

		constexpr int chromaticAberrationRootParameterCount = 3;

		CD3DX12_ROOT_PARAMETER chromaticAberrationSlotRootParameter[chromaticAberrationRootParameterCount];

		chromaticAberrationSlotRootParameter[0].InitAsDescriptorTable(1, &litSceneTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
		chromaticAberrationSlotRootParameter[1].InitAsConstantBufferView(0);
		chromaticAberrationSlotRootParameter[2].InitAsConstants(1, 1);

		CD3DX12_ROOT_SIGNATURE_DESC chromaticAberrationRootSigDesc(chromaticAberrationRootParameterCount,
		                                                           chromaticAberrationSlotRootParameter,
		                                                           static_cast<UINT>(TextureManager::GetLinearSamplers().size()),
		                                                           TextureManager::GetLinearSamplers().data(),
		                                                           D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3D12SerializeRootSignature(&chromaticAberrationRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
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
			IID_PPV_ARGS(_chromaticAndVignettingRootSignature.GetAddressOf())));
	}
}

void PostProcessManager::BuildShaders()
{ 
	_lightOcclusionMaskPs = d3dUtil::CompileShader(L"Shaders\\LightOcclusionMask.hlsl", nullptr, "LightOcclusionPS",
	                                               "ps_5_1");
	_godRaysPs = d3dUtil::CompileShader(L"Shaders\\GodRays.hlsl", nullptr, "GodRaysPS", "ps_5_1");
	_ssrPs = d3dUtil::CompileShader(L"Shaders\\ScreenSpaceReflection.hlsl", nullptr, "PS", "ps_5_1");
	_chromaticAberrationPs = d3dUtil::CompileShader(L"Shaders\\ChromaticAndVignetting.hlsl", nullptr, "ChromaticPS", "ps_5_1");
	_vignettingPs = d3dUtil::CompileShader(L"Shaders\\ChromaticAndVignetting.hlsl", nullptr, "VignettingPS", "ps_5_1");
}

void PostProcessManager::BuildPsOs()
{ 
	D3D12_GRAPHICS_PIPELINE_STATE_DESC lightOcclusionMaskPsoDesc;
	ZeroMemory(&lightOcclusionMaskPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	lightOcclusionMaskPsoDesc.InputLayout = { nullptr, 0 };
	lightOcclusionMaskPsoDesc.pRootSignature = _lightOcclusionMaskRootSignature.Get();
	lightOcclusionMaskPsoDesc.VS = 
	{
	static_cast<BYTE*>(_fullscreenLightVs->GetBufferPointer()),
	_fullscreenLightVs->GetBufferSize()
	};
	lightOcclusionMaskPsoDesc.PS = 
	{
		static_cast<BYTE*>(_lightOcclusionMaskPs->GetBufferPointer()),
		_lightOcclusionMaskPs->GetBufferSize()
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
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&lightOcclusionMaskPsoDesc, IID_PPV_ARGS(&_lightOcclusionMaskPso)));

	//god rays
	D3D12_GRAPHICS_PIPELINE_STATE_DESC godRaysPsoDesc = lightOcclusionMaskPsoDesc;
	godRaysPsoDesc.pRootSignature = _godRaysRootSignature.Get();
	godRaysPsoDesc.PS = 
	{
		static_cast<BYTE*>(_godRaysPs->GetBufferPointer()),
		_godRaysPs->GetBufferSize()
	};
	godRaysPsoDesc.RTVFormats[0] = _format;
	godRaysPsoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	godRaysPsoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	godRaysPsoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	godRaysPsoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	godRaysPsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	ThrowIfFailed(_device->CreateGraphicsPipelineState(&godRaysPsoDesc, IID_PPV_ARGS(&_godRaysPso)));

	//ssr
	D3D12_GRAPHICS_PIPELINE_STATE_DESC ssrPSODesc = lightOcclusionMaskPsoDesc;
	ssrPSODesc.pRootSignature = _ssrRootSignature.Get();
	ssrPSODesc.VS =
	{
	static_cast<BYTE*>(_fullscreenVs->GetBufferPointer()),
	_fullscreenVs->GetBufferSize()
	};
	ssrPSODesc.PS =
	{
		static_cast<BYTE*>(_ssrPs->GetBufferPointer()),
		_ssrPs->GetBufferSize()
	};
	ssrPSODesc.RTVFormats[0] = _format;
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&ssrPSODesc, IID_PPV_ARGS(&_ssrPso)));

	//chromatic aberration
	D3D12_GRAPHICS_PIPELINE_STATE_DESC chromaticAberrationPSODesc = ssrPSODesc;
	chromaticAberrationPSODesc.pRootSignature = _chromaticAndVignettingRootSignature.Get();
	chromaticAberrationPSODesc.PS =
	{
		static_cast<BYTE*>(_chromaticAberrationPs->GetBufferPointer()),
		_chromaticAberrationPs->GetBufferSize()
	};
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&chromaticAberrationPSODesc, IID_PPV_ARGS(&_chromaticAberrationPso)));

	//vignetting
	D3D12_GRAPHICS_PIPELINE_STATE_DESC vignettingPSODesc = chromaticAberrationPSODesc;
	vignettingPSODesc.PS =
	{
		static_cast<BYTE*>(_vignettingPs->GetBufferPointer()),
		_vignettingPs->GetBufferSize()
	};
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&vignettingPSODesc, IID_PPV_ARGS(&_vignettingPso)));
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

	_device->CreateRenderTargetView(_lightOcclusionMask.Resource.Get(), &rtvDesc, TextureManager::RtvHeapAllocator->GetCpuHandle(_lightOcclusionMask.OtherIndex));

	//create SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = _lightOcclusionMaskFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	_device->CreateShaderResourceView(_lightOcclusionMask.Resource.Get(), &srvDesc, 
	                                  TextureManager::SrvHeapAllocator->GetCpuHandle(_lightOcclusionMask.SrvIndex));

	//most textures are kinda identical
	{
		D3D12_RESOURCE_DESC resourceDesc = {};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Alignment = 0;
		resourceDesc.Width = _width;
		resourceDesc.Height = _height;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = _format;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		D3D12_CLEAR_VALUE value = {};
		value.Format = _format;
		value.Color[0] = 0.0f;
		value.Color[1] = 0.0f;
		value.Color[2] = 0.0f;
		value.Color[3] = 0.0f;

		CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);

		ThrowIfFailed(_device->CreateCommittedResource(
			&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
			&value, IID_PPV_ARGS(&_ssrTexture.Resource)));

		ThrowIfFailed(_device->CreateCommittedResource(
			&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
			&value, IID_PPV_ARGS(&_chromaticAberrationTexture.Resource)));

		ThrowIfFailed(_device->CreateCommittedResource(
			&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
			&value, IID_PPV_ARGS(&_vignettingTexture.Resource)));

		// Create RTV
		D3D12_RENDER_TARGET_VIEW_DESC targetViewDesc = {};
		targetViewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		targetViewDesc.Texture2D.MipSlice = 0;
		targetViewDesc.Texture2D.PlaneSlice = 0;
		targetViewDesc.Format = _format;

		_device->CreateRenderTargetView(_ssrTexture.Resource.Get(), &targetViewDesc,
			TextureManager::RtvHeapAllocator->GetCpuHandle(_ssrTexture.OtherIndex));
		_device->CreateRenderTargetView(_chromaticAberrationTexture.Resource.Get(), &targetViewDesc,
			TextureManager::RtvHeapAllocator->GetCpuHandle(_chromaticAberrationTexture.OtherIndex));
		_device->CreateRenderTargetView(_vignettingTexture.Resource.Get(), &targetViewDesc,
			TextureManager::RtvHeapAllocator->GetCpuHandle(_vignettingTexture.OtherIndex));

		//create SRV
		D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc = {};
		shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		shaderResourceViewDesc.Format = _format;
		shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		shaderResourceViewDesc.Texture2D.MipLevels = 1;

		_device->CreateShaderResourceView(_ssrTexture.Resource.Get(), &shaderResourceViewDesc,
			TextureManager::SrvHeapAllocator->GetCpuHandle(_ssrTexture.SrvIndex));
		_device->CreateShaderResourceView(_chromaticAberrationTexture.Resource.Get(), &shaderResourceViewDesc,
			TextureManager::SrvHeapAllocator->GetCpuHandle(_chromaticAberrationTexture.SrvIndex));
		_device->CreateShaderResourceView(_vignettingTexture.Resource.Get(), &shaderResourceViewDesc,
			TextureManager::SrvHeapAllocator->GetCpuHandle(_vignettingTexture.SrvIndex));
	}
}

void PostProcessManager::SetNewResolutionAndRects(const int newWidth, const int newHeight)
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

#include "GBuffer.h"

int GBuffer::CurrentDepth = 0;

GBuffer::GBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const int width, const int height)
{
	_device = device;
	_width = width;
	_height = height;
	_cmdList = cmdList;
	_rtvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	_srvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	_dsvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	DescriptorHeapAllocator* srvHeapAllocator = TextureManager::SrvHeapAllocator.get();
	DescriptorHeapAllocator* rtvHeapAllocator = TextureManager::RtvHeapAllocator.get();
	DescriptorHeapAllocator* dsvHeapAllocator = TextureManager::DsvHeapAllocator.get();

	for (int i = 0; i < static_cast<int>(GBufferInfo::Count); i++)
	{
		_info[i].SrvIndex = srvHeapAllocator->Allocate();
		_info[i].OtherIndex = rtvHeapAllocator->Allocate();
	}

	for (int i = 0; i < depthsNum; i++)
	{
		_depths[i].SrvIndex = srvHeapAllocator->Allocate();
		_depths[i].OtherIndex = dsvHeapAllocator->Allocate();
	}
}

void GBuffer::OnResize(const int width, const int height)
{
	_width = width;
	_height = height;

	// Release the previous resources we will be recreating.

	for (auto& tex : _info)
		tex.Reset();

	const DescriptorHeapAllocator* srvHeapAllocator = TextureManager::SrvHeapAllocator.get();
	const DescriptorHeapAllocator* rtvHeapAllocator = TextureManager::RtvHeapAllocator.get();
	const DescriptorHeapAllocator* dsvHeapAllocator = TextureManager::DsvHeapAllocator.get();

	for (int i = 0; i < static_cast<int>(GBufferInfo::Count); i++)
	{
		CreateGBufferTexture(i, 
		                     rtvHeapAllocator->GetCpuHandle(_info[i].OtherIndex), 
		                     srvHeapAllocator->GetCpuHandle(_info[i].SrvIndex), false);
	}

	for (int i = 0; i < depthsNum; i++)
	{
		CreateGBufferTexture(i, 
		                     dsvHeapAllocator->GetCpuHandle(_depths[i].OtherIndex), 
		                     srvHeapAllocator->GetCpuHandle(_depths[i].SrvIndex), true);
	}
}

void GBuffer::CreateGBufferTexture(int i, D3D12_CPU_DESCRIPTOR_HANDLE otherHeapHandle, D3D12_CPU_DESCRIPTOR_HANDLE srvHeapHandle, const bool isDsv)
{
	//creating new rtvs
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = _width;
	texDesc.Height = _height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = isDsv ? depthFormat : infoFormats[i];
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = isDsv ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clearValue = {};
	if (isDsv)
	{
		clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		clearValue.DepthStencil.Depth = 1.0f;
		clearValue.DepthStencil.Stencil = 0;
	}
	else
	{
		clearValue.Format = infoFormats[i];
		clearValue.Color[0] = 0.0f;
		clearValue.Color[1] = 0.0f;
		clearValue.Color[2] = 0.0f;
		clearValue.Color[3] = 0.0f;
	}
	
	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

	ThrowIfFailed(_device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
		&clearValue, IID_PPV_ARGS(isDsv ? &_depths[i].Resource : &_info[i].Resource)));

	if (isDsv)
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		dsvDesc.Texture2D.MipSlice = 0;
		_device->CreateDepthStencilView(_depths[i].Resource.Get(), &dsvDesc, otherHeapHandle);
	}
	else
	{
		// Create RTV
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		rtvDesc.Texture2D.PlaneSlice = 0;
		rtvDesc.Format = infoFormats[i];

		_device->CreateRenderTargetView(_info[i].Resource.Get(), &rtvDesc, otherHeapHandle);
	}

	//create SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = isDsv ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : infoFormats[i];
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	_device->CreateShaderResourceView((isDsv ? _depths[i] : _info[i]).Resource.Get(), &srvDesc, srvHeapHandle);
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::DepthStencilView() const
{
	return TextureManager::DsvHeapAllocator->GetCpuHandle(_depths[CurrentDepth].OtherIndex);
}

void GBuffer::ClearInfo(const FLOAT* color)
{
	ChangeDsvState(D3D12_RESOURCE_STATE_DEPTH_WRITE, CurrentDepth);

	_cmdList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	const auto prevState = _info[0].PrevState;
	const auto rtvHeapAllocator = TextureManager::RtvHeapAllocator.get();

	ChangeRtVsState(D3D12_RESOURCE_STATE_RENDER_TARGET);
	for (int i = 0; i < static_cast<int>(GBufferInfo::Count); i++)
	{
		if (i == static_cast<int>(GBufferInfo::Depth))
			continue;
		_cmdList->ClearRenderTargetView(rtvHeapAllocator->GetCpuHandle(_info[i].OtherIndex), color, 0, nullptr);
	}
	ChangeRtVsState(prevState);
}

void GBuffer::ChangeRtVsState(const D3D12_RESOURCE_STATES stateAfter)
{
	std::vector<CD3DX12_RESOURCE_BARRIER> barriers;
	for (int i = 0; i < static_cast<int>(GBufferInfo::Count); i++)
	{
		if (_info[i].PrevState == stateAfter)
			continue;

		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(_info[i].Resource.Get(), _info[i].PrevState, stateAfter));
		_info[i].PrevState = stateAfter;
	}
	if (barriers.size() == 0)
	{
		return;
	}
	_cmdList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

void GBuffer::ChangeDsvState(const D3D12_RESOURCE_STATES stateAfter, const int depthIndex)
{
	RtvSrvTexture* depthTex = &_depths[depthIndex];
	if (depthTex->PrevState == stateAfter)
		return;
	const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(depthTex->Resource.Get(), 
                         		depthTex->PrevState, stateAfter);
	_cmdList->ResourceBarrier(1, &barrier);
	depthTex->PrevState = stateAfter;
}

void GBuffer::ChangeBothDepthState(const D3D12_RESOURCE_STATES stateAfter)
{
	for (int i = 0; i < depthsNum; i++)
	{
		ChangeDsvState(stateAfter, i);
	}
}

std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> GBuffer::RtVs() const
{
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs;
	const auto rtvDescriptorAllocator = TextureManager::RtvHeapAllocator.get();
	for (int i = 0; i < static_cast<int>(GBufferInfo::Count); i++)
	{
		rtvs.push_back(rtvDescriptorAllocator->GetCpuHandle(_info[i].OtherIndex));
	}
	return rtvs;
}

D3D12_GPU_DESCRIPTOR_HANDLE GBuffer::SrvGpuHandle() const
{
	return TextureManager::SrvHeapAllocator->GetGpuHandle(_info[0].SrvIndex);
}

D3D12_GPU_DESCRIPTOR_HANDLE GBuffer::GetGBufferTextureSrv(GBufferInfo type) const
{
	return TextureManager::SrvHeapAllocator->GetGpuHandle(_info[static_cast<int>(type)].SrvIndex);
}

D3D12_GPU_DESCRIPTOR_HANDLE GBuffer::GetGBufferDepthSrv(const bool isCurrent) const
{
	return TextureManager::SrvHeapAllocator->GetGpuHandle(_depths[isCurrent ? CurrentDepth : (CurrentDepth + 1) % depthsNum].SrvIndex);
}

void GBuffer::ChangeDepthTexture()
{
	CurrentDepth = (CurrentDepth + 1) % depthsNum;
}

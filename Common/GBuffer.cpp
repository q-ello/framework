#include "GBuffer.h"

GBuffer::GBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, int width, int height)
{
	_device = device;
	_width = width;
	_height = height;
	_cmdList = cmdList;
	_rtvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	_srvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	_dsvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	DescriptorHeapAllocator* srvHeapAllocator = TextureManager::srvHeapAllocator.get();
	DescriptorHeapAllocator* rtvHeapAllocator = TextureManager::rtvHeapAllocator.get();
	DescriptorHeapAllocator* dsvHeapAllocator = TextureManager::dsvHeapAllocator.get();

	for (int i = 0; i < (int)GBufferInfo::Count; i++)
	{
		_info[i].SrvIndex = srvHeapAllocator->Allocate();
		_info[i].otherIndex = i == (int)GBufferInfo::Depth ? dsvHeapAllocator->Allocate() : rtvHeapAllocator->Allocate();
	}
}

GBuffer::~GBuffer()
{
	for (auto& tex : _info)
		tex.Reset();
}

void GBuffer::OnResize(int width, int height)
{
	_width = width;
	_height = height;

	// Release the previous resources we will be recreating.

	for (auto& tex : _info)
		tex.Reset();

	DescriptorHeapAllocator* srvHeapAllocator = TextureManager::srvHeapAllocator.get();
	DescriptorHeapAllocator* rtvHeapAllocator = TextureManager::rtvHeapAllocator.get();
	DescriptorHeapAllocator* dsvHeapAllocator = TextureManager::dsvHeapAllocator.get();

	for (int i = 0; i < (int)GBufferInfo::Count; i++)
	{
		bool isDsv = i == (int)GBufferInfo::Depth;
		CreateGBufferTexture(i, 
			isDsv ? dsvHeapAllocator->GetCpuHandle(_info[i].otherIndex) : rtvHeapAllocator->GetCpuHandle(_info[i].otherIndex), 
			srvHeapAllocator->GetCpuHandle(_info[i].SrvIndex));
	}
}

void GBuffer::CreateGBufferTexture(int i, D3D12_CPU_DESCRIPTOR_HANDLE otherHeapHandle, D3D12_CPU_DESCRIPTOR_HANDLE srvHeapHandle)
{
	bool isDSV = i == (int)GBufferInfo::Depth;

	//creating new rtvs
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = _width;
	texDesc.Height = _height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = infoFormats[i]; // example for normals
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = isDSV ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clearValue = {};
	if (isDSV)
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
		&clearValue, IID_PPV_ARGS(&_info[i].Resource)));

	if (isDSV)
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		dsvDesc.Texture2D.MipSlice = 0;
		_device->CreateDepthStencilView(_info[i].Resource.Get(), &dsvDesc, otherHeapHandle);
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
	srvDesc.Format = isDSV ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : infoFormats[i];
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	_device->CreateShaderResourceView(_info[i].Resource.Get(), &srvDesc, srvHeapHandle);
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::DepthStencilView()
{
	return TextureManager::dsvHeapAllocator->GetCpuHandle(_info[(int)GBufferInfo::Depth].otherIndex);
}

void GBuffer::ClearInfo(const FLOAT* color)
{
	ChangeDSVState(D3D12_RESOURCE_STATE_DEPTH_WRITE);

	_cmdList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	auto prevState = _info[0].prevState;
	auto rtvHeapAllocator = TextureManager::rtvHeapAllocator.get();

	ChangeRTVsState(D3D12_RESOURCE_STATE_RENDER_TARGET);
	for (int i = 0; i < (int)GBufferInfo::Count; i++)
	{
		if (i == (int)GBufferInfo::Depth)
			continue;
		_cmdList->ClearRenderTargetView(rtvHeapAllocator->GetCpuHandle(_info[i].otherIndex), color, 0, nullptr);
	}
	ChangeRTVsState(prevState);
}

void GBuffer::ChangeRTVsState(D3D12_RESOURCE_STATES stateAfter)
{
	std::vector<CD3DX12_RESOURCE_BARRIER> barriers;
	for (int i = 0; i < (int)GBufferInfo::Count; i++)
	{
		if (i == (int)GBufferInfo::Depth || _info[i].prevState == stateAfter)
			continue;

		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(_info[i].Resource.Get(), _info[i].prevState, stateAfter));
		_info[i].prevState = stateAfter;
	}
	if (barriers.size() == 0)
	{
		return;
	}
	_cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
}

void GBuffer::ChangeDSVState(D3D12_RESOURCE_STATES stateAfter)
{
	RtvSrvTexture* depthTex = &_info[(int)GBufferInfo::Depth];
	if (depthTex->prevState == stateAfter)
		return;
	_cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(depthTex->Resource.Get(), 
		depthTex->prevState, stateAfter));
	depthTex->prevState = stateAfter;
}

std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> GBuffer::RTVs() const
{
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs;
	auto rtvDescriptorAllocator = TextureManager::rtvHeapAllocator.get();
	for (int i = 0; i < (int)GBufferInfo::Count; i++)
	{
		if (i == (int)GBufferInfo::Depth)
			continue;
		rtvs.push_back(rtvDescriptorAllocator->GetCpuHandle(_info[i].otherIndex));
	}
	return rtvs;
}
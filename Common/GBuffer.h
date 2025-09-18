#pragma once
#include "d3dUtil.h"
#include "../ObjectLoader/ObjectLoader/TextureManager.h"

enum class GBufferInfo
{
	BaseColor = 0,
	Normals,
	Emissive,
	ORM,
	Depth,
	Count
};

struct GBufferTexture
{
	Microsoft::WRL::ComPtr<ID3D12Resource> Resource = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle = {};
	int SrvIndex = -1;
	D3D12_RESOURCE_STATES prevState = D3D12_RESOURCE_STATE_COMMON;

	void Reset()
	{
		Resource.Reset();
		RtvHandle.ptr = 0;
	}
};

class GBuffer
{
public:
	GBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, int width, int height);
	~GBuffer();
	//onresize
	void OnResize(int width, int height);

	void CreateGBufferTexture(int i, CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle, D3D12_CPU_DESCRIPTOR_HANDLE srvHeapHandle,
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHeapHandle);

	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView();

	void ClearInfo(const FLOAT* color);

	static int InfoCount(bool forRTVS = true)
	{
		return (int)GBufferInfo::Count - (int)forRTVS;
	}

	void ChangeRTVsState(D3D12_RESOURCE_STATES stateAfter);
	void ChangeDSVState(D3D12_RESOURCE_STATES stateAfter);

	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> RTVs() const;

	static constexpr DXGI_FORMAT infoFormats[(int)GBufferInfo::Count] = {
	DXGI_FORMAT_R8G8B8A8_UNORM,			// Diffuse
	DXGI_FORMAT_R16G16B16A16_FLOAT,		// Normals
	DXGI_FORMAT_R16G16B16A16_FLOAT,		//Emissive
	DXGI_FORMAT_R8G8B8A8_UNORM,			//ORM
	DXGI_FORMAT_R24G8_TYPELESS,         // Depth
	};

	D3D12_GPU_DESCRIPTOR_HANDLE SRVGPUHandle() const
	{
		return TextureManager::srvHeapAllocator->GetGpuHandle(_info[0].SrvIndex);
	}

private:
	int _height;
	int _width;
	UINT _rtvDescriptorSize;
	UINT _srvDescriptorSize;
	UINT _dsvDescriptorSize;

	GBufferTexture _info[(int)GBufferInfo::Count];

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _infoRTVHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _infoDSVHeap;

	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _cmdList;

	Microsoft::WRL::ComPtr<ID3D12Device> _device;
};
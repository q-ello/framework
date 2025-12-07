#pragma once
#include "d3dUtil.h"
#include "../ObjectLoader/ObjectLoader/Managers/TextureManager.h"

enum class GBufferInfo : uint8_t
{
	BaseColor = 0,
	Emissive,
	Normals,
	Orm,
	TexCoord,
	Depth,
	Count
};

class GBuffer
{
public:
	GBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, int width, int height);
	~GBuffer() = default;
	GBuffer(GBuffer&) = delete;
	GBuffer& operator=(GBuffer&) = delete;
	GBuffer(GBuffer&&) = delete;
	GBuffer& operator=(GBuffer&&) = delete;
	
	void OnResize(int width, int height);

	void CreateGBufferTexture(int i, D3D12_CPU_DESCRIPTOR_HANDLE otherHeapHandle, D3D12_CPU_DESCRIPTOR_HANDLE srvHeapHandle);

	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

	void ClearInfo(const FLOAT* color);

	static int InfoCount(const bool forRtvs = true)
	{
		return static_cast<int>(GBufferInfo::Count) - static_cast<int>(forRtvs);
	}

	void ChangeRtVsState(D3D12_RESOURCE_STATES stateAfter);
	void ChangeDsvState(D3D12_RESOURCE_STATES stateAfter);

	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> RtVs() const;

	static constexpr DXGI_FORMAT infoFormats[static_cast<int>(GBufferInfo::Count)] = {
	DXGI_FORMAT_R8G8B8A8_UNORM,			// Diffuse
	DXGI_FORMAT_R16G16B16A16_FLOAT,		// Normals
	DXGI_FORMAT_R16G16B16A16_FLOAT,		//Emissive
	DXGI_FORMAT_R8G8B8A8_UNORM,			//ORM
	DXGI_FORMAT_R16G16_FLOAT,			//Tex Coords
	DXGI_FORMAT_R24G8_TYPELESS,         // Depth
	};

	D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle() const
	{
		return TextureManager::SrvHeapAllocator->GetGpuHandle(_info[0].SrvIndex);
	}

	D3D12_GPU_DESCRIPTOR_HANDLE GetGBufferTextureSrv(GBufferInfo type) const
	{
		return TextureManager::SrvHeapAllocator->GetGpuHandle(_info[static_cast<int>(type)].SrvIndex);
	}

	ID3D12Resource* GetGBufferTexture(GBufferInfo type) const
	{
		return _info[static_cast<int>(type)].Resource.Get();
	}

private:
	int _height;
	int _width;
	UINT _rtvDescriptorSize;
	UINT _srvDescriptorSize;
	UINT _dsvDescriptorSize;

	RtvSrvTexture _info[static_cast<int>(GBufferInfo::Count)];

	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _cmdList;
	Microsoft::WRL::ComPtr<ID3D12Device> _device;
};
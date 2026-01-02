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
	Velocity,
	Count, //depth and velocity are separate so count is before that
	Depth,
};

class GBuffer
{
public:
	GBuffer(ID3D12Device* device, ID3D12GraphicsCommandList4* cmdList, int width, int height);
	~GBuffer() = default;
	GBuffer(GBuffer&) = delete;
	GBuffer& operator=(GBuffer&) = delete;
	GBuffer(GBuffer&&) = delete;
	GBuffer& operator=(GBuffer&&) = delete;
	
	void OnResize(int width, int height);

	void CreateGBufferTexture(int i, D3D12_CPU_DESCRIPTOR_HANDLE otherHeapHandle, D3D12_CPU_DESCRIPTOR_HANDLE srvHeapHandle, bool isDsv);

	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

	void ClearInfo(const FLOAT* color);

	static int InfoCount(const bool forRtvs = true)
	{
		if (forRtvs)
		{
			return static_cast<int>(GBufferInfo::Count);
		}
		else
		{
			return static_cast<int>(GBufferInfo::Count) - 1;
		}
	}

	void ChangeRtvsState(D3D12_RESOURCE_STATES stateAfter);
	void ChangeDsvState(D3D12_RESOURCE_STATES stateAfter, const int depthIndex = CurrentDepth);
	void ChangeBothDepthState(D3D12_RESOURCE_STATES stateAfter);

	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> Rtvs() const;

	static constexpr DXGI_FORMAT infoFormats[static_cast<int>(GBufferInfo::Count)] = {
	DXGI_FORMAT_R8G8B8A8_UNORM,			// Diffuse
	DXGI_FORMAT_R16G16B16A16_FLOAT,		//Emissive
	DXGI_FORMAT_R16G16B16A16_FLOAT,		// Normals
	DXGI_FORMAT_R8G8B8A8_UNORM,			//ORM
	DXGI_FORMAT_R16G16_FLOAT,			//Tex Coords
	DXGI_FORMAT_R16G16_FLOAT,			//Velocity
	};

	static constexpr DXGI_FORMAT depthFormat = DXGI_FORMAT_R24G8_TYPELESS;

	static int CurrentDepth;

	D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle() const;

	D3D12_GPU_DESCRIPTOR_HANDLE GetGBufferTextureSrv(GBufferInfo type) const;

	D3D12_GPU_DESCRIPTOR_HANDLE GetGBufferDepthSrv(const bool isCurrent) const;

	static void ChangeDepthTexture();
private:
	int _height;
	int _width;
	UINT _rtvDescriptorSize;
	UINT _srvDescriptorSize;
	UINT _dsvDescriptorSize;
	static constexpr int depthsNum = 2;

	RtvSrvTexture _info[static_cast<int>(GBufferInfo::Count)];
	RtvSrvTexture _depths[depthsNum];

	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> _cmdList;
	Microsoft::WRL::ComPtr<ID3D12Device> _device;
};
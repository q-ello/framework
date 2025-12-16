#pragma once
#include "../Helpers/RenderItem.h"
#include "unordered_map"
#include "../../../Common/d3dUtil.h"
#include "../Helpers/DescriptorHeapAllocator.h"
#include <assimp/scene.h>

struct RtvSrvTexture
{
	Microsoft::WRL::ComPtr<ID3D12Resource> Resource = nullptr;
	int OtherIndex = -1;
	int SrvIndex = -1;
	D3D12_RESOURCE_STATES PrevState = D3D12_RESOURCE_STATE_GENERIC_READ;

	void Reset()
	{
		Resource.Reset();
	}
};

class TextureManager
{
public:
	static std::unordered_map<std::wstring, std::unique_ptr<Texture>>& Textures();

	static TextureHandle LoadTexture(const WCHAR* filename = L"default.dds", int prevIndex = 0, int texCount = 1);
	static void LoadTexture(const WCHAR* filename, TextureHandle& texHandle);
	static TextureHandle LoadEmbeddedTexture(const std::wstring& texName, const aiTexture* embeddedTex);
	static bool LoadCubeTexture(const WCHAR* texturePath, TextureHandle& cubeMapHandle);
	static void DeleteTexture(const std::wstring& name, int texCount = 1);

	static void Init(ID3D12Device* device);
	static std::array<const CD3DX12_STATIC_SAMPLER_DESC, 8> GetStaticSamplers();
	static std::array<const CD3DX12_STATIC_SAMPLER_DESC, 3> GetLinearSamplers();
	static std::array<const CD3DX12_STATIC_SAMPLER_DESC, 1> GetLinearWrapSampler();
	static std::unique_ptr<DescriptorHeapAllocator> SrvHeapAllocator;
	static Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> SrvDescriptorHeap;

	static std::unique_ptr<DescriptorHeapAllocator> RtvHeapAllocator;
	static Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> RtvDescriptorHeap;

	static std::unique_ptr<DescriptorHeapAllocator> DsvHeapAllocator;
	static Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DsvDescriptorHeap;
private:
	static ID3D12Device* _device;
	static UINT _srvDescriptorSize;
	static UINT _rtvDescriptorSize;
	static UINT _dsvDescriptorSize;

	static std::unordered_map<std::wstring, UINT>& TexIndices();
	static std::unordered_map<std::wstring, int>& TexUsed();
};
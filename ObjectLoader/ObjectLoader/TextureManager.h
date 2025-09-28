#pragma once
#include "RenderItem.h"
#include "unordered_map"
#include "BasicUtil.h"
#include "../../Common/d3dUtil.h"
#include "DescriptorHeapAllocator.h"
#include "UploadManager.h"
#include <assimp/scene.h>

struct RtvSrvTexture
{
	Microsoft::WRL::ComPtr<ID3D12Resource> Resource = nullptr;
	int otherIndex = -1;
	int SrvIndex = -1;
	D3D12_RESOURCE_STATES prevState = D3D12_RESOURCE_STATE_COMMON;

	void Reset()
	{
		Resource.Reset();
	}
};

class TextureManager
{
public:
	static std::unordered_map<std::wstring, std::unique_ptr<Texture>>& textures();

	static TextureHandle LoadTexture(const WCHAR* filename = L"default.dds", int prevIndex = 0, int texCount = 1);
	static TextureHandle LoadEmbeddedTexture(const std::wstring& texName, const aiTexture* embeddedTex);
	static bool LoadCubeTexture(const WCHAR* texturePath, TextureHandle& cubeMapHandle);
	static void deleteTexture(std::wstring name, int texCount = 1);

	static void Init(ID3D12Device* device);
	static std::array<const CD3DX12_STATIC_SAMPLER_DESC, 8> GetStaticSamplers();
	static std::array<const CD3DX12_STATIC_SAMPLER_DESC, 3> GetShadowSamplers();
	static std::array<const CD3DX12_STATIC_SAMPLER_DESC, 1> GetLinearWrapSampler();
	static std::unique_ptr<DescriptorHeapAllocator> srvHeapAllocator;
	static Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap;

	static std::unique_ptr<DescriptorHeapAllocator> rtvHeapAllocator;
	static Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;

	static std::unique_ptr<DescriptorHeapAllocator> dsvHeapAllocator;
	static Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap;
private:
	static ID3D12Device* _device;
	static UINT _srvDescriptorSize;
	static UINT _rtvDescriptorSize;
	static UINT _dsvDescriptorSize;

	static std::unordered_map<std::wstring, UINT>& _texIndices();
	static std::unordered_map<std::wstring, int>& _texUsed();
};
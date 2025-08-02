#pragma once
#include "RenderItem.h"
#include "unordered_map"
#include "BasicUtil.h"
#include "../../Common/d3dUtil.h"
#include "DescriptorHeapAllocator.h"
#include "UploadManager.h"
#include <assimp/scene.h>

class TextureManager
{
public:
	static std::unordered_map<std::wstring, std::unique_ptr<Texture>>& textures();

	static TextureHandle LoadTexture(const WCHAR* filename = L"default.dds", int prevIndex = 0, int texCount = 1);
	static TextureHandle LoadEmbeddedTexture(const std::wstring& texName, const aiTexture* embeddedTex);
	static void deleteTexture(std::wstring name, int texCount = 1);

	static void Init(ID3D12Device* device);
	static std::array<const CD3DX12_STATIC_SAMPLER_DESC, 8> GetStaticSamplers();
	static std::unique_ptr<DescriptorHeapAllocator> srvHeapAllocator;
	static Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap;
private:
	static ID3D12Device* _device;
	static UINT _srvDescriptorSize;

	static std::unordered_map<std::wstring, UINT>& _texIndices();
	static std::unordered_map<std::wstring, int>& _texUsed();
};
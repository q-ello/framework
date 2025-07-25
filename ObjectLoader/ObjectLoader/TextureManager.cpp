#include "TextureManager.h"

std::unique_ptr<DescriptorHeapAllocator> TextureManager::srvHeapAllocator = nullptr;
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> TextureManager::srvDescriptorHeap = nullptr;
ID3D12Device* TextureManager::_device = nullptr;
UINT TextureManager::_srvDescriptorSize = 0;

std::unordered_map<std::wstring, std::unique_ptr<Texture>>& TextureManager::textures()
{
	static std::unordered_map<std::wstring, std::unique_ptr<Texture>> textures;
	return textures;
}

TextureHandle TextureManager::LoadTexture(WCHAR* filename, int prevIndex, int texCount)
{
	std::wstring croppedName = BasicUtil::getCroppedName(filename);

	if (textures().find(croppedName) != textures().end())
	{
		if (_texIndices()[croppedName] != prevIndex)
		{
			_texUsed()[croppedName] += texCount;
		}
		return { std::string(croppedName.begin(), croppedName.end()), _texIndices()[croppedName], true };
	}

	auto tex = std::make_unique<Texture>();
	tex->Name = croppedName;
	tex->Filename = filename;

	UploadManager::CreateTexture(tex.get());

	tex->size[0] = (std::uint32_t)tex->Resource.Get()->GetDesc().Width;
	tex->size[1] = (std::uint32_t)tex->Resource.Get()->GetDesc().Height;

	UINT index = srvHeapAllocator.get()->Allocate();
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = srvHeapAllocator.get()->GetCpuHandle(index);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = tex.get()->Resource->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = tex.get()->Resource.Get()->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	_device->CreateShaderResourceView(tex.get()->Resource.Get(), &srvDesc, srvHandle);

	UploadManager::ExecuteUploadCommandList();

	textures()[croppedName] = std::move(tex);
	_texIndices()[croppedName] = index;
	_texUsed()[croppedName] = texCount;

	return { std::string(croppedName.begin(), croppedName.end()), index, true };
}

void TextureManager::deleteTexture(std::wstring name, int texCount)
{
	_texUsed()[name] -= texCount;
	if (_texUsed()[name] == 0)
	{
		UploadManager::Flush();
		textures()[name].release();
		textures().erase(name);
		_texUsed().erase(name);
		srvHeapAllocator->Free(_texIndices()[name]);
		_texIndices().erase(name);
	}
}

void TextureManager::Init(ID3D12Device* device)
{
	_device = device;

	//Create the SRV heap.
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 100;

	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvDescriptorHeap)));
	_srvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	srvHeapAllocator = std::make_unique<DescriptorHeapAllocator>(srvDescriptorHeap.Get(), _srvDescriptorSize, srvHeapDesc.NumDescriptors);

	LoadTexture();

	//add default texture
	UINT index = srvHeapAllocator.get()->Allocate();
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = srvHeapAllocator.get()->GetCpuHandle(index);
	auto& defaultTex = textures()[L"tile"]->Resource;
	_texIndices()[L"tile"] = index;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = defaultTex.Get()->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = defaultTex.Get()->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	_device->CreateShaderResourceView(defaultTex.Get(), &srvDesc, srvHandle);
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 8> TextureManager::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearBorder(
		4, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_BORDER); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		6, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC linearMirror(
		7, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_MIRROR,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_MIRROR,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_MIRROR,  // addressW
		0.0f,                              // mipLODBias
		8);

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp, linearBorder,
		anisotropicWrap, anisotropicClamp, linearMirror };
}

std::unordered_map<std::wstring, UINT>& TextureManager::_texIndices()
{
	static std::unordered_map<std::wstring, UINT> _texIndices;
	return _texIndices;
}

std::unordered_map<std::wstring, int>& TextureManager::_texUsed()
{
	static std::unordered_map<std::wstring, int> _texUsed;
	return _texUsed;
}

#include "TextureManager.h"

#include "UploadManager.h"

std::unique_ptr<DescriptorHeapAllocator> TextureManager::SrvHeapAllocator = nullptr;
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> TextureManager::SrvDescriptorHeap = nullptr;
std::unique_ptr<DescriptorHeapAllocator> TextureManager::RtvHeapAllocator = nullptr;
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> TextureManager::RtvDescriptorHeap = nullptr;
std::unique_ptr<DescriptorHeapAllocator> TextureManager::DsvHeapAllocator = nullptr;
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> TextureManager::DsvDescriptorHeap = nullptr;
ID3D12Device* TextureManager::_device = nullptr;
UINT TextureManager::_srvDescriptorSize = 0;
UINT TextureManager::_rtvDescriptorSize = 0;
UINT TextureManager::_dsvDescriptorSize = 0;

std::unordered_map<std::wstring, std::unique_ptr<Texture>>& TextureManager::Textures()
{
	static std::unordered_map<std::wstring, std::unique_ptr<Texture>> textures;
	return textures;
}

TextureHandle TextureManager::LoadTexture(const WCHAR* filename, int prevIndex, int texCount)
{
	std::wstring croppedName = BasicUtil::GetCroppedName(filename);

	if (Textures().find(croppedName) != Textures().end())
	{
		if (TexIndices()[croppedName] != prevIndex)
		{
			TexUsed()[croppedName] += texCount;
		}
		return { std::string(croppedName.begin(), croppedName.end()), TexIndices()[croppedName], true };
	}

	auto tex = std::make_unique<Texture>();
	tex->Name = croppedName;
	tex->Filename = filename;

	if (!UploadManager::CreateTexture(tex.get()))
	{
		OutputDebugStringA(("Failed to load texture: " + std::string(tex->Filename.begin(), tex->Filename.end()) + "\n").c_str());
		return { std::string(croppedName.begin(), croppedName.end()), 0, false };
	}

	UINT index = SrvHeapAllocator.get()->Allocate();
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = SrvHeapAllocator.get()->GetCpuHandle(index);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = tex.get()->Resource->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = tex.get()->Resource.Get()->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	_device->CreateShaderResourceView(tex.get()->Resource.Get(), &srvDesc, srvHandle);

	UploadManager::ExecuteUploadCommandList();

	Textures()[croppedName] = std::move(tex);
	TexIndices()[croppedName] = index;
	TexUsed()[croppedName] = texCount;

	return { std::string(croppedName.begin(), croppedName.end()), index, true };
}

void TextureManager::LoadTexture(const WCHAR* filename, TextureHandle& texHandle)
{
	std::wstring croppedName = BasicUtil::GetCroppedName(filename);

	auto tex = std::make_unique<Texture>();
	tex->Name = croppedName;
	tex->Filename = filename;

	if (!UploadManager::CreateTexture(tex.get()))
	{
		OutputDebugStringA(("Failed to load texture: " + std::string(tex->Filename.begin(), tex->Filename.end()) + "\n").c_str());
		texHandle.useTexture = false;
		return;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = SrvHeapAllocator.get()->GetCpuHandle(texHandle.index);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = tex.get()->Resource->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = tex.get()->Resource.Get()->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	_device->CreateShaderResourceView(tex.get()->Resource.Get(), &srvDesc, srvHandle);

	UploadManager::ExecuteUploadCommandList();

	Textures()[croppedName] = std::move(tex);
	TexIndices()[croppedName] = texHandle.index;
	TexUsed()[croppedName] = 1;

	texHandle.name = std::string(croppedName.begin(), croppedName.end());
	texHandle.useTexture = true;
	return;
}

TextureHandle TextureManager::LoadEmbeddedTexture(const std::wstring& texName, const aiTexture* embeddedTex)
{
	auto tex = std::make_unique<Texture>();
	tex->Name = texName;

	if (Textures().find(texName) != Textures().end())
	{
		TexUsed()[texName]++;
		return { std::string(texName.begin(), texName.end()), TexIndices()[texName], true };
	}

	UploadManager::CreateEmbeddedTexture(tex.get(), embeddedTex);

	UINT index = SrvHeapAllocator.get()->Allocate();
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = SrvHeapAllocator.get()->GetCpuHandle(index);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = tex.get()->Resource->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = tex.get()->Resource.Get()->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	_device->CreateShaderResourceView(tex.get()->Resource.Get(), &srvDesc, srvHandle);

	UploadManager::ExecuteUploadCommandList();

	Textures()[texName] = std::move(tex);
	TexIndices()[texName] = index;
	TexUsed()[texName] = 1;

	return  { std::string(texName.begin(), texName.end()), index, true};
}

bool TextureManager::LoadCubeTexture(const WCHAR* texturePath, TextureHandle& cubeMapHandle)
{
	std::wstring croppedName = BasicUtil::GetCroppedName(texturePath);

	if (Textures().find(croppedName) != Textures().end())
	{
		cubeMapHandle = { std::string(croppedName.begin(), croppedName.end()), TexIndices()[croppedName], true };
		return true;
	}

	auto tex = std::make_unique<Texture>();
	tex->Name = croppedName;
	tex->Filename = texturePath;

	if (!UploadManager::CreateTexture(tex.get()))
	{
		OutputDebugStringA(("Failed to load texture: " + std::string(tex->Filename.begin(), tex->Filename.end()) + "\n").c_str());
		return false;
	}

	D3D12_RESOURCE_DESC texDesc = tex->Resource->GetDesc();
	if (texDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || texDesc.DepthOrArraySize != 6)
	{
		UploadManager::ExecuteUploadCommandList();
		tex->Resource.Reset();
		return false;
	}

	UINT index = SrvHeapAllocator.get()->Allocate();
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = SrvHeapAllocator.get()->GetCpuHandle(index);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = tex.get()->Resource->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = tex.get()->Resource->GetDesc().MipLevels;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

	_device->CreateShaderResourceView(tex.get()->Resource.Get(), &srvDesc, srvHandle);

	UploadManager::ExecuteUploadCommandList();

	Textures()[croppedName] = std::move(tex);
	TexIndices()[croppedName] = index;
	TexUsed()[croppedName] = 1;

	cubeMapHandle = { std::string(croppedName.begin(), croppedName.end()), index, true };

	return true;
}

void TextureManager::DeleteTexture(const std::wstring& name, const int texCount)
{
	TexUsed()[name] -= texCount;
	if (TexUsed()[name] == 0)
	{
		UploadManager::Flush();
		Textures()[name].release();
		Textures().erase(name);
		TexUsed().erase(name);
		SrvHeapAllocator->Free(TexIndices()[name]);
		TexIndices().erase(name);
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
	ThrowIfFailed(_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&SrvDescriptorHeap)));
	_srvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	SrvHeapAllocator = std::make_unique<DescriptorHeapAllocator>(SrvDescriptorHeap.Get(), _srvDescriptorSize, srvHeapDesc.NumDescriptors);

	//create the rtv heap
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = srvHeapDesc;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ThrowIfFailed(_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&RtvDescriptorHeap)));
	_rtvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	RtvHeapAllocator = std::make_unique<DescriptorHeapAllocator>(RtvDescriptorHeap.Get(), _rtvDescriptorSize, rtvHeapDesc.NumDescriptors);

	//create the dsv heap
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = rtvHeapDesc;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	ThrowIfFailed(_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&DsvDescriptorHeap)));
	_dsvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	DsvHeapAllocator = std::make_unique<DescriptorHeapAllocator>(DsvDescriptorHeap.Get(), _dsvDescriptorSize, dsvHeapDesc.NumDescriptors);

	LoadTexture();
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

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 3> TextureManager::GetLinearSamplers()
{
	const CD3DX12_STATIC_SAMPLER_DESC shadow(
		0, // shaderRegister (e.g. s1)
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		1,                                // maxAnisotropy
		D3D12_COMPARISON_FUNC_LESS_EQUAL,  // comparisonFunc
		D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
		0.0f,                              // minLOD
		D3D12_FLOAT32_MAX,                 // maxLOD
		D3D12_SHADER_VISIBILITY_PIXEL);    // visibility (or ALL);

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	return { shadow, linearClamp, linearWrap };
}


std::array<const CD3DX12_STATIC_SAMPLER_DESC, 1> TextureManager::GetLinearWrapSampler()
{
	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	return { linearWrap };
}

std::unordered_map<std::wstring, UINT>& TextureManager::TexIndices()
{
	static std::unordered_map<std::wstring, UINT> texIndices;
	return texIndices;
}

std::unordered_map<std::wstring, int>& TextureManager::TexUsed()
{
	static std::unordered_map<std::wstring, int> texUsed;
	return texUsed;
}

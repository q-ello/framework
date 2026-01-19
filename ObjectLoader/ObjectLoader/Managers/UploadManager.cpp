#include "UploadManager.h"

#include <DirectXTex.h>

ID3D12Device5* UploadManager::Device = nullptr;
Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> UploadManager::UploadCmdList = nullptr;
Microsoft::WRL::ComPtr<ID3D12CommandQueue> UploadManager::_commandQueue = nullptr;
Microsoft::WRL::ComPtr<ID3D12CommandAllocator> UploadManager::_uploadCmdAlloc = nullptr;
Microsoft::WRL::ComPtr<ID3D12Fence> UploadManager::_uploadFence = nullptr;
UINT64 UploadManager::_uploadFenceValue = 0;
HANDLE UploadManager::_uploadFenceEvent = nullptr;

void UploadManager::InitUploadCmdList(ID3D12Device5* device, const Microsoft::WRL::ComPtr<ID3D12CommandQueue>& cmdQueue)
{
	Device = device;
	_commandQueue = cmdQueue;

	ThrowIfFailed(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_uploadCmdAlloc)));
	ThrowIfFailed(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _uploadCmdAlloc.Get(), nullptr,
		IID_PPV_ARGS(&UploadCmdList)));
	ThrowIfFailed(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_uploadFence)));
	_uploadFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void UploadManager::ExecuteUploadCommandList()
{
	OutputDebugString(L"Executing upload command list\n");
	ThrowIfFailed(UploadCmdList->Close());
	ID3D12CommandList* cmds[] = { UploadCmdList.Get() };
	_commandQueue->ExecuteCommandLists(1, cmds);

	Flush();

	//Reset upload allocator + list for next use
	ThrowIfFailed(_uploadCmdAlloc->Reset());
	ThrowIfFailed(UploadCmdList->Reset(_uploadCmdAlloc.Get(), nullptr));
}

bool UploadManager::CreateTexture(Texture* tex)
{
    std::wstring ext = tex->Filename.substr(tex->Filename.find_last_of(L'.') + 1);
    for (auto& c : ext) c = towlower(c);

    if (ext == L"dds")
    {
        // Original path
        ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(Device,
            UploadCmdList.Get(), tex->Filename.c_str(),
            tex->Resource, tex->UploadHeap));
    }
    else
    {
        DirectX::TexMetadata metadata;
        DirectX::ScratchImage scratch;

        const HRESULT res = DirectX::LoadFromWICFile(
            tex->Filename.c_str(),
            DirectX::WIC_FLAGS_FORCE_RGB, // or _SRGB/_NONE if you care
            &metadata,
            scratch
        );

        if (FAILED(res))
        {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        const CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            metadata.format,
            static_cast<UINT>(metadata.width),
            static_cast<UINT>(metadata.height),
            static_cast<UINT16>(metadata.arraySize),
            static_cast<UINT16>(metadata.mipLevels)
        );

        const auto heapPropertiesDefault = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(Device->CreateCommittedResource(
            &heapPropertiesDefault,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&texture)));

        UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, static_cast<UINT>(metadata.mipLevels));

        Microsoft::WRL::ComPtr<ID3D12Resource> textureUploadHeap;
        const auto heapPropertiesUpload = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        const auto buffer = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
        ThrowIfFailed(Device->CreateCommittedResource(
            &heapPropertiesUpload,
            D3D12_HEAP_FLAG_NONE,
            &buffer,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&textureUploadHeap)));

        ThrowIfFailed(Device->CreateCommittedResource(
            &heapPropertiesUpload,
            D3D12_HEAP_FLAG_NONE,
            &buffer,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&tex->UploadHeap)
        ));

        const UINT numSubresources = static_cast<UINT>(metadata.mipLevels * metadata.arraySize);
        std::vector<D3D12_SUBRESOURCE_DATA> subresources;
        subresources.reserve(numSubresources);

        const DirectX::Image* images = scratch.GetImages();
        for (UINT i = 0; i < numSubresources; ++i)
        {
            D3D12_SUBRESOURCE_DATA sr = {};
            sr.pData = images[i].pixels;
            sr.RowPitch = static_cast<long long>(images[i].rowPitch);
            sr.SlicePitch = static_cast<long long>(images[i].slicePitch);
            subresources.push_back(sr);
        }

        tex->Resource = texture;

        UpdateSubresources(UploadCmdList.Get(), tex->Resource.Get(), tex->UploadHeap.Get(),
            0, 0, numSubresources, subresources.data());

        const auto resourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
            tex->Resource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_GENERIC_READ
        );
        UploadCmdList->ResourceBarrier(1, &resourceBarrier);
    }
    return true;
}

void UploadManager::CreateEmbeddedTexture(Texture* tex, const aiTexture* texture)
{
    if (!texture || !tex)
        return;

    DirectX::TexMetadata metadata;
    DirectX::ScratchImage scratch;
    
    if (texture->mHeight == 0)
    {
        // Compressed texture (PNG, JPG, etc.)
        auto imageData = reinterpret_cast<const uint8_t*>(texture->pcData);
        size_t imageSize = texture->mWidth;

        ThrowIfFailed(DirectX::LoadFromWICMemory(
            imageData,
            imageSize,
            DirectX::WIC_FLAGS_FORCE_RGB,
            &metadata,
            scratch));
    }
    else
    {
        // Raw uncompressed texture (RGBA8888)
        const aiTexel* texels = texture->pcData;

        metadata.width = texture->mWidth;
        metadata.height = texture->mHeight;
        metadata.depth = 1;
        metadata.arraySize = 1;
        metadata.mipLevels = 1;
        metadata.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;

        DirectX::Image image = {};
        image.width = metadata.width;
        image.height = metadata.height;
        image.format = metadata.format;
        image.rowPitch = metadata.width * 4;
        image.slicePitch = image.rowPitch * metadata.height;
        std::unique_ptr<uint8_t[]> pixelCopy(new uint8_t[image.slicePitch]);
        memcpy(pixelCopy.get(), texels, image.slicePitch);
        image.pixels = pixelCopy.get();

        ThrowIfFailed(scratch.InitializeFromImage(image));
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> comPtr;
    CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        metadata.format,
        static_cast<UINT>(metadata.width),
        static_cast<UINT>(metadata.height),
        static_cast<UINT16>(metadata.arraySize),
        static_cast<UINT16>(metadata.mipLevels)
    );

    const auto propertiesDefault = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(Device->CreateCommittedResource(
        &propertiesDefault,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&comPtr)));

    UINT64 uploadBufferSize = GetRequiredIntermediateSize(comPtr.Get(), 0, static_cast<UINT>(metadata.mipLevels));

    const auto propertiesUpload = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto desc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
    ThrowIfFailed(Device->CreateCommittedResource(
        &propertiesUpload,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&tex->UploadHeap)
    ));

    const UINT numSubresources = static_cast<UINT>(metadata.mipLevels * metadata.arraySize);
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    subresources.reserve(numSubresources);

    const DirectX::Image* images = scratch.GetImages();
    for (UINT i = 0; i < numSubresources; ++i)
    {
        D3D12_SUBRESOURCE_DATA sr = {};
        sr.pData = images[i].pixels;
        sr.RowPitch = static_cast<long long>(images[i].rowPitch);
        sr.SlicePitch = static_cast<long long>(images[i].slicePitch);
        subresources.push_back(sr);
    }

    tex->Resource = comPtr;

    UpdateSubresources(UploadCmdList.Get(), tex->Resource.Get(), tex->UploadHeap.Get(),
        0, 0, numSubresources, subresources.data());

    const auto resourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
                                         tex->Resource.Get(),
                                         D3D12_RESOURCE_STATE_COPY_DEST,
                                         D3D12_RESOURCE_STATE_GENERIC_READ
                                     );
    UploadCmdList->ResourceBarrier(1, &resourceBarrier);
}

void UploadManager::Flush()
{
	_uploadFenceValue++;
	ThrowIfFailed(_commandQueue->Signal(_uploadFence.Get(), _uploadFenceValue));
	if (_uploadFence->GetCompletedValue() < _uploadFenceValue)
	{
		ThrowIfFailed(_uploadFence->SetEventOnCompletion(_uploadFenceValue, _uploadFenceEvent));
		WaitForSingleObject(_uploadFenceEvent, INFINITE);
	}
}

void UploadManager::Reset()
{
	ThrowIfFailed(UploadCmdList->Close());
	ThrowIfFailed(UploadCmdList->Reset(_uploadCmdAlloc.Get(), nullptr));
}

Microsoft::WRL::ComPtr<ID3D12Resource> UploadManager::CreateUavBuffer(const UINT64 size)
{
	D3D12_RESOURCE_DESC desc;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Alignment = 0;
	desc.Width = size;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
	ThrowIfFailed(
		UploadManager::Device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&buffer)));

	return buffer;
}

Microsoft::WRL::ComPtr<ID3D12Resource> UploadManager::CreateAsBuffer(UINT64 size)
{
	// DXR requires 64 KB alignment
	size = (size + D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1) &
		   ~(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1);

	D3D12_RESOURCE_DESC desc;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Alignment = 0;
	desc.Width = size;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
	ThrowIfFailed(
		Device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
			nullptr,
			IID_PPV_ARGS(&buffer)));

	return buffer;
}

Microsoft::WRL::ComPtr<ID3D12Resource> UploadManager::CreateUploadBuffer(UINT bufferSize)
{
	Microsoft::WRL::ComPtr<ID3D12Resource> buffer;

	const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
	const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

	ThrowIfFailed(Device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&buffer)));

	return buffer;
}

Microsoft::WRL::ComPtr<ID3D12Resource> UploadManager::CreateShaderTable(const UINT64 size)
{
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = size;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE; // 🔴 IMPORTANT

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD; // 🔴 REQUIRED

	Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
	ThrowIfFailed(
		Device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, // 🔴 REQUIRED
			nullptr,
			IID_PPV_ARGS(&buffer)
		)
	);

	return buffer;
}

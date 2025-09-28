#include "UploadManager.h"

ID3D12Device* UploadManager::device = nullptr;
Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> UploadManager::uploadCmdList = nullptr;
Microsoft::WRL::ComPtr<ID3D12CommandQueue> UploadManager::_commandQueue = nullptr;
Microsoft::WRL::ComPtr<ID3D12CommandAllocator> UploadManager::_uploadCmdAlloc = nullptr;
Microsoft::WRL::ComPtr<ID3D12Fence> UploadManager::_uploadFence = nullptr;
UINT64 UploadManager::_uploadFenceValue = 0;
HANDLE UploadManager::_uploadFenceEvent = nullptr;

void UploadManager::InitUploadCmdList(ID3D12Device* Device, Microsoft::WRL::ComPtr<ID3D12CommandQueue> cmdQueue)
{
	device = Device;
	_commandQueue = cmdQueue;

	ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_uploadCmdAlloc)));
	ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _uploadCmdAlloc.Get(), nullptr,
		IID_PPV_ARGS(&uploadCmdList)));
	ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_uploadFence)));
	_uploadFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void UploadManager::ExecuteUploadCommandList()
{
	OutputDebugString(L"Executing upload command list\n");
	ThrowIfFailed(uploadCmdList->Close());
	ID3D12CommandList* cmds[] = { uploadCmdList.Get() };
	_commandQueue->ExecuteCommandLists(1, cmds);

	Flush();

	//Reset upload allocator + list for next use
	ThrowIfFailed(_uploadCmdAlloc->Reset());
	ThrowIfFailed(uploadCmdList->Reset(_uploadCmdAlloc.Get(), nullptr));
}

bool UploadManager::CreateTexture(Texture* tex)
{
    std::wstring ext = tex->Filename.substr(tex->Filename.find_last_of(L'.') + 1);
    for (auto& c : ext) c = towlower(c);

    if (ext == L"dds")
    {
        // Original path
        ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(device,
            uploadCmdList.Get(), tex->Filename.c_str(),
            tex->Resource, tex->UploadHeap));
    }
    else
    {
        DirectX::TexMetadata metadata;
        DirectX::ScratchImage scratch;

        HRESULT res = DirectX::LoadFromWICFile(
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
        CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            metadata.format,
            static_cast<UINT>(metadata.width),
            static_cast<UINT>(metadata.height),
            static_cast<UINT16>(metadata.arraySize),
            static_cast<UINT16>(metadata.mipLevels)
        );

        device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&texture));

        UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, static_cast<UINT>(metadata.mipLevels));

        Microsoft::WRL::ComPtr<ID3D12Resource> textureUploadHeap;
        device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&textureUploadHeap));

        ThrowIfFailed(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
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
            sr.RowPitch = images[i].rowPitch;
            sr.SlicePitch = images[i].slicePitch;
            subresources.push_back(sr);
        }

        tex->Resource = texture;

        UpdateSubresources(uploadCmdList.Get(), tex->Resource.Get(), tex->UploadHeap.Get(),
            0, 0, numSubresources, subresources.data());

        uploadCmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            tex->Resource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        ));
    }
}

void UploadManager::CreateEmbeddedTexture(Texture* tex, const aiTexture* embTexture)
{
    if (!embTexture || !tex)
        return;

    DirectX::TexMetadata metadata;
    DirectX::ScratchImage scratch;
    
    if (embTexture->mHeight == 0)
    {
        // Compressed texture (PNG, JPG, etc.)
        const uint8_t* imageData = reinterpret_cast<const uint8_t*>(embTexture->pcData);
        size_t imageSize = embTexture->mWidth;

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
        const aiTexel* texels = embTexture->pcData;

        metadata.width = embTexture->mWidth;
        metadata.height = embTexture->mHeight;
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

    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        metadata.format,
        static_cast<UINT>(metadata.width),
        static_cast<UINT>(metadata.height),
        static_cast<UINT16>(metadata.arraySize),
        static_cast<UINT16>(metadata.mipLevels)
    );

    device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture));

    UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, static_cast<UINT>(metadata.mipLevels));

    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
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
        sr.RowPitch = images[i].rowPitch;
        sr.SlicePitch = images[i].slicePitch;
        subresources.push_back(sr);
    }

    tex->Resource = texture;

    UpdateSubresources(uploadCmdList.Get(), tex->Resource.Get(), tex->UploadHeap.Get(),
        0, 0, numSubresources, subresources.data());

    uploadCmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        tex->Resource.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    ));
}

void UploadManager::Flush()
{
	_uploadFenceValue++;
	_commandQueue->Signal(_uploadFence.Get(), _uploadFenceValue);
	if (_uploadFence->GetCompletedValue() < _uploadFenceValue)
	{
		_uploadFence->SetEventOnCompletion(_uploadFenceValue, _uploadFenceEvent);
		WaitForSingleObject(_uploadFenceEvent, INFINITE);
	}
}

void UploadManager::Reset()
{
	ThrowIfFailed(uploadCmdList->Close());
	ThrowIfFailed(uploadCmdList->Reset(_uploadCmdAlloc.Get(), nullptr));
}

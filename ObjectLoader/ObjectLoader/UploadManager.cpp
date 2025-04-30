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
	OutputDebugString(L"Executing upload command list");
	ThrowIfFailed(uploadCmdList->Close());
	ID3D12CommandList* cmds[] = { uploadCmdList.Get() };
	_commandQueue->ExecuteCommandLists(1, cmds);

	Flush();

	//Reset upload allocator + list for next use
	ThrowIfFailed(_uploadCmdAlloc->Reset());
	ThrowIfFailed(uploadCmdList->Reset(_uploadCmdAlloc.Get(), nullptr));
}

void UploadManager::CreateTexture(Texture* tex)
{
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(device,
		uploadCmdList.Get(), tex->Filename.c_str(),
		tex->Resource, tex->UploadHeap));
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

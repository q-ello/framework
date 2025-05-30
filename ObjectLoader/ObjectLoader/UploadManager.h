#pragma once
#include "../../Common/d3dUtil.h"

class UploadManager
{
public:
	static void InitUploadCmdList(ID3D12Device* device, Microsoft::WRL::ComPtr<ID3D12CommandQueue> cmdQueue);
	static void ExecuteUploadCommandList();
	static void CreateTexture(Texture* tex);
	static void Flush();
	static void Reset();

	static ID3D12Device* device;
	static Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> uploadCmdList;
private:
	static Microsoft::WRL::ComPtr<ID3D12CommandQueue> _commandQueue;
	static Microsoft::WRL::ComPtr<ID3D12CommandAllocator> _uploadCmdAlloc;
	static Microsoft::WRL::ComPtr<ID3D12Fence> _uploadFence;
	static UINT64 _uploadFenceValue;
	static HANDLE _uploadFenceEvent;
};
#pragma once
#include "../../../Common/d3dUtil.h"
#include "./../../../Common/d3dx12.h"
#include <assimp/scene.h>

class UploadManager
{
public:
	static void InitUploadCmdList(ID3D12Device5* device, const Microsoft::WRL::ComPtr<ID3D12CommandQueue>& cmdQueue);
	static void ExecuteUploadCommandList();
	static bool CreateTexture(Texture* tex);
	static void CreateEmbeddedTexture(Texture* tex, const aiTexture* texture);
	static void Flush();
	static void Reset();

	static ID3D12Device5* Device;
	static Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> UploadCmdList;
private:
	static Microsoft::WRL::ComPtr<ID3D12CommandQueue> _commandQueue;
	static Microsoft::WRL::ComPtr<ID3D12CommandAllocator> _uploadCmdAlloc;
	static Microsoft::WRL::ComPtr<ID3D12Fence> _uploadFence;
	static UINT64 _uploadFenceValue;
	static HANDLE _uploadFenceEvent;
};
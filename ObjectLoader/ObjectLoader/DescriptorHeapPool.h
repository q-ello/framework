#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <queue>

using namespace Microsoft::WRL;

struct DescriptorHandle
{
    D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle;
};

class DescriptorHeapPool {
public:
    DescriptorHeapPool(ComPtr<ID3D12Device> device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors);

    DescriptorHandle Allocate();
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE handle);

    ComPtr<ID3D12DescriptorHeap> GetHeap() const { return m_descriptorHeap; }

    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle);

private:
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12DescriptorHeap> m_descriptorHeap;

    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_handles;
    std::queue<UINT> m_freeIndices;

    UINT m_descriptorSize;
};

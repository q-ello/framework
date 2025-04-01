#include "DescriptorHeapPool.h"
#include "../../Common/d3dUtil.h"

DescriptorHeapPool::DescriptorHeapPool(ComPtr<ID3D12Device> device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors)
    : m_device(device)
{
    // Create Descriptor Heap
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = type;
    heapDesc.NumDescriptors = numDescriptors;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_descriptorHeap)));

    // Get descriptor size
    m_descriptorSize = device->GetDescriptorHandleIncrementSize(type);

    // Initialize free indices
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < numDescriptors; ++i) {
        m_handles.push_back(handle);
        m_freeIndices.push(i);
        handle.ptr += m_descriptorSize;
    }
}

DescriptorHandle DescriptorHeapPool::Allocate() {
    if (m_freeIndices.empty())
    {
        throw std::runtime_error("Descriptor heap exhausted!");
    }

    UINT index = m_freeIndices.front();
    m_freeIndices.pop();

    DescriptorHandle handle;
    handle.CpuHandle = m_handles[index];

    // Calculate GPU handle
    handle.GpuHandle = m_descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    handle.GpuHandle.ptr += index * m_descriptorSize;  // Correctly offset GPU handle

    return handle;
}

void DescriptorHeapPool::Free(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    auto it = std::find_if(m_handles.begin(), m_handles.end(), [&](const D3D12_CPU_DESCRIPTOR_HANDLE& h) { return h.ptr == handle.ptr; });
    if (it != m_handles.end()) {
        m_freeIndices.push(static_cast<UINT>(std::distance(m_handles.begin(), it)));
    }
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeapPool::GetGPUHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) {
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE heapStart = m_descriptorHeap->GetCPUDescriptorHandleForHeapStart();

    UINT descriptorIndex = (cpuHandle.ptr - heapStart.ptr) / m_descriptorSize;
    gpuHandle.ptr += descriptorIndex * m_descriptorSize;

    return gpuHandle;
}
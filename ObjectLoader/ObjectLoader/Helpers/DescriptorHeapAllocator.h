#pragma once

#include <d3d12.h>
#include <vector>

class DescriptorHeapAllocator
{
public:
    DescriptorHeapAllocator(ID3D12DescriptorHeap* heap, const UINT descriptorSize, const UINT maxDescriptors)
        : _heap(heap)
        , _descriptorSize(descriptorSize)
        , _maxDescriptors(maxDescriptors)
    {
        _nextFreeIndex = 0;
		_freeIndices.reserve(maxDescriptors);
    }

    //Allocate a descriptor
    UINT Allocate()
    {
        if (!_freeIndices.empty())
        {
			const UINT index = _freeIndices.back();
			_freeIndices.pop_back();
			return index;
        }

		if (_nextFreeIndex >= _maxDescriptors)
		{
			throw std::exception("Descriptor Heap is full!");
		}

		return _nextFreeIndex++;
    }

	//Free a descriptor
	void Free(const UINT index)
	{
		if (index < _maxDescriptors)
		{
			_freeIndices.push_back(index);
		}
	}

    //get cpu handle
	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(const UINT index) const
	{
		D3D12_CPU_DESCRIPTOR_HANDLE handle = _heap->GetCPUDescriptorHandleForHeapStart();
		handle.ptr += static_cast<SIZE_T>(_descriptorSize * index);
		return handle;
	}

	//get gpu handle
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(const UINT index) const
	{
		D3D12_GPU_DESCRIPTOR_HANDLE handle = _heap->GetGPUDescriptorHandleForHeapStart();
		handle.ptr += static_cast<SIZE_T>(_descriptorSize * index);
		return handle;
	}
private:
	ID3D12DescriptorHeap* _heap;
	UINT _descriptorSize;
	UINT _maxDescriptors;
	UINT _nextFreeIndex;
	std::vector<UINT> _freeIndices;
};
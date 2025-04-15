#pragma once

#include <d3d12.h>
#include <vector>
#include <assert.h>

class DescriptorHeapAllocator
{
public:
    DescriptorHeapAllocator(ID3D12DescriptorHeap* heap, UINT descriptorSize, UINT maxDescriptors)
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
			UINT index = _freeIndices.back();
			_freeIndices.pop_back();
			return index;
        }

		if (_nextFreeIndex >= _maxDescriptors)
		{
			throw ("Descriptor Heap is full!");
		}

		return _nextFreeIndex++;
    }

	//Free a descriptor
	void Free(UINT index)
	{
		if (index < _maxDescriptors)
		{
			_freeIndices.push_back(index);
		}
	}

    //get cpu handle
	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(UINT index)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE handle = _heap->GetCPUDescriptorHandleForHeapStart();
		handle.ptr += index * _descriptorSize;
		return handle;
	}

	//get gpu handle
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(UINT index)
	{
		D3D12_GPU_DESCRIPTOR_HANDLE handle = _heap->GetGPUDescriptorHandleForHeapStart();
		handle.ptr += index * _descriptorSize;
		return handle;
	}
private:
	ID3D12DescriptorHeap* _heap;
	UINT _descriptorSize;
	UINT _maxDescriptors;
	UINT _nextFreeIndex;
	std::vector<UINT> _freeIndices;
};
#pragma once
#include "../Helpers/FrameResource.h"
#include "../Helpers/RenderItem.h"

class RayTracingManager
{
public:
    void UpdateData(ID3D12GraphicsCommandList4* cmdList);
    void BuildTlas(ID3D12GraphicsCommandList4* cmdList);
    ID3D12Resource* Tlas() const;

    void AddRtObject(EditableRenderItem* object);
    void DeleteRtObject(const EditableRenderItem* object);

private:
    std::vector<EditableRenderItem*> _rtObjects;

    Microsoft::WRL::ComPtr<ID3D12Resource> _tlas;
    Microsoft::WRL::ComPtr<ID3D12Resource> _scratch;

    Microsoft::WRL::ComPtr<ID3D12Resource> _instanceBuffer;
    UINT _instanceBufferSize = 0;

    UINT _instanceCount = 0;
};

#pragma once
#include "TextureManager.h"
#include "../../../Common/GBuffer.h"
#include "../Helpers/FrameResource.h"
#include "../Helpers/RenderItem.h"

class RayTracingManager
{
public:
    explicit RayTracingManager(ID3D12Device5* device, const int width, const int height);
    
    void Init();
    void BindToOtherData(GBuffer* buffer);
    void OnResize(const int newWidth, const int newHeight);
    
    void UpdateData();
    void BuildTlas();
    ID3D12Resource* Tlas() const;

    void AddRtObject(EditableRenderItem* object);
    void DeleteRtObject(const EditableRenderItem* object);

    void DispatchRays(ID3D12GraphicsCommandList4* cmdList, const FrameResource* currFrameResource);

    int ShadowMaskSrv() const;

private:
    std::vector<EditableRenderItem*> _rtObjects;

    GBuffer* _gbuffer;

    Microsoft::WRL::ComPtr<ID3D12Device5> _device = nullptr; 
    Microsoft::WRL::ComPtr<ID3D12StateObject> _rtPso = nullptr;
    Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> _rtPsoInfo = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	//actually uav srv
    RtvSrvTexture _shadowMaskTexture;
    const UINT _shaderRecord = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT; // 32 bytes
    const UINT _tableStride = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

    Microsoft::WRL::ComPtr<ID3D12Resource> _rayGenTable;
    Microsoft::WRL::ComPtr<ID3D12Resource> _missTable;
    Microsoft::WRL::ComPtr<ID3D12Resource> _hitGroupTable;

    Microsoft::WRL::ComPtr<ID3D12Resource> _tlas;
    Microsoft::WRL::ComPtr<ID3D12Resource> _scratch;

    Microsoft::WRL::ComPtr<ID3D12Resource> _instanceBuffer;
    UINT _instanceBufferSize = 0;

    int _instanceCount = -1;
    int _width;
    int _height;
    UINT _tlasSrvIndex;

    void BuildPso();
    void BuildRootSignature();
    void BuildTextures();
    void BuildShaderTables();
};

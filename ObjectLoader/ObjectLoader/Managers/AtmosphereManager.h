#pragma once
#include <d3d12.h>
#include <wrl/client.h>

#include "TextureManager.h"
#include "../Helpers/FrameResource.h"

struct FrameResource;
class Camera;
class GBuffer;
class LightingManager;

class AtmosphereManager
{
public:
    explicit AtmosphereManager(ID3D12Device* device) : Parameters(), _device{device}
    {
        _framesDirty = gNumFrameResources;
    }

    void Init(int width, int height);
    void BindToManagers(LightingManager* lightingManager, GBuffer* buffer);
    void Draw(ID3D12GraphicsCommandList* cmdList, const FrameResource* currFrameResource);
    void UpdateParameters(const FrameResource* currFrame);
    void SetDirty();
    void OnResize(int width, int height);
    void BuildTexture(int width, int height);
    
    AtmosphereConstants Parameters;

private:
    void BuildRootSignature();
    void BuildShaders();
    void BuildPso();

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> _pso;
    Microsoft::WRL::ComPtr<ID3DBlob> _ps;
    Microsoft::WRL::ComPtr<ID3DBlob> _fullscreenVs;
    DXGI_FORMAT _format = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT _depthFormat = DXGI_FORMAT_R24G8_TYPELESS;
    RtvSrvTexture _middlewareTexture;

    ID3D12Device* _device = nullptr;
    GBuffer* _gBuffer = nullptr;
    LightingManager* _lightingManager = nullptr;
    
    int _framesDirty;
    
};

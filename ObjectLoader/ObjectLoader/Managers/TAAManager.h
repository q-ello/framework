#pragma once

#include "TextureManager.h"
#include "../../../Common/d3dUtil.h"
#include "../Helpers/FrameResource.h"
#include "LightingManager.h"

class Camera;
class GBuffer;

class TaaManager
{
public:
	explicit TaaManager(ID3D12Device* device) : _device{ device } {}

	void Init(const int width, const int height);
	void BindToManagers(LightingManager* lightingManager, GBuffer* buffer, Camera* camera);
	void ApplyTaa(ID3D12GraphicsCommandList* cmdList, const FrameResource* currFrameResource);
	void OnResize(int newWidth, int newHeight);
	void UpdateTaaParameters(const FrameResource* currFrame) const;

private:
	void BuildRootSignature();
	void BuildShaders();
	void BuildPso();
	void BuildTextures();
	void ChangeHistoryState(ID3D12GraphicsCommandList* cmdList, int index, D3D12_RESOURCE_STATES newState);
	static void ChangeTextureState(ID3D12GraphicsCommandList* cmdList, RtvSrvTexture& texture, D3D12_RESOURCE_STATES newState);

private:
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _pso;
	Microsoft::WRL::ComPtr<ID3DBlob> _ps;
	Microsoft::WRL::ComPtr<ID3DBlob> _fullscreenVs;
	DXGI_FORMAT _format = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT _depthFormat = DXGI_FORMAT_R24G8_TYPELESS;

	ID3D12Device* _device = nullptr;
	GBuffer* _gBuffer = nullptr;
	LightingManager* _lightingManager = nullptr;
	Camera* _camera = nullptr;

	int _width = 0;
	int _height = 0;

	RtvSrvTexture _historyTextures[2];
	int _currentHistoryIndex = 0;
};

#pragma once

#include "TextureManager.h"
#include "../../Common/d3dUtil.h"
#include "FrameResource.h"
#include "LightingManager.h"

class TAAManager
{
public:
	TAAManager(ID3D12Device* device) : _device{ device } {}
	~TAAManager();

	void Init(int width, int height);
	void BindToManagers(LightingManager* lightingManager, GBuffer* gBuffer, Camera* camera);
	void ApplyTAA(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, bool taaEnabled);
	void OnResize(int newWidth, int newHeight);
	void UpdateTAAParameters(FrameResource* currFrame, DirectX::XMFLOAT4X4 PrevViewProj, DirectX::XMFLOAT4X4 CurrInvViewProj);

private:
	void BuildRootSignature();
	void BuildShaders();
	void BuildPSO();
	void BuildTextures();
	void ChangeHistoryState(ID3D12GraphicsCommandList* cmdList, int index, D3D12_RESOURCE_STATES newState);

private:
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _PSO;
	Microsoft::WRL::ComPtr<ID3DBlob> _PS;
	Microsoft::WRL::ComPtr<ID3DBlob> _fullscreenVS;
	DXGI_FORMAT _format = DXGI_FORMAT_R8G8B8A8_UNORM;

	ID3D12Device* _device = nullptr;
	GBuffer* _gBuffer = nullptr;
	LightingManager* _lightingManager = nullptr;
	Camera* _camera = nullptr;

	int _width = 0;
	int _height = 0;

	RtvSrvTexture _historyTextures[2];
	int _currentHistoryIndex = 0;

	bool _historyValid = false;
};
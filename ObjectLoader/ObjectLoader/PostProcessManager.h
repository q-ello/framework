#pragma once

#include "TextureManager.h"
#include "../../Common/d3dUtil.h"
#include "LightingManager.h"
#include "../../Common/GBuffer.h"

class PostProcessManager
{
public:
	PostProcessManager(ID3D12Device* device) 
		: _device{ device }
	{};
	~PostProcessManager() = default;
	void Init();
	void BindToManagers(GBuffer* gbuffer, LightingManager* lightingManager);

	void GodRaysPass(ID3D12GraphicsCommandList* cmdList);

	void OnResize(int newWidth, int newHeight);
private:
	void BuildRootSignature();
	void BuildShaders();
	void BuildPSOs();
	void BuildTextures();

	//god rays
	RtvSrvTexture _lightOcclusionMask;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _lightOcclusionMaskRootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _lightOcclusionMaskPSO;
	Microsoft::WRL::ComPtr<ID3DBlob> _lightOcclusionMaskVS;
	Microsoft::WRL::ComPtr<ID3DBlob> _lightOcclusionMaskPS;

	DXGI_FORMAT _format = DXGI_FORMAT_R8G8B8A8_UNORM;

	ID3D12Device* _device = nullptr;
	GBuffer* _gBuffer = nullptr;
	LightingManager* _lightingManager = nullptr;
	int _width = 0;
	int _height = 0;

};
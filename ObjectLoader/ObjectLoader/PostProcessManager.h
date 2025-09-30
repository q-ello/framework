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
	~PostProcessManager() {};
	void Init(int width, int height);
	void BindToManagers(GBuffer* gbuffer, LightingManager* lightingManager, Camera* camera);

	void DrawGodRaysPass(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);
	void OcclusionMaskPass(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);
	void GodRaysPass(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);
	void DrawSSR(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);

	void OnResize(int newWidth, int newHeight);
	void UpdateGodRaysParameters();
	void UpdateSSRParameters(FrameResource* currFrame);

	GodRaysConstants GodRaysParameters;
	SSRConstants SSRParameters;
private:
	void BuildRootSignature();
	void BuildShaders();
	void BuildPSOs();
	void BuildTextures();
	void CreateSSRTexture();

	void SetNewResolutionAndRects(int newWidth, int newHeight);

	//light occlusion mask
	RtvSrvTexture _lightOcclusionMask;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _lightOcclusionMaskRootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _lightOcclusionMaskPSO;
	Microsoft::WRL::ComPtr<ID3DBlob> _lightOcclusionMaskPS;
	DXGI_FORMAT _lightOcclusionMaskFormat = DXGI_FORMAT_R8_UNORM;
	int _lightOcclusionMaskWidth = 0;
	int _lightOcclusionMaskHeight = 0;
	D3D12_VIEWPORT _occlusionMaskViewport{ 0, 0, 0, 0, 0, 1 };
	D3D12_RECT _occlusionMaskScissorRect{ 0, 0, 0, 0 };
	
	//god rays
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _godRaysRootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _godRaysPSO;
	Microsoft::WRL::ComPtr<ID3DBlob> _godRaysPS;

	//screen space reflection
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _ssrRootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _ssrPSO;
	Microsoft::WRL::ComPtr<ID3DBlob> _ssrVS;
	Microsoft::WRL::ComPtr<ID3DBlob> _ssrPS;
	RtvSrvTexture _ssrTexture;

	Microsoft::WRL::ComPtr<ID3DBlob> _fullscreenVS;
	DXGI_FORMAT _format = DXGI_FORMAT_R8G8B8A8_UNORM;
	D3D12_VIEWPORT _viewport{ 0, 0, 0, 0, 0, 1 };
	D3D12_RECT _scissorRect{ 0, 0, 0, 0 };

	ID3D12Device* _device = nullptr;
	GBuffer* _gBuffer = nullptr;
	LightingManager* _lightingManager = nullptr;
	Camera* _camera = nullptr;

	int _width = 0;
	int _height = 0;
};
#pragma once

#include "TextureManager.h"
#include "../../../Common/d3dUtil.h"
#include "LightingManager.h"
#include "../../../Common/GBuffer.h"

class PostProcessManager
{
public:
	explicit PostProcessManager(ID3D12Device* device)
		: GodRaysParameters(), _device{device}
	{
	};
	PostProcessManager(const PostProcessManager& postProcessManager) = delete;
	PostProcessManager& operator=(const PostProcessManager&) = delete;
	PostProcessManager(const PostProcessManager&& postProcessManager) = delete;
	PostProcessManager& operator=(const PostProcessManager&& postProcessManager) = delete;
	~PostProcessManager() = default;
	void Init(int width, int height);
	void BindToManagers(GBuffer* gbuffer, LightingManager* lightingManager, Camera* camera);

	void DrawGodRaysPass(ID3D12GraphicsCommandList* cmdList, const FrameResource* currFrameResource) const;
	void OcclusionMaskPass(ID3D12GraphicsCommandList* cmdList, const FrameResource* currFrameResource) const;
	void GodRaysPass(ID3D12GraphicsCommandList* cmdList, const FrameResource* currFrameResource) const;
	void DrawSsr(ID3D12GraphicsCommandList* cmdList, const FrameResource* currFrameResource) const;
	void DrawChromaticAberration(ID3D12GraphicsCommandList* cmdList, const FrameResource* currFrameResource) const;
	void DrawVignetting(ID3D12GraphicsCommandList* cmdList, const FrameResource* currFrameResource) const;

	void OnResize(int newWidth, int newHeight);
	void UpdateGodRaysParameters() const;
	void UpdateSsrParameters(const FrameResource* currFrame);

	GodRaysConstants GodRaysParameters;
	SsrConstants SsrParameters;
	float ChromaticAberrationStrength = 0.05f;
	float VignettingPower = 1.f;
private:
	void BuildRootSignature();
	void BuildShaders();
	void BuildPsOs();
	void BuildTextures();
	static void ChangeTextureState(ID3D12GraphicsCommandList* cmdList, const RtvSrvTexture& texture, D3D12_RESOURCE_STATES state);

	void SetNewResolutionAndRects(int newWidth, int newHeight);

	//light occlusion mask
	RtvSrvTexture _lightOcclusionMask;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _lightOcclusionMaskRootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _lightOcclusionMaskPso;
	Microsoft::WRL::ComPtr<ID3DBlob> _lightOcclusionMaskPs;
	DXGI_FORMAT _lightOcclusionMaskFormat = DXGI_FORMAT_R8_UNORM;
	int _lightOcclusionMaskWidth = 0;
	int _lightOcclusionMaskHeight = 0;
	D3D12_VIEWPORT _occlusionMaskViewport{ 0, 0, 0, 0, 0, 1 };
	D3D12_RECT _occlusionMaskScissorRect{ 0, 0, 0, 0 };
	
	//god rays
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _godRaysRootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _godRaysPso;
	Microsoft::WRL::ComPtr<ID3DBlob> _godRaysPs;

	//screen space reflection
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _ssrRootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _ssrPso;
	Microsoft::WRL::ComPtr<ID3DBlob> _ssrPs;
	RtvSrvTexture _ssrTexture;

	//chromatic aberration
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _chromaticAndVignettingRootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _chromaticAberrationPso;
	Microsoft::WRL::ComPtr<ID3DBlob> _chromaticAberrationPs;
	RtvSrvTexture _chromaticAberrationTexture;

	//vignetting
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _vignettingPso;
	Microsoft::WRL::ComPtr<ID3DBlob> _vignettingPs;
	RtvSrvTexture _vignettingTexture;

	Microsoft::WRL::ComPtr<ID3DBlob> _fullscreenLightVs;
	Microsoft::WRL::ComPtr<ID3DBlob> _fullscreenVs;
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
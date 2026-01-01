#pragma once
#define NOMINMAX
#include "../../../Common/d3dUtil.h"
#include "../Helpers/FrameResource.h"
#include "GeometryManager.h"
#include "../Helpers/Camera.h"
#include "TextureManager.h"
#include "CubeMapManager.h"
#include "../../../Common/GBuffer.h"

struct ShadowTextureArray
{
	Microsoft::WRL::ComPtr<ID3D12Resource> TextureArray;
	int Srv = -1;
};

struct LightRenderItem
{
	Light LightData;
	BoundingSphere Bounds;
	int NumFramesDirty = gNumFrameResources;
	int LightIndex = -1;
	int ShadowMapDsv = -1;
};

class LightingManager
{
public:
	LightingManager(ID3D12Device* device, UINT width, UINT height, bool rayTracingSupported);
	~LightingManager() = default;

	void AddLight(ID3D12Device* device);
	void DeleteLight(int deletedLight);

	void UpdateDirectionalLightCb(const FrameResource* currFrameResource);
	void UpdateLightCBs(const FrameResource* currFrameResource);
	void UpdateWorld(int lightIndex) const;

	int LightsCount() const;
	LightRenderItem* GetLight(int i) const;
	void CalculateCascadesViewProjs();

	//different draw calls
	void DrawDirLight(ID3D12GraphicsCommandList4* cmdList, const FrameResource* currFrameResource, bool rayTracingEnabled);
	void DrawLocalLights(ID3D12GraphicsCommandList4* cmdList, const FrameResource* currFrameResource, bool rayTracingEnabled) const;
	void DrawDebug(ID3D12GraphicsCommandList4* cmdList, FrameResource* currFrameResource) const;
	void DrawEmissive(ID3D12GraphicsCommandList4* cmdList, FrameResource* currFrameResource) const;
	void DrawShadows(ID3D12GraphicsCommandList4* cmdList, FrameResource* currFrameResource, const std::vector<std::shared_ptr<EditableRenderItem>>& objects);
	void DrawIntoBackBuffer(ID3D12GraphicsCommandList4* cmdList, FrameResource* currFrameResource);

	void Init();
	void BindToOtherData(GBuffer* gbuffer, CubeMapManager* cubeMapManager, Camera* camera, const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputLayout);
	void OnResize(UINT newWidth, UINT newHeight);

	bool* IsMainLightOn()
	{
		return &_isMainLightOn;
	}

	float* MainLightDirection()
	{
		return &_mainLightDirection.x;
	}

	void SetMainLightDirection(const DirectX::XMFLOAT3 newDir)
	{
		_mainLightDirection = newDir;
	}

	float* MainLightColor()
	{
		return &_dirLightColor.x;
	}

	bool* DebugEnabled()
	{
		return &_debugEnabled;
	}

	Light* MainSpotlight()
	{
		return &_handSpotlight;
	}

	int LightsInsideFrustum() const
	{
		return static_cast<int>(_lightsInsideFrustum.size());
	}

	ID3DBlob* GetFullScreenVsWithSamplers() const;

	ID3DBlob* GetFullScreenVs() const
	{
		return _finalPassVsShader.Get();
	}

	D3D12_GPU_DESCRIPTOR_HANDLE GetCascadeShadowSrv() const
	{
		return TextureManager::SrvHeapAllocator->GetGpuHandle(_cascadeShadowTextureArray.Srv);
	}

	void SetFinalTextureIndex(const int newIndex)
	{
		_finalTextureSrvIndex = newIndex;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetMiddlewareRtv() const
	{
		return TextureManager::RtvHeapAllocator->GetCpuHandle(_middlewareTexture.OtherIndex);
	}

	ID3D12Resource* GetMiddlewareTexture() const
	{
		return _middlewareTexture.Resource.Get();
	}

	D3D12_GPU_DESCRIPTOR_HANDLE GetFinalTextureSrv() const
	{
		return TextureManager::SrvHeapAllocator->GetGpuHandle(_finalTextureSrvIndex);
	}

	void ChangeMiddlewareState(ID3D12GraphicsCommandList4* cmdList, D3D12_RESOURCE_STATES newState);

	//shadow mask stuff
	void AddShadowMask(const TextureHandle& handle);
	void DeleteShadowMask(size_t i);
	void SetSelectedShadowMask(size_t i)
	{
		_selectedShadowMask = i;
	}
	std::string ShadowMaskName(const size_t i) const
	{
		return _shadowMasks[i].Name;
	}
	size_t SelectedShadowMask() const
	{
		return _selectedShadowMask;
	}
	size_t ShadowMaskCount() const
	{
		return _shadowMasks.size();
	}
	float ShadowMaskUvScale = 1.f;

private:
	ID3D12Device* _device = nullptr;

	//shadow cascades for directional light
	CascadeOnCpu _cascades[gCascadesCount];
	ShadowTextureArray _cascadeShadowTextureArray;
	ShadowTextureArray _localLightsShadowTextureArray;

	//dirlight data
	DirectX::XMFLOAT3 _mainLightDirection = { 1.f, -1.f, 0.f };
	bool _isMainLightOn = true;
	DirectX::XMFLOAT3 _dirLightColor = { 1.f, 1.f, 1.f };

	bool _debugEnabled = false;

	bool _rayTracingSupported = false;

	//locallights data
	std::vector<std::unique_ptr<LightRenderItem>> _localLights;
	std::vector<int> _freeLightIndices;
	int _nextAvailableLightIndex = 0;
	const int _maxLights = 512;

	//hand spotlight
	Light _handSpotlight = Light(false);

	Camera* _camera = nullptr;
	CubeMapManager* _cubeMapManager = nullptr;
	GBuffer* _gbuffer = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignatureCsm;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignatureRt;
	
	//dirlight pso
	Microsoft::WRL::ComPtr<ID3DBlob> _dirLightVsShader;
	//cascade shadow maps
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _dirLightPsoCsm;
	Microsoft::WRL::ComPtr<ID3DBlob> _dirLightPsShaderCsm;
	//ray tracing
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _dirLightPsoRt;
	Microsoft::WRL::ComPtr<ID3DBlob> _dirLightPsShaderRt;

	//local lights pso
	std::vector<D3D12_INPUT_ELEMENT_DESC> _localLightsInputLayout;
	Microsoft::WRL::ComPtr<ID3DBlob> _localLightsVsShader;
	//cascade shadow maps
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _localLightsPsoCsm;
	Microsoft::WRL::ComPtr<ID3DBlob> _localLightsPsShaderCsm;
	//ray tracing
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _localLightsPsoRt;
	Microsoft::WRL::ComPtr<ID3DBlob> _localLightsPsShaderRt;

	//local lights wireframe pso
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _localLightsWireframePso;
	Microsoft::WRL::ComPtr<ID3DBlob> _localLightsWireframePsShader;

	//emissive pso
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _emissivePso;
	Microsoft::WRL::ComPtr<ID3DBlob> _emissivePsShader;

	//shadow pso
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _shadowPso;
	std::vector<D3D12_INPUT_ELEMENT_DESC> _shadowInputLayout;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _shadowRootSignature;
	Microsoft::WRL::ComPtr<ID3DBlob> _shadowVsShader;

	//shadow rects
	D3D12_VIEWPORT _shadowViewport{ 0, 0, 0, 0, 0, 1 };
	D3D12_RECT _shadowScissorRect{ 0, 0, 0, 0 };
	float _shadowMapResolution{1028.f};
	UINT _shadowCbSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ShadowLightConstants));

	//lights culling
	std::vector<int> _lightsInsideFrustum{};
	std::vector<int> _lightsContainingFrustum{};

	//middleware to backbuffer
	RtvSrvTexture _middlewareTexture;
	DXGI_FORMAT _middlewareTextureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	UINT _width;
	UINT _height;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _finalPassPso;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _finalPassRootSignature;
	Microsoft::WRL::ComPtr<ID3DBlob> _finalPassVsShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _finalPassPsShader;

	int _finalTextureSrvIndex = -1;

	//shadow mask stuff
	std::vector<TextureHandle> _shadowMasks;
	size_t _selectedShadowMask = -1;
private:
	//default functions
	void BuildInputLayout();
	void BuildRootSignature();
	void BuildShaders();
	void BuildPso();
	void BuildDescriptors();

	//helpers
	int CreateShadowTextureDsv(bool forCascade, int index) const;
	static void DeleteShadowTexture(int texDsv);
	std::vector<int> FrustumCulling(const std::vector<std::shared_ptr<EditableRenderItem>>& objects, int cascadeIdx) const;
	static std::vector<int> FrustumCulling(const std::vector<std::shared_ptr<EditableRenderItem>>& objects, DirectX::BoundingSphere lightAabb);
	static void ShadowPass(FrameResource* currFrameResource, ID3D12GraphicsCommandList4* cmdList,
	                       const std::vector<int>& visibleObjects, const std::vector<std::shared_ptr<EditableRenderItem>>& objects);
	void SnapToTexel(DirectX::XMFLOAT3& minPt, DirectX::XMFLOAT3& maxPt) const;
	void CreateMiddlewareTexture();
};
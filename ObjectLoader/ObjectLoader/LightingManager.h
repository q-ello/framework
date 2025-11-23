#pragma once
#define NOMINMAX
#include "../../Common/d3dUtil.h"
#include "FrameResource.h"
#include "UploadManager.h"
#include "GeometryManager.h"
#include "Camera.h"
#include "TextureManager.h"
#include "CubeMapManager.h"
#include "../../Common/GBuffer.h"
#include <DirectXColors.h>

struct ShadowTextureArray
{
	Microsoft::WRL::ComPtr<ID3D12Resource> textureArray;
	int SRV = -1;
};

struct LightRenderItem
{
	Light LightData;
	BoundingSphere Bounds;
	int NumFramesDirty = gNumFrameResources;
	int LightIndex = -1;
	int ShadowMapDSV = -1;
};

class LightingManager
{
public:
	LightingManager(ID3D12Device* device, UINT width, UINT height);
	~LightingManager();

	void AddLight(ID3D12Device* device);
	void DeleteLight(int deletedLight);

	void UpdateDirectionalLightCB(FrameResource* currFrameResource);
	void UpdateLightCBs(FrameResource* currFrameResource);
	void UpdateWorld(int lightIndex);

	int LightsCount();
	LightRenderItem* GetLight(int i);
	void CalculateCascadesViewProjs();

	//different draw calls
	void DrawDirLight(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);
	void DrawLocalLights(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);
	void DrawDebug(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);
	void DrawEmissive(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);
	void DrawShadows(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, std::vector<std::shared_ptr<EditableRenderItem>>& objects);
	void DrawIntoBackBuffer(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);

	void Init();
	void BindToOtherData(GBuffer* gbuffer, CubeMapManager* cubeMapManager, Camera* camera, std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout);
	void OnResize(UINT newWidth, UINT newHeight);

	bool* IsMainLightOn()
	{
		return &_isMainLightOn;
	}

	float* MainLightDirection()
	{
		return &_mainLightDirection.x;
	}

	void SetMainLightDirection(DirectX::XMFLOAT3 newDir)
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
		return (int)_lightsInsideFrustum.size();
	}

	ID3DBlob* GetFullScreenVSWithSamplers()
	{
		return _dirLightVSShader.Get();
	}

	ID3DBlob* GetFullScreenVS()
	{
		return _finalPassVSShader.Get();
	}

	D3D12_GPU_DESCRIPTOR_HANDLE GetCascadeShadowSRV() const
	{
		return TextureManager::srvHeapAllocator->GetGpuHandle(_cascadeShadowTextureArray.SRV);
	}

	void SetFinalTextureIndex(int newIndex)
	{
		_finalTextureSrvIndex = newIndex;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetMiddlewareRTV() const
	{
		return TextureManager::rtvHeapAllocator->GetCpuHandle(_middlewareTexture.otherIndex);
	}

	ID3D12Resource* GetMiddlewareTexture() const
	{
		return _middlewareTexture.Resource.Get();
	}

	D3D12_GPU_DESCRIPTOR_HANDLE GetFinalTextureSRV() const
	{
		return TextureManager::srvHeapAllocator->GetGpuHandle(_finalTextureSrvIndex);
	}

	void ChangeMiddlewareState(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES newState);

	//shadow mask stuff
	void AddShadowMask(TextureHandle handle);
	void DeleteShadowMask(size_t i);
	void SetSelectedShadowMask(size_t i)
	{
		_selectedShadowMask = i;
	}
	std::string ShadowMaskName(size_t i) const
	{
		return _shadowMasks[i].name;
	}
	size_t SelectedShadowMask() const
	{
		return _selectedShadowMask;
	}
	size_t ShadowMaskCount() const
	{
		return _shadowMasks.size();
	}
	float shadowMaskUVScale = 1.f;

private:
	ID3D12Device* _device = nullptr;

	//shadow cascades for directional light
	CascadeOnCPU _cascades[gCascadesCount];
	ShadowTextureArray _cascadeShadowTextureArray;
	ShadowTextureArray _localLightsShadowTextureArray;

	//dirlight data
	DirectX::XMFLOAT3 _mainLightDirection = { 1.f, -1.f, 0.f };
	bool _isMainLightOn = true;
	DirectX::XMFLOAT3 _dirLightColor = { 1.f, 1.f, 1.f };

	bool _debugEnabled = false;

	//locallights data
	std::vector<std::unique_ptr<LightRenderItem>> _localLights;
	std::vector<int> FreeLightIndices;
	int NextAvailableLightIndex = 0;
	const int MaxLights = 512;

	//hand spotlight
	Light _handSpotlight = Light(false);

	Camera* _camera = nullptr;
	CubeMapManager* _cubeMapManager = nullptr;
	GBuffer* _gbuffer = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;

	//dirlight pso
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _dirLightPSO;
	Microsoft::WRL::ComPtr<ID3DBlob> _dirLightVSShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _dirLightPSShader;

	//local lights pso
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _localLightsPSO;
	std::vector<D3D12_INPUT_ELEMENT_DESC> _localLightsInputLayout;
	Microsoft::WRL::ComPtr<ID3DBlob> _localLightsVSShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _localLightsPSShader;

	//local lights wireframe pso
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _localLightsWireframePSO;
	Microsoft::WRL::ComPtr<ID3DBlob> _localLightsWireframePSShader;

	//emissive pso
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _emissivePSO;
	Microsoft::WRL::ComPtr<ID3DBlob> _emissivePSShader;

	//shadow pso
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _shadowPSO;
	std::vector<D3D12_INPUT_ELEMENT_DESC> _shadowInputLayout;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _shadowRootSignature;
	Microsoft::WRL::ComPtr<ID3DBlob> _shadowVSShader;

	//shadow rects
	D3D12_VIEWPORT _shadowViewport{ 0, 0, 0, 0, 0, 1 };
	D3D12_RECT _shadowScissorRect{ 0, 0, 0, 0 };
	float _shadowMapResolution{1028.f};
	UINT _shadowCBSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ShadowLightConstants));

	//lights culling
	std::vector<int> _lightsInsideFrustum{};
	std::vector<int> _lightsContainingFrustum{};

	//middleware to backbuffer
	RtvSrvTexture _middlewareTexture;
	DXGI_FORMAT _middlewareTextureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	UINT _width;
	UINT _height;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _finalPassPSO;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _finalPassRootSignature;
	Microsoft::WRL::ComPtr<ID3DBlob> _finalPassVSShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _finalPassPSShader;

	int _finalTextureSrvIndex = -1;
private:
	//default functions
	void BuildInputLayout();
	void BuildRootSignature();
	void BuildShaders();
	void BuildPSO();
	void BuildDescriptors();

	//helpers
	int CreateShadowTextureDSV(bool forCascade, int index);
	void DeleteShadowTexture(int texDSV);
	std::vector<int> FrustumCulling(std::vector<std::shared_ptr<EditableRenderItem>>& objects, int cascadeIdx) const;
	std::vector<int> FrustumCulling(std::vector<std::shared_ptr<EditableRenderItem>>& objects, DirectX::BoundingSphere lightAABB);
	void ShadowPass(FrameResource* currFrameResource, ID3D12GraphicsCommandList* cmdList, std::vector<int> visibleObjects, std::vector<std::shared_ptr<EditableRenderItem>>& objects);
	void SnapToTexel(DirectX::XMFLOAT3& minPt, DirectX::XMFLOAT3& maxPt) const;
	void CreateMiddlewareTexture();

	//shadow mask stuff
	std::vector<TextureHandle> _shadowMasks;
	size_t _selectedShadowMask = -1;
};
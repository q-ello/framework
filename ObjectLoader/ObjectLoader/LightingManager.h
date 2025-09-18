#pragma once
#define NOMINMAX
#include "../../Common/d3dUtil.h"
#include "FrameResource.h"
#include "UploadManager.h"
#include "GeometryManager.h"
#include "Camera.h"

struct ShadowTexture
{
	Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
	int DSV;
	int SRV;
};

struct LightRenderItem
{
	Light LightData;
	BoundingSphere Bounds;
	int NumFramesDirty = gNumFrameResources;
	int LightIndex = -1;
	ShadowTexture ShadowMap;
};

class LightingManager
{
public:
	LightingManager(ID3D12Device* device);
	~LightingManager();

	void AddLight(ID3D12Device* device);
	void DeleteLight(int deletedLight);

	void UpdateDirectionalLightCB(FrameResource* currFrameResource);
	void UpdateLightCBs(FrameResource* currFrameResource);
	void UpdateWorld(int lightIndex);

	int LightsCount();
	LightRenderItem* GetLight(int i);
	DirectX::XMMATRIX CalculateMainLightViewProj();


	//different draw calls
	void DrawDirLight(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, D3D12_GPU_DESCRIPTOR_HANDLE descTable);
	void DrawLocalLights(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);
	void DrawDebug(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);
	void DrawEmissive(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);
	void DrawShadows(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, std::vector<std::shared_ptr<EditableRenderItem>>& objects);

	void Init(int srvAmount);

	bool* IsMainLightOn()
	{
		return &_isMainLightOn;
	}

	float* MainLightDirection()
	{
		return &_mainLightDirection.x;
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

	void SetData(Camera* camera, std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout)
	{
		_camera = camera;
		_shadowInputLayout = inputLayout;
	}

private:
	ID3D12Device* _device = nullptr;

	//dirlight data
	DirectX::XMFLOAT3 _mainLightDirection = { 1.f, -1.f, 0.f };
	bool _isMainLightOn = true;
	DirectX::XMFLOAT3 _dirLightColor = { 1.f, 1.f, 1.f };
	ShadowTexture _dirLightShadowMap;

	bool _debugEnabled = false;

	//locallights data
	std::vector<std::unique_ptr<LightRenderItem>> _localLights;
	std::vector<int> FreeLightIndices;
	int NextAvailableIndex = 0;
	const int MaxLights = 512;

	//hand spotlight
	Light _handSpotlight = Light(false);

	Camera* _camera;

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

	//texture managers
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _shadowDSVHeap;
	std::unique_ptr<DescriptorHeapAllocator> _shadowDSVAllocator;

	std::vector<int> _lightsInsideFrustum{};
	std::vector<int> _lightsContainingFrustum{};

	//default functions
	void BuildInputLayout();
	void BuildRootSignature(int srvAmount);
	void BuildShaders();
	void BuildPSO();

	//helpers
	ShadowTexture CreateShadowTexture();
	void DeleteShadowTexture(ShadowTexture tex);
	std::vector<int> FrustumCulling(std::vector<std::shared_ptr<EditableRenderItem>>& objects, DirectX::BoundingBox lightAABB);
	std::vector<int> FrustumCulling(std::vector<std::shared_ptr<EditableRenderItem>>& objects, DirectX::BoundingSphere lightAABB);
	BoundingBox CalculateDirLightAABB();
	void ShadowPass(FrameResource* currFrameResource, ID3D12GraphicsCommandList* cmdList, std::vector<int> visibleObjects, std::vector<std::shared_ptr<EditableRenderItem>>& objects);
};
#pragma once
#include "../../Common/d3dUtil.h"
#include "FrameResource.h"
#include "UploadManager.h"
#include "GeometryManager.h"

struct LightRenderItem
{
	Light LightData;
	int NumFramesDirty = gNumFrameResources;
	int LightIndex = -1;
};


class LightingManager
{
public:
	LightingManager();
	~LightingManager();

	void addLight(ID3D12Device* device);
	void deleteLight(int deletedLight);

	void UpdateDirectionalLightCB(FrameResource* currFrameResource);
	void UpdateLightCBs(FrameResource* currFrameResource);
	void UpdateWorld(int lightIndex);

	int lightsCount();
	LightRenderItem* light(int i);

	void DrawDirLight(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, D3D12_GPU_DESCRIPTOR_HANDLE descTable);
	void DrawLocalLights(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);
	void DrawDebug(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);
	void Init(int srvAmount, ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle);

	bool* isMainLightOn()
	{
		return &_isMainLightOn;
	}

	float* mainLightDirection()
	{
		return &_mainLightDirection.x;
	}

	float* mainLightColor()
	{
		return &_dirLightColor.x;
	}

	bool* debugEnabled()
	{
		return &_debugEnabled;
	}

	Light* mainSpotlight()
	{
		return &_handSpotlight;
	}

private:
	DirectX::XMFLOAT3 _mainLightDirection = { 1.f, -1.f, 0.f };
	bool _isMainLightOn = true;
	DirectX::XMFLOAT3 _dirLightColor = { 1.f, 1.f, 1.f };
	DirectionalLightConstants _dirLightCB;
	bool _debugEnabled = false;

	std::vector<std::unique_ptr<LightRenderItem>> _localLights;
	std::vector<int> FreeLightIndices;
	int NextAvailableIndex = 0;
	const int MaxLights = 512;

	//hand spotlight
	Light _handSpotlight = Light(false);

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> _dirLightPSO;
	Microsoft::WRL::ComPtr<ID3DBlob> _dirLightVSShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _dirLightPSShader;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> _localLightsPSO;
	std::vector<D3D12_INPUT_ELEMENT_DESC> _localLightsInputLayout;
	Microsoft::WRL::ComPtr<ID3DBlob> _localLightsVSShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _localLightsPSShader;
	Microsoft::WRL::ComPtr<ID3D12Resource> _lightBufferGPU;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> _localLightsWireframePSO;
	Microsoft::WRL::ComPtr<ID3DBlob> _localLightsWireframePSShader;

	void BuildInputLayout();
	void BuildRootSignature(int srvAmount);
	void BuildShaders();
	void BuildPSO();
};
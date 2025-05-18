#pragma once
#include "../../Common/d3dUtil.h"
#include "FrameResource.h"
#include "UploadManager.h"

/*
* TODOS:
* - checkbox: turn on/off directional light
* - the direction of directional light
* - add ambience?
* - add point/spot lights
* - change transform? need to think. I guess radius and maybe the angle??
* - change color
* - add lightRenderItem for every light and do everything else, with analogy to object render items? store world matrix?
* - draw it blending with indexed instanced
* - everything else is a work in the shader, we'll get to that
* - two draws: one for small lights and the other one for directional lighting
*/

struct Light
{
    int type;
    DirectX::XMFLOAT3 position;
    float radius;
    DirectX::XMFLOAT3 direction;
    float angle;
    DirectX::XMFLOAT3 color;
    float intensity;
    bool active;
};

struct LightGrid
{
	int offset = 0;
	int lightCount = 0;
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
	void AddLightToResource(Microsoft::WRL::ComPtr<ID3D12Device> device, FrameResource* currFrameResource);
	int lightsCount();
	Light* light(int i);
	void Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, D3D12_GPU_DESCRIPTOR_HANDLE descTable);
	void Init(int srvAmount);

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

private:
	DirectX::XMFLOAT3 _mainLightDirection = { 1.f, -1.f, 0.f };
	bool _isMainLightOn = true;
	DirectX::XMFLOAT3 _dirLightColor = { 1.f, 1.f, 1.f };
	DirectionalLightConstants _dirLightCB;

	std::vector<std::unique_ptr<Light>> _localLights;

	std::uint32_t uidCount;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> _dirLightPSO;
	Microsoft::WRL::ComPtr<ID3DBlob> _dirLightVSShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _dirLightPSShader;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> _localLightsPSO;
	std::vector<D3D12_INPUT_ELEMENT_DESC> _localLightsInputLayout;
	Microsoft::WRL::ComPtr<ID3DBlob> _localLightsVSShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _localLightsPSShader;

	void BuildInputLayout();
	void BuildRootSignature(int srvAmount);
	void BuildShaders();
	void BuildPSO();
};
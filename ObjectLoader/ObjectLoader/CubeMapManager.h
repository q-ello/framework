#pragma once
#include "wrl.h"
#include "d3d12.h"
#include "../../Common/d3dUtil.h"
#include "Material.h"
#include "RenderItem.h"
#include "FrameResource.h"
#include "GeometryManager.h"

enum class CubeMap
{
	Skybox = 0,
	Irradiance,
	Prefiltered,
	BRDF,
	Count
};

class CubeMapManager
{
public:
	CubeMapManager(ID3D12Device* device);
	~CubeMapManager();
	void Init();

	void AddObjectToResource(FrameResource* currFrameResource);
	void Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);

	std::string EnvironmentName(CubeMap type) const
	{
		return _maps[(int)type].name;
	}

	void AddMap(CubeMap type, TextureHandle handle);

	D3D12_GPU_DESCRIPTOR_HANDLE GetCubeMapGPUHandle();
	D3D12_GPU_DESCRIPTOR_HANDLE GetIBLMapsGPUHandle();

private:
	ID3D12Device* _device = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _pso;
	Microsoft::WRL::ComPtr<ID3DBlob> _vsShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _psShader;
	std::vector<D3D12_INPUT_ELEMENT_DESC> _inputLayout;
	std::unique_ptr<EditableRenderItem> _skyRItem;
	UINT _cbSize = d3dUtil::CalcConstantBufferByteSize(sizeof(StaticObjectConstants));
	std::vector<TextureHandle> _maps;

	D3D12_VIEWPORT _viewport{ 0, 0, 0, 0, 0, 1 };
	D3D12_RECT _scissorsRect{ 0, 0, 0, 0 };

	void BuildInputLayout();
	void BuildRootSignature();
	void BuildShaders();
	void BuildPSO();
	void BuildRenderItem();
};
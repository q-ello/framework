#pragma once
#include "wrl.h"
#include "d3d12.h"
#include "../../../Common/d3dUtil.h"
#include "../Helpers/Material.h"
#include "../Helpers/RenderItem.h"
#include "../Helpers/FrameResource.h"
#include "GeometryManager.h"

enum class CubeMap : uint8_t
{
	Skybox = 0,
	Irradiance,
	Prefiltered,
	Brdf,
	Count
};

class CubeMapManager
{
public:
	explicit CubeMapManager(ID3D12Device* device);
	~CubeMapManager() = default;
	CubeMapManager(const CubeMapManager&) = delete;
	CubeMapManager(const CubeMapManager&&) = delete;
	CubeMapManager& operator=(const CubeMapManager&) = delete;
	CubeMapManager& operator=(CubeMapManager&&) = delete;
	void Init();

	void AddObjectToResource(const FrameResource* currFrameResource) const;
	void Draw(ID3D12GraphicsCommandList4* cmdList, const FrameResource* currFrameResource) const;

	std::string EnvironmentName(CubeMap type) const
	{
		return _maps[static_cast<int>(type)].Name;
	}

	void AddMap(CubeMap type, const TextureHandle& handle);

	D3D12_GPU_DESCRIPTOR_HANDLE GetCubeMapGpuHandle() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetIblMapsGpuHandle() const;

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
	void BuildPso();
	void BuildRenderItem();
};
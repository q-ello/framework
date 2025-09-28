#pragma once
#include "wrl.h"
#include "d3d12.h"
#include "../../Common/d3dUtil.h"
#include "Material.h"
#include "RenderItem.h"
#include "FrameResource.h"
#include "GeometryManager.h"

class CubeMapManager
{
public:
	CubeMapManager(ID3D12Device* device);
	~CubeMapManager();
	void Init();

	void AddObjectToResource(FrameResource* currFrameResource);
	void Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);

	size_t EnvironmentsCount() const
	{
		return _environments.size();
	}

	int SelectedEnvironment() const
	{
		return _selected;
	}

	std::string EnvironmentName(int i) const
	{
		return _environments[i].name;
	}

	void SetSelected(int i)
	{
		_selected = i;
	}

	void AddEnvironment(TextureHandle handle);
	void DeleteEnvironment(int i);

private:
	ID3D12Device* _device = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _pso;
	Microsoft::WRL::ComPtr<ID3DBlob> _vsShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _psShader;
	std::vector<D3D12_INPUT_ELEMENT_DESC> _inputLayout;
	std::unique_ptr<EditableRenderItem> _skyRItem;
	UINT _cbSize = d3dUtil::CalcConstantBufferByteSize(sizeof(StaticObjectConstants));

	std::vector<TextureHandle> _environments;
	int _selected = -1;

	D3D12_VIEWPORT _viewport{ 0, 0, 0, 0, 0, 1 };
	D3D12_RECT _scissorsRect{ 0, 0, 0, 0 };

	void BuildInputLayout();
	void BuildRootSignature();
	void BuildShaders();
	void BuildPSO();
	void BuildRenderItem();
};
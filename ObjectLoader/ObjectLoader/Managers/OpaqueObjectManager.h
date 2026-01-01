#pragma once
#define NOMINMAX
#include "ObjectManager.h"
#include "../Helpers/Camera.h"

class EditableObjectManager : public ObjectManager
{
	using ObjectManager::ObjectManager;
public:
	void UpdateObjectCBs(FrameResource* currFrameResource) override;
	void SetCamera(Camera* camera);
	void AddObjectToResource(Microsoft::WRL::ComPtr<ID3D12Device5> device, FrameResource* currFrameResource) override;
	int AddRenderItem(ID3D12Device5* device, ModelData&& modelData) override;
	bool DeleteObject(int selectedObject) override;
	int AddLod(ID3D12Device5* device, LODData lod, EditableRenderItem* ri) const;
	static void DeleteLod(EditableRenderItem* ri, int index);

	int VisibleObjectsCount() override { return static_cast<int>(_visibleTesselatedObjects.size() + _visibleUntesselatedObjects.size()); };
	int ObjectsCount() override;

	std::string ObjectName(int i) override;
	EditableRenderItem* Object(int i) override;
	void Draw(ID3D12GraphicsCommandList4* cmdList, FrameResource* currFrameResource, float screenHeight, bool isWireframe = false, bool fixedLod = false) const;
	auto DrawObjects(ID3D12GraphicsCommandList4* cmdList, FrameResource* currFrameResource,
	                 const std::vector<uint32_t>& indices, std::unordered_map<uint32_t, EditableRenderItem*> objects,
	                 float screenHeight,
	                 bool fixedLod) const -> void;
	void DrawAabBs(ID3D12GraphicsCommandList4* cmdList, FrameResource* currFrameResource) const;

	std::vector< D3D12_INPUT_ELEMENT_DESC > InputLayout() const
	{
		return _inputLayout;
	}

	std::vector<std::shared_ptr<EditableRenderItem>> Objects()
	{
		return _objects;
	}

private:
	std::vector<std::shared_ptr<EditableRenderItem>> _objects;

	void BuildInputLayout() override;
	void BuildRootSignature() override;
	void BuildPso() override;
	void BuildShaders() override;

	void CountLodOffsets(LodData* lod) const;
	void CountLodIndex(EditableRenderItem* ri, float screenHeight) const;
	float ComputeScreenSize(XMVECTOR& center, float radius, float screenHeight) const;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> _wireframePso;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _tesselatedPso;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _wireframeTesselatedPso;

	Microsoft::WRL::ComPtr<ID3DBlob> _tessVsShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _tessHsShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _tessDsShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _wireframePsShader;

	std::unordered_map<uint32_t, EditableRenderItem*> _tesselatedObjects{};
	std::unordered_map<uint32_t, EditableRenderItem*> _untesselatedObjects{};

	std::vector<uint32_t> _visibleTesselatedObjects{};
	std::vector<uint32_t> _visibleUntesselatedObjects{};

	UINT _cbMeshElementSize = d3dUtil::CalcConstantBufferByteSize(sizeof(OpaqueObjectConstants));
	UINT _cbMaterialElementSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	Camera* _camera;
};
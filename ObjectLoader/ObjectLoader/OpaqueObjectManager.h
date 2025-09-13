#pragma once
#include "ObjectManager.h"
#include "Camera.h"

class EditableObjectManager : public ObjectManager
{
	using ObjectManager::ObjectManager;
public:
	void UpdateObjectCBs(FrameResource* currFrameResource, Camera* camera) override;
	void AddObjectToResource(Microsoft::WRL::ComPtr<ID3D12Device> device, FrameResource* currFrameResource) override;
	int addRenderItem(ID3D12Device* device, ModelData&& modelData) override;
	int addLOD(ID3D12Device* device, LODData lod, EditableRenderItem* ri);
	bool deleteObject(int selectedObject) override;

	int visibleObjectsCount() override { return int(_visibleTesselatedObjects.size() + _visibleUntesselatedObjects.size()); };
	int objectsCount() override;

	std::string objectName(int i) override;
	EditableRenderItem* object(int i);
	void Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, bool isWireframe = false) override;
	void DrawObjects(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, std::vector<uint32_t> indices, std::unordered_map<uint32_t, EditableRenderItem*> objects);
	void DrawAABBs(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);

private:
	std::vector<std::unique_ptr<EditableRenderItem>> _objects;

	void BuildInputLayout() override;
	void BuildRootSignature() override;
	void BuildPSO() override;
	void BuildShaders() override;

	void CountLODOffsets(LODData* lod, int i);

	Microsoft::WRL::ComPtr<ID3D12PipelineState> _wireframePSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _tesselatedPSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _wireframeTesselatedPSO;

	Microsoft::WRL::ComPtr<ID3DBlob> _tessVSShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _tessHSShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _tessDSShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _wireframePSShader;

	std::unordered_map<uint32_t, EditableRenderItem*> _tesselatedObjects{};
	std::unordered_map<uint32_t, EditableRenderItem*> _untesselatedObjects{};

	std::vector<uint32_t> _visibleTesselatedObjects{};
	std::vector<uint32_t> _visibleUntesselatedObjects{};

	UINT _cbMeshElementSize = d3dUtil::CalcConstantBufferByteSize(sizeof(OpaqueObjectConstants));
	UINT _cbMaterialElementSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));
};
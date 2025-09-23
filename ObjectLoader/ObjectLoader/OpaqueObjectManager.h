#pragma once
#define NOMINMAX
#include "ObjectManager.h"
#include "Camera.h"

struct OctreeNode {
	BoundingBox aabb;
	std::vector<int> objects;
	OctreeNode* children[8] = { nullptr }; 
};

struct InstanceData
{
	DirectX::XMFLOAT4X4 World;
	BoundingBox AABB;
};

class EditableObjectManager : public ObjectManager
{
	using ObjectManager::ObjectManager;
public:
	void UpdateObjectCBs(FrameResource* currFrameResource) override;
	void SetCamera(Camera* camera);
	void AddObjectToResource(Microsoft::WRL::ComPtr<ID3D12Device> device, FrameResource* currFrameResource) override;
	
	int AddRenderItem(ID3D12Device* device, ModelData&& modelData) override;
	bool DeleteObject(int selectedObject) override;
	
	int AddLOD(ID3D12Device* device, LODData lod, EditableRenderItem* ri);
	void DeleteLOD(EditableRenderItem* ri, int index);

	void GenerateInstanceTransformsArray(int amount);

	int VisibleObjectsCount() override { return int(_visibleTesselatedObjects.size() + _visibleUntesselatedObjects.size()); };
	int ObjectsCount() override;

	std::string objectName(int i) override;
	EditableRenderItem* object(int i);
	void Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, float screenHeight, bool isWireframe = false, bool fixedLOD = false);
	void DrawObjects(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, std::vector<uint32_t> indices, std::unordered_map<uint32_t, EditableRenderItem*> objects, float screenHeight,
		bool fixedLOD);
	void DrawAABBs(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);

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
	void BuildPSO() override;
	void BuildShaders() override;

	void CountLODOffsets(LODData* lod);
	void CountLODIndex(EditableRenderItem* ri, float screenHeight);
	float ComputeScreenSize(XMVECTOR& center, float radius, float screenHeight);

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

	Camera* _camera;

	OctreeNode* _rootOctTree;
	std::vector<InstanceData> _instances;
};
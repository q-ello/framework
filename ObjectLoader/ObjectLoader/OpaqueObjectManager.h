#pragma once
#include "ObjectManager.h"

class OpaqueObjectManager : public ObjectManager
{
	using ObjectManager::ObjectManager;
public:
	void UpdateObjectCBs(FrameResource* currFrameResource) override;

private:
	std::vector<std::unique_ptr<EditableRenderItem>> _objects;

	void BuildInputLayout() override;
	void BuildRootSignature() override;
	void BuildPSO() override;
	void BuildShaders() override;

	void AddObjectToResource(Microsoft::WRL::ComPtr<ID3D12Device> device, FrameResource* currFrameResource) override;
	int addRenderItem(ID3D12Device* device, const std::string& itemName, bool isTesselated = false) override;
	bool deleteObject(int selectedObject) override;
	int objectsCount() override;
	std::string objectName(int i) override;
	EditableRenderItem* object(int i);
	void Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, bool isWireframe = false) override;
	void DrawObjects(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, std::unordered_map<uint32_t, EditableRenderItem*> objects);

	Microsoft::WRL::ComPtr<ID3D12PipelineState> _wireframePSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _tesselatedPSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _wireframeTesselatedPSO;

	Microsoft::WRL::ComPtr<ID3DBlob> _tessVSShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _tessHSShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _tessDSShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _wireframePSShader;

	std::unordered_map<uint32_t, EditableRenderItem*> _tesselatedObjects{};
	std::unordered_map<uint32_t, EditableRenderItem*> _untesselatedObjects{};
};
#pragma once
#include "ObjectManager.h"


class UnlitObjectManager : public ObjectManager
{
	using ObjectManager::ObjectManager;

public:
	void UpdateObjectCBs(FrameResource* currFrameResource, Camera* camera = nullptr) override;
	int addRenderItem(ID3D12Device* device, ModelData&& modelData) override;
	void Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, bool isWireframe = false) override;
	void AddObjectToResource(Microsoft::WRL::ComPtr<ID3D12Device> device, FrameResource* currFrameResource) override;

private:
	std::vector<std::unique_ptr<UnlitRenderItem>> _objects;

	void BuildInputLayout() override;
	void BuildRootSignature() override;
	void BuildPSO() override;
	void BuildShaders() override;

	bool deleteObject(int selectedObject) override;
	
	int objectsCount() override;
	int visibleObjectsCount() override { return objectsCount(); };
	std::string objectName(int i) override;
	EditableRenderItem* object(int i) override { return nullptr; };
	
};
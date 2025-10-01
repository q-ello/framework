#pragma once
#include "ObjectManager.h"


class UnlitObjectManager : public ObjectManager
{
	using ObjectManager::ObjectManager;

public:
	void UpdateObjectCBs(FrameResource* currFrameResource) override;
	int AddRenderItem(ModelData&& modelData) override;
	void Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, float screenHeight = 0.f, bool isWireframe = false);
	void AddObjectToResource(Microsoft::WRL::ComPtr<ID3D12Device> device, FrameResource* currFrameResource) override;

private:
	std::vector<std::unique_ptr<UnlitRenderItem>> _objects;
	UINT _cbSize = d3dUtil::CalcConstantBufferByteSize(sizeof(StaticObjectConstants));

	void BuildInputLayout() override;
	void BuildRootSignature() override;
	void BuildPSO() override;
	void BuildShaders() override;

	bool DeleteObject(int selectedObject) override;
	
	int ObjectsCount() override;
	int VisibleObjectsCount() override { return ObjectsCount(); };
	std::string objectName(int i) override;
	EditableRenderItem* object(int i) override { return nullptr; };
	
};
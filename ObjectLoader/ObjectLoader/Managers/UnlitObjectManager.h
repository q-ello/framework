#pragma once
#include "ObjectManager.h"


class UnlitObjectManager : public ObjectManager
{
	using ObjectManager::ObjectManager;

public:
	void UpdateObjectCBs(FrameResource* currFrameResource) override;
	int AddRenderItem(ID3D12Device* device, ModelData&& modelData) override;
	void Draw(ID3D12GraphicsCommandList* cmdList, const FrameResource* currFrameResource, float screenHeight = 0.f, bool isWireframe = false) const;
	void AddObjectToResource(Microsoft::WRL::ComPtr<ID3D12Device> device, FrameResource* currFrameResource) override;

private:
	std::vector<std::unique_ptr<UnlitRenderItem>> _objects;
	UINT _cbSize = d3dUtil::CalcConstantBufferByteSize(sizeof(StaticObjectConstants));

	void BuildInputLayout() override;
	void BuildRootSignature() override;
	void BuildPso() override;
	void BuildShaders() override;

	bool DeleteObject(int selectedObject) override;
	
	int ObjectsCount() override;
	int VisibleObjectsCount() override { return ObjectsCount(); };
	std::string ObjectName(int i) override;
	EditableRenderItem* Object(int i) override { return nullptr; };
	
};
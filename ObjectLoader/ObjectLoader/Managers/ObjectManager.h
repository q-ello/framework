#pragma once
#include "../Helpers/FrameResource.h"
#include "../Helpers/RenderItem.h"
#include "TextureManager.h"
#include "GeometryManager.h"

class ObjectManager
{
public:
	explicit ObjectManager(ID3D12Device5* device);
	virtual ~ObjectManager() = default;

	virtual int AddRenderItem(ID3D12Device5* device, ModelData&& modelData) = 0;
	virtual bool DeleteObject(int selectedObject) = 0;

	virtual void UpdateObjectCBs(FrameResource* currFrameResource) = 0;
	virtual void AddObjectToResource(Microsoft::WRL::ComPtr<ID3D12Device5> device, FrameResource* currFrameResource) = 0;

	virtual int ObjectsCount() = 0;
	virtual int VisibleObjectsCount() = 0;
	virtual EditableRenderItem* Object(int i) = 0;
	virtual std::string ObjectName(int i) = 0;

	void Init();
	virtual bool* DrawDebug()
	{
		return &_drawDebug;
	}


protected:
	std::unordered_map<std::string, int> _objectCounters;
	std::unordered_map<std::string, int> _objectLoaded;

	std::uint32_t _uidCount;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> _pso;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	std::vector<D3D12_INPUT_ELEMENT_DESC> _inputLayout;
	Microsoft::WRL::ComPtr<ID3DBlob> _vsShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _psShader;
	Microsoft::WRL::ComPtr<ID3D12Device5> _device;

	virtual void BuildInputLayout() = 0;
	virtual void BuildRootSignature() = 0;
	virtual void BuildPso()= 0;
	virtual void BuildShaders() = 0;

	bool _drawDebug = false;
};

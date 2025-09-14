#pragma once
#include "UploadManager.h"
#include "FrameResource.h"
#include "RenderItem.h"
#include "DirectXMath.h"
#include "TextureManager.h"
#include "../../Common/GBuffer.h"
#include "GeometryManager.h"
#include "Camera.h"

class ObjectManager
{
public:
	ObjectManager(ID3D12Device* device);
	~ObjectManager();

	virtual int addRenderItem(ID3D12Device* device, ModelData&& modelData) = 0;
	virtual bool deleteObject(int selectedObject) = 0;

	virtual void UpdateObjectCBs(FrameResource* currFrameResource) = 0;
	virtual void AddObjectToResource(Microsoft::WRL::ComPtr<ID3D12Device> device, FrameResource* currFrameResource) = 0;

	virtual int objectsCount() = 0;
	virtual int visibleObjectsCount() = 0;
	virtual EditableRenderItem* object(int i) = 0;
	virtual std::string objectName(int i) = 0;

	virtual void Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource, bool isWireframe = false) = 0;
	void Init();
	virtual bool* drawDebug()
	{
		return &_drawDebug;
	}


protected:
	std::unordered_map<std::string, int> _objectCounters;
	std::unordered_map<std::string, int> _objectLoaded;

	std::uint32_t uidCount;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> _pso;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	std::vector<D3D12_INPUT_ELEMENT_DESC> _inputLayout;
	Microsoft::WRL::ComPtr<ID3DBlob> _vsShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _psShader;
	Microsoft::WRL::ComPtr<ID3D12Device> _device;

	virtual void BuildInputLayout() = 0;
	virtual void BuildRootSignature() = 0;
	virtual void BuildPSO()= 0;
	virtual void BuildShaders() = 0;

	bool _drawDebug = false;
};

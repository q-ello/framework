//***************************************************************************************
// MyApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"
#include "DescriptorHeapAllocator.h"
#include "BasicUtil.h"
#include "RenderItem.h"
#include "UploadManager.h"
#include "ObjectManager.h"
#include "TextureManager.h"
#include "GeometryManager.h"
#include "LightingManager.h"
#include "ModelManager.h"

#define DELETE_ID 333

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

enum class PSO
{
	Opaque = 0,
	//Transparent,
	//AlphaTested,
	Unlit,
	Count
};

class MyApp : public D3DApp
{
public:
	MyApp(HINSTANCE hInstance);
	MyApp(const MyApp& rhs) = delete;
	MyApp& operator=(const MyApp& rhs) = delete;
	~MyApp();

	virtual bool Initialize()override;

private:
	void OnResize() override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y) override;
	virtual void OnMouseWheel(WPARAM btnState) override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMainPassCBs(const GameTimer& gt);

	void BuildRootSignatures();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildPSOs();
	void BuildFrameResources();
	void ClearData();

	//imgui staff
	void DrawInterface();
	void DrawObjectsList(int* btnId);
	void DrawHandSpotlight(int* buttonId);
	void DrawLightData(int* btnId);
	void DrawLocalLightData(int* btnId, int lightIndex);
	void DrawObjectInfo(int* btnId);
	void DrawMultiObjectTransform(int* btnId);
	void DrawMultiObjectTextures(int* btnId);
	bool DrawTextureButton(const std::string& label, TextureType texType, int* btnId);
	void DrawTransformInput(const std::string& label, int btnId, int transformIndex, float speed);
	void DrawCameraSpeed();
	void DrawImportModal();

	void InitManagers();

	//drawing
	void GBufferPass();
	void LightingPass();
	void WireframePass();

	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	ComPtr<ID3D12DescriptorHeap> _imGuiDescriptorHeap = nullptr;
	std::unique_ptr<ModelManager> _modelManager = std::make_unique<ModelManager>();

	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;

	std::unordered_map<PSO, std::unique_ptr<ObjectManager>> _objectManagers;
	std::unique_ptr<LightingManager> _lightingManager = nullptr;

	GBufferPassConstants _GBufferCB;
	LightingPassConstants _lightingCB;

	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float _yaw = XM_PI / 2.f;
	float _pitch = 0.f;
	XMFLOAT3 _eyePos = { 0.0f, 1.0f, -5.0f };
	float _cameraSpeed = 0.01f;
	float _mbDown = false;
	bool _isWireframe = false;

	POINT mLastMousePos;

	bool checkForImGui(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

	PSO _selectedType = PSO::Opaque;
	std::set<int> _selectedMeshes;
};


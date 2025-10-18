//***************************************************************************************
// MyApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "BasicUtil.h"
#include "GeometryManager.h"
#include "LightingManager.h"
#include "ModelManager.h"
#include "Model.h"
#include <shobjidl.h> 
#include <sstream>
#include <commctrl.h>
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_dx12.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "OpaqueObjectManager.h"
#include "UnlitObjectManager.h"
#include "PostProcessManager.h"
#include "CubeMapManager.h"
#include "TerrainManager.h"

struct Toast {
	std::string message;
	float lifetime;     // seconds to live
	float creationTime; // ImGui::GetTime() when created
};

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
	void DrawObjectsList(int& btnId);
	void DrawShadowMasksList(int& btnId);
	void DrawTerrain(int& btnId);

	void DrawHandSpotlight(int& buttonId);
	void DrawLightData(int& btnId);
	void DrawLocalLightData(int& btnId, int lightIndex);

	void DrawObjectInfo(int& btnId);
	void DrawMultiObjectTransform(int& btnId);
	void DrawTransformInput(const std::string& label, int btnId, int transformIndex, float speed);
	void DrawObjectMaterial(int& btnId, int matIndex);
	bool DrawIsTransparentCheckbox();
	bool DrawUseARMTextureCheckbox(Material* material);
	void DrawMaterials(int& btnId);
	void DrawMaterialProperty(Material* material, const std::string& label, size_t index, int& btnId, bool isFloat3, bool hasAdditionalInfo = false, const std::string& additionalInfoLabel = "", size_t additionalInfoIndex = -1);
	void DrawMaterialTexture(Material* material, const std::string& label, size_t index, int& btnId, bool hasAdditionalInfo = false, const std::string& additionalInfoLabel = "", size_t additionalInfoIndex = -1);
	void DrawMaterialARMTexture(Material* material, const std::string& label, size_t index, int& btnId);
	void DrawLODs(int& btnId);
	
	void DrawPostProcesses();

	void AddToast(const std::string& msg, float lifetime = 3.0f);
	void DrawToasts();

	void DrawHeader();
	void DrawCameraSpeed();

	//modals
	void DrawImportModal();

	//loading
	void AddModel();
	void AddMultipleModels();
	void AddLOD();
	void AddShadowMask();

	void InitManagers();

	//drawing
	void GBufferPass();
	void LightingPass();
	void WireframePass();
	//presenting middleware texture to backbuffer
	void FinalPass();

	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	ComPtr<ID3D12DescriptorHeap> _imGuiDescriptorHeap = nullptr;
	std::unique_ptr<ModelManager> _modelManager = std::make_unique<ModelManager>();

	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;

	std::unique_ptr<UnlitObjectManager> _gridManager;
	std::unique_ptr<EditableObjectManager> _objectsManager;
	std::unique_ptr<LightingManager> _lightingManager = nullptr;
	std::unique_ptr<CubeMapManager> _cubeMapManager = nullptr;
	std::unique_ptr<PostProcessManager> _postProcessManager = nullptr;
	std::unique_ptr<TerrainManager> _terrainManager = nullptr;

	GBufferPassConstants _GBufferCB;
	LightingPassConstants _lightingCB;

	float _cameraSpeed = 0.01f;
	float _mbDown = false;
	bool _isWireframe = false;
	bool _fixedLOD = false;

	POINT _lastMousePos;

	bool checkForImGui(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

	std::set<int> _selectedModels;

	Camera _camera;

	std::vector<Toast> _notifications;

	//post process effects
	bool _godRays = false;
	bool _ssr = false;
	bool _chromaticAberration = false;
	bool _vignetting = false;
};


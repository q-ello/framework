//***************************************************************************************
// MyApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************
#pragma once

#include <set>

#include "../../Common/d3dApp.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "../Managers/GeometryManager.h"
#include "../Managers/LightingManager.h"
#include "../Managers/ModelManager.h"
#include <sstream>
#include "imgui/backends/imgui_impl_dx12.h"
#include "../Managers/OpaqueObjectManager.h"
#include "../Managers/UnlitObjectManager.h"
#include "../Managers/PostProcessManager.h"
#include "../Managers/CubeMapManager.h"
#include "../Managers/TerrainManager.h"
#include "../Managers/TAAManager.h"
#include "Managers/AtmosphereManager.h"

struct Toast {
	std::string Message;
	float Lifetime;     // seconds to live
	float CreationTime; // ImGui::GetTime() when created
};

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

enum class Pso : uint8_t
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
	explicit MyApp(HINSTANCE hInstance);
	MyApp(const MyApp& rhs) = delete;
	MyApp& operator=(const MyApp& rhs) = delete;
	MyApp(const MyApp&& rhs) = delete;
	MyApp& operator=(const MyApp&& rhs) = delete;
	~MyApp() override;

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
	void UpdateObjectCBs(const GameTimer& gt) const;
	void UpdateMainPassCBs(const GameTimer& gt);

	void BuildDescriptorHeaps();
	void BuildFrameResources() const;

	//imgui staff
	void DrawInterface();
	void DrawObjectsList(int& btnId);
	void DrawShadowMasksList(int& btnId) const;
	void DrawTerrain(int& btnId) const;
	void DrawAtmosphere(int& btnId);

	void DrawHandSpotlight(int& btnId) const;
	void DrawLightData(int& btnId) const;
	void DrawLocalLightData(int& btnId, int lightIndex) const;

	void DrawObjectInfo(int& btnId);
	void DrawMultiObjectTransform(int& btnId) const;
	void DrawTransformInput(const std::string& label, int btnId, int transformIndex, float speed) const;
	void DrawObjectMaterial(int& btnId, int matIndex) const;
	bool DrawIsTransparentCheckbox() const;
	static bool DrawUseArmTextureCheckbox(Material* material);
	void DrawMaterials(int& btnId) const;
	void DrawMaterialProperty(Material* material, const std::string& label, size_t index, int& btnId, bool isFloat3,
	                          bool hasAdditionalInfo = false, const std::string& additionalInfoLabel = "",
	                          size_t additionalInfoIndex = -1) const;
	void DrawMaterialTexture(Material* material, const std::string& label, size_t index, int& btnId,
	                         bool hasAdditionalInfo = false, const std::string& additionalInfoLabel = "",
	                         size_t additionalInfoIndex = -1) const;
	void DrawMaterialArmTexture(Material* material, const std::string& label, size_t index, int& btnId) const;
	void DrawLoDs(int& btnId);
	
	void DrawPostProcesses();

	void AddToast(const std::string& msg, float lifetime = 3.0f);
	void DrawToasts();

	void DrawHeader();
	void DrawCameraSpeed() const;

	//modals
	void DrawImportModal();

	//loading
	void AddModel();
	void AddMultipleModels();
	void AddLod();
	void AddShadowMask() const;
	void UpdateDirToSun();

	void InitManagers();

	//drawing
	void GBufferPass() const;
	void LightingPass() const;
	void WireframePass() const;
	//presenting middleware texture to backbuffer
	void FinalPass() const;

	FrameResource* _currFrameResource = nullptr;
	int _currFrameResourceIndex = 0;

	ComPtr<ID3D12DescriptorHeap> _imGuiDescriptorHeap = nullptr;
	std::unique_ptr<ModelManager> _modelManager = std::make_unique<ModelManager>();

	std::unordered_map<std::string, ComPtr<ID3DBlob>> _shaders;

	std::unique_ptr<UnlitObjectManager> _gridManager;
	std::unique_ptr<EditableObjectManager> _objectsManager;
	std::unique_ptr<LightingManager> _lightingManager = nullptr;
	std::unique_ptr<CubeMapManager> _cubeMapManager = nullptr;
	std::unique_ptr<PostProcessManager> _postProcessManager = nullptr;
	std::unique_ptr<TerrainManager> _terrainManager = nullptr;
	std::unique_ptr<TaaManager> _taaManager = nullptr;
	std::unique_ptr<AtmosphereManager> _atmosphereManager = nullptr;

	GBufferPassConstants _gBufferCb;
	LightingPassConstants _lightingCb;

	float _cameraSpeed = 0.01f;
	bool _mbDown = false;
	bool _isWireframe = false;
	bool _fixedLod = false;

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

	bool _taaEnabled = false;
	bool _atmosphereEnabled = false;	
	int _timeInHours = 0;
	int _timeInMinutes = 0;
	
};


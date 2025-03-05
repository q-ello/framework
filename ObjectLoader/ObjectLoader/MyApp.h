//***************************************************************************************
// MyApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"

#include "Controls.h"

#define DELETE_ID 333

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;
	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();

	std::vector<std::vector<float>> transform = { {0., 0., 0.}, {0., 0., 0.}, { 1., 1., 1. } };
	bool lockedScale = true;

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

class MyApp : public D3DApp
{
public:
	MyApp(HINSTANCE hInstance);
	MyApp(const MyApp& rhs) = delete;
	MyApp& operator=(const MyApp& rhs) = delete;
	~MyApp();

	virtual bool Initialize()override;

	void processText(HWND hwnd, wchar_t* text) override;

private:
	void OnResize() override;
	void OnResizing() override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void LoadTextures();
	void BuildRootSignature();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void buildGridGeometry();
	void BuildModelGeometry(WCHAR* filename = L"obj\\african_head.obj");
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void CreateControls() override;
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	void buildGrid();

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 8> GetStaticSamplers();

	void moveCamera(int vector, float coeff);

private:
	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;
	UINT mCbvSrvDescriptorSize = 0;
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::wstring, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::wstring, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::wstring, int> _objectCounters;
	std::unordered_map<std::wstring, int> _objectLoaded;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayoutColored;

	ComPtr<ID3D12PipelineState> mOpaquePSO = nullptr;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;

	PassConstants mMainPassCB;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 5.0f };
	XMFLOAT3 _targetPos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = .5f * XM_PI;
	float mPhi = .5f * XM_PI;
	float mRadius = 2.5f;

	POINT mLastMousePos;

	std::shared_ptr<Control> _addNewBtn;
	std::shared_ptr<Control> _lockScaleBtn;
	std::vector<std::shared_ptr<Control>> _objectBtns;

	HWND _transformPanel;
	std::vector<HWND> _transformControls;
	std::vector<std::vector<HWND>> _transformControlsRects;
	std::vector<std::vector<std::shared_ptr<Control>>> _transformControlsCoords;

	HBRUSH _transformBG = CreateSolidBrush(RGB(40, 40, 40));
	HBRUSH _transformPartsBG = CreateSolidBrush(RGB(50, 50, 50));


	void resizeRenderWindow();
	void UnloadModel(const std::wstring& modelName);
	void addRenderItem(const std::wstring& itemName);
	void addObjectControl(const std::wstring& name);
	bool handleControls(WPARAM wParam) override;
	void handleRightClickControls(HWND hwnd, int x, int y) override;
	void drawUI(LPDRAWITEMSTRUCT lpdis) override;
	LRESULT colorOtherData(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;
	void handlePaint() override;
	void showTransform(bool show);
	bool onKeyDown(UINT wParam) override;

	void deleteObject();

	int _newId = 1;
	int _selectedObject = -1;

	bool _isMouseDown = false;
};

LRESULT CALLBACK TransformPanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

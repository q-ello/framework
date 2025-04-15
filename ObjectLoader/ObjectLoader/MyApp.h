//***************************************************************************************
// MyApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"
#include "DescriptorHeapAllocator.h"

#define DELETE_ID 333

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

enum class PSO
{
	Opaque = 0,
	//Transparent,
	//AlphaTested,
	Grid,
	Count
};

//textureStaff
struct TextureHandle
{
	std::string name = "load";
	UINT index = -1;
	bool isRelevant = true;
};

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;
	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();

	std::uint32_t uid = 0;

	std::string Name;
	int nameCount = 0;

	float transform[3][3] = { {0., 0., 0.}, {0., 0., 0.}, {1., 1., 1.} };
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

	TextureHandle diffuseHandle;
	TextureHandle specularHandle;
	TextureHandle normalHandle;

	PSO type = PSO::Opaque;
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
	TextureHandle LoadTexture(WCHAR* filename);
	void BuildRootSignature();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void buildGridGeometry();
	void BuildModelGeometry(WCHAR* filename = L"obj\\african_head.obj");
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	void buildGrid();
	void DrawInterface();
	bool TryToOpenFile(WCHAR* extension1, WCHAR* extension2, PWSTR& filePath);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 8> GetStaticSamplers();

	std::wstring getCroppedName(WCHAR* filename);

private:
	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;
	UINT mCbvSrvDescriptorSize = 0;
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap> _imGuiDescriptorHeap = nullptr;
	std::unique_ptr<DescriptorHeapAllocator> _srvHeapAllocator = nullptr;


	std::unordered_map<std::wstring, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::wstring, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::wstring, UINT> _texIndices;
	std::unordered_map<std::wstring, int> _texUsed;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::wstring, int> _objectCounters;
	std::unordered_map<std::wstring, int> _objectLoaded;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayoutColored;

	// Render items divided by PSO.
	std::unordered_map<PSO, ComPtr<ID3D12PipelineState>> _psos;
	std::unordered_map<PSO, std::vector<RenderItem*>> _renderItems;
	std::vector<std::unique_ptr<RenderItem>> _allRenderItems;
	std::uint32_t uidCount = 0;

	PassConstants mMainPassCB;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 5.0f };
	XMFLOAT3 _targetPos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = .5f * XM_PI;
	float mPhi = .5f * XM_PI;
	float mRadius = 2.5f;

	POINT mLastMousePos;

	void UnloadModel(const std::wstring& modelName);
	void addRenderItem(const std::wstring& itemName);
	bool checkForImGui(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

	void deleteObject();

	int _selectedObject = -1;
	PSO _selectedType = PSO::Opaque;
};


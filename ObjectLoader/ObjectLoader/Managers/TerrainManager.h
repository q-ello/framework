#pragma once
#include "TextureManager.h"
#include "../Helpers/FrameResource.h"
#include "../Helpers/Camera.h"
#include "GeometryManager.h"

enum class TerrainTexture : int8_t
{
	Low = 0,
	Slope,
	High,
	Count
};

struct GridQuadNode
{
	DirectX::BoundingBox Aabb;
	GridInfo Info;
	std::unique_ptr<GridQuadNode> Children[4] = {nullptr};
};

class TerrainManager
{
public:
	explicit TerrainManager(ID3D12Device* device);
	~TerrainManager() = default;

	TerrainManager(const TerrainManager&) = delete;
	TerrainManager& operator=(const TerrainManager&) = delete;
	TerrainManager(TerrainManager&&) = delete;
	TerrainManager& operator=(TerrainManager&&) = delete;

	void Init();
	void BindToOtherData(Camera* camera);
	void Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);

	void InitTerrain();
	void UpdateTerrainCb(const FrameResource* currFrameResource);

	TextureHandle& HeightmapTexture()
	{
		return _heightmapTexture;
	}

	float MaxTerrainHeight = 10.0f;
	float HeightThreshold = 0.5f;
	float SlopeThreshold = 0.5f;

	int GridsCount() const
	{
		return static_cast<int>(_grids.size());
	}

	int VisibleGrids() const
	{
		return _visibleGrids;
	}
	
	MaterialProperty* TerrainTexture(int index);
	
	void SetDirty();

private:
	std::vector<std::unique_ptr<GridQuadNode>> _grids;
	TextureHandle _heightmapTexture;
	int _visibleGrids;
	int _heightmapTextureWidth = 0;
	int _heightmapTextureHeight = 0;
	
	MaterialProperty _textures[static_cast<int>(TerrainTexture::Count)];

	ID3D12Device* _device = nullptr;
	Camera* _camera = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _pso;
	Microsoft::WRL::ComPtr<ID3DBlob> _vsShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _psShader;
	std::vector<D3D12_INPUT_ELEMENT_DESC> _inputLayout;

	DXGI_FORMAT _format = DXGI_FORMAT_R8G8B8A8_UNORM;
	int _gridSize = 33;
	
	int _numFramesDirty = gNumFrameResources;

private:
	void BuildInputLayout();
	void BuildRootSignature();
	void BuildShaders();
	void BuildPso();
	
	std::unique_ptr<GridQuadNode> CreateQuadNode(int xTexelStart, int yTexelStart, float worldOffsetX, float worldOffsetY, int stride);

	void FrustumCulling(FrameResource* currFrameResource);
	void CullQuad(GridQuadNode* grid, BoundingFrustum& localFrustum, FrameResource* currFrameResource);
	void AddQuadToVisible(GridQuadNode* grid, FrameResource* currFrameResource);
};
#pragma once
#include "TextureManager.h"
#include "FrameResource.h"
#include "Camera.h"
#include "GeometryManager.h"
#include "../../Common/GBuffer.h"

struct GridQuadNode
{
	DirectX::BoundingBox AABB;
	GridInfo info;
	std::unique_ptr<GridQuadNode> children[4] = {nullptr};
};

class TerrainManager
{
public:
	TerrainManager(ID3D12Device* device);
	~TerrainManager();

	void Init();
	void BindToOtherData(Camera* camera);
	void Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource);

	void InitTerrain();
	void UpdateTerrainCB(FrameResource* currFrameResource);

	TextureHandle& HeightmapTexture()
	{
		return _heightmapTexture;
	}

	TextureHandle& DiffuseTexture()
	{
		return _diffuseTexture;
	}

	float maxTerrainHeight = 10.0f;

private:
	std::vector<std::unique_ptr<GridQuadNode>> _grids;
	TextureHandle _heightmapTexture;
	TextureHandle _diffuseTexture;
	int _visibleGrids;
	int _heightmapTextureWidth = 0;
	int _heightmapTextureHeight = 0;

	ID3D12Device* _device = nullptr;
	Camera* _camera = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _pso;
	Microsoft::WRL::ComPtr<ID3DBlob> _vsShader;
	Microsoft::WRL::ComPtr<ID3DBlob> _psShader;
	std::vector<D3D12_INPUT_ELEMENT_DESC> _inputLayout;

	DXGI_FORMAT _format = DXGI_FORMAT_R8G8B8A8_UNORM;
	int _gridSize = 33;

private:
	void BuildInputLayout();
	void BuildRootSignature();
	void BuildShaders();
	void BuildPSO();
	
	std::unique_ptr<GridQuadNode> CreateQuadNode(int xTexelStart, int yTexelStart, float worldOffsetX, float worldOffsetY, int stride);

	void FrustumCulling(FrameResource* currFrameResource);
	void CullQuad(GridQuadNode* grid, BoundingFrustum& localFrustum, FrameResource* currFrameResource);
	void AddQuadToVisible(GridQuadNode* grid, FrameResource* currFrameResource);
};
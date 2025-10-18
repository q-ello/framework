#include "TerrainManager.h"

TerrainManager::TerrainManager(ID3D12Device* device)
{
	_device = device;
}

TerrainManager::~TerrainManager()
{
}

void TerrainManager::Init()
{
	BuildInputLayout();
	BuildRootSignature();
	BuildShaders();
	BuildPSO();

	//allocating indices for heightmap and diffuse textures
	const auto& allocator = TextureManager::srvHeapAllocator.get();
	_heightmapTexture.index = allocator->Allocate();
	_diffuseTexture.index = allocator->Allocate();
}

void TerrainManager::BindToOtherData(Camera* camera)
{
	_camera = camera;
}

void TerrainManager::InitTerrain()
{
	_grids.clear();


}

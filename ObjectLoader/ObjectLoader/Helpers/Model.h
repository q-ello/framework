#pragma once
#include <string>
#include <vector>
#include <algorithm>
#define NOMINMAX
#include <Windows.h>
#include <assimp/scene.h>           // Output data structure
#include "RenderItem.h"
#include "../Managers/TextureManager.h"

enum class Transform
{
	Translation,
	Rotation,
	Scale
};

class Model
{
public:
	Model(){};
	Model(std::vector<aiNode*> lods, std::string name, aiMaterial** materials, UINT materialsCount, aiMesh** meshes, aiTexture** textures, std::wstring fileLocation);
	//lod
	Model(aiNode* lod, aiMesh** meshes);
	~Model();

	std::string name = "";
	std::vector<std::unique_ptr<Material>> materials();
	DirectX::BoundingBox AABB() const
	{
		return _aabb;
	}

	std::vector<LOD> lods() const
	{
		return std::move(_lods);
	}

	bool isTesselated()
	{
		return _isTesselated;
	}

	const std::array<DirectX::XMFLOAT3, 3>& transform() const
	{
		return _transform;
	}

private:
	std::vector<LOD> _lods = {};

	std::vector<std::unique_ptr<Material>> _materials = {};

	DirectX::BoundingBox _aabb = BoundingBox::BoundingBox();
	bool _aabbIsInitialized = false;

	std::wstring _fileLocation;

	std::array<DirectX::XMFLOAT3, 3> _transform = {};

	bool _isTesselated = false;

	Mesh ParseMesh(aiMesh* mesh, LOD& lod, DirectX::XMMATRIX parentWorld = DirectX::XMMatrixIdentity());
	void ParseMaterial(aiMaterial* material, aiTexture** textures);
	std::vector<Mesh> ParseNode(aiNode* node, aiMesh** meshes, LOD& lod, DirectX::XMMATRIX parentWorld = DirectX::XMMatrixIdentity());
	LOD ParseLOD(aiNode* node, aiMesh** meshes);

	//helper
	bool LoadMatPropTexture(aiMaterial* material, Material* newMaterial, aiTexture** textures, MatProp property, aiTextureType texType);
	bool LoadMatTexture(aiMaterial* material, Material* newMaterial, aiTexture** textures, MatTex property, aiTextureType texType);
	void CalculateAABB();
	void AlignMeshes();
};
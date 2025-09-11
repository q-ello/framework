#pragma once
#include <string>
#include "VertexData.h"
#include <vector>
#include <cfloat> 
#include <algorithm>
#define NOMINMAX
#include <Windows.h>
#include <fstream>
#include <sstream>
#include <set>
#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/scene.h>           // Output data structure
#include <assimp/postprocess.h>     // Post processing flags
#include <map>
#include "RenderItem.h"
#include "TextureManager.h"
#include <limits>



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
	Model(aiNode* node, std::string name, aiMaterial** materials, UINT materialsCount, aiMesh** meshes, aiTexture** textures, std::wstring fileLocation);
	~Model();

	std::vector<Vertex> vertices() const;
	std::vector<std::int32_t> indices() const;
	std::string name = "";
	std::vector<std::unique_ptr<Material>> materials();
	DirectX::BoundingBox AABB() const
	{
		return _aabb;
	}

	std::vector<Mesh> meshes() const
	{
		return _meshesData;
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
	std::vector<Vertex> _vertices;
	std::vector<std::int32_t> _indices;
	
	std::vector<Mesh> _meshesData;

	std::vector<std::unique_ptr<Material>> _materials = {};

	DirectX::BoundingBox _aabb = BoundingBox::BoundingBox();
	bool _aabbIsInitialized = false;

	std::wstring _fileLocation;

	std::array<DirectX::XMFLOAT3, 3> _transform = {};

	bool _isTesselated = false;

	void ParseMesh(aiMesh* mesh,DirectX::XMMATRIX parentWorld = DirectX::XMMatrixIdentity());
	void ParseMaterial(aiMaterial* material, aiTexture** textures);
	void ParseNode(aiNode* node, aiMesh** meshes, DirectX::XMMATRIX parentWorld = DirectX::XMMatrixIdentity());

	//helper
	bool LoadMatPropTexture(aiMaterial* material, Material* newMaterial, aiTexture** textures, MatProp property, aiTextureType texType);
	bool LoadMatTexture(aiMaterial* material, Material* newMaterial, aiTexture** textures, MatTex property, aiTextureType texType);

	DirectX::XMFLOAT3 _vMin = { FLT_MAX, FLT_MAX, FLT_MAX };
	DirectX::XMFLOAT3 _vMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
};
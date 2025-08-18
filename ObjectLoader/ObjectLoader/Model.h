#pragma once
#include <string>
#include "VertexData.h"
#include <vector>
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

class Model
{
public:
	Model() {}
	Model(aiMesh** meshes, unsigned int numMeshes, std::string sceneName, aiMaterial* material, aiTexture** textures);
	Model(aiMesh* mesh, aiMaterial* material, aiTexture** textures);
	~Model();

	std::vector<Vertex> vertices() const;
	std::vector<std::int32_t> indices() const;
	bool isTesselated = false;
	std::string name = "";
	std::unique_ptr<Material> material();
	DirectX::BoundingBox AABB() const
	{
		return _aabb;
	}
private:
	std::vector<Vertex> _vertices;
	std::vector<std::int32_t> _indices;
	std::vector<DirectX::XMFLOAT2> _texCoords;
	std::vector<DirectX::XMFLOAT3> _normals;
	std::unique_ptr<Material> _material = std::make_unique<Material>();
	DirectX::BoundingBox _aabb;

	void ParseMesh(aiMesh* mesh);
	void ParseMaterial(aiMaterial* material, aiTexture** textures);

	//helper
	bool LoadMatPropTexture(aiMaterial* material, aiTexture** textures, MatProp property, aiTextureType texType);
	bool LoadMatTexture(aiMaterial* material, aiTexture** textures, MatTex property, aiTextureType texType);
};
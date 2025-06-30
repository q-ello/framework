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

class Model
{
public:
	Model() {}
	Model(aiMesh** meshes, unsigned int numMeshes, std::string sceneName);
	Model(aiMesh* mesh);
	~Model();

	std::vector<Vertex> vertices() const;
	std::vector<std::int32_t> indices() const;
	bool isTesselated = false;
	std::string name = "";
private:
	std::vector<Vertex> _vertices;
	std::vector<std::int32_t> _indices;
	std::vector<DirectX::XMFLOAT2> _texCoords;
	std::vector<DirectX::XMFLOAT3> _normals;

	void ParseMesh(aiMesh* mesh);
};
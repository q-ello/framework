#include "Model.h"
#include <fstream>
#include <Windows.h>
#include <sstream>
#include <set>
#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/scene.h>           // Output data structure
#include <assimp/postprocess.h>     // Post processing flags
#include <map>

Model::Model(WCHAR* filename)
{
	// Create an instance of the Importer class
	Assimp::Importer importer;
	std::wstring ws(filename);
	// And have it read the given file with some example postprocessing
	// Usually - if speed is not the most important aspect for you - you'll
	// probably to request more postprocessing than we do in this example.
	const aiScene* scene = importer.ReadFile(std::string(ws.begin(), ws.end()),
		aiProcess_Triangulate |
		aiProcess_JoinIdenticalVertices |
		aiProcess_MakeLeftHanded | 
		aiProcess_FlipWindingOrder |
		aiProcess_FlipUVs |
		aiProcess_CalcTangentSpace
	);

	// If the import failed, report it
	if (nullptr == scene) {
		MessageBox(0, L"Failed to open file", L"", MB_OK);
		return;
	}

	for (int j = 0; j < scene->mNumMeshes; j++)
	{
		//offsetting vertices because, well, parsing is incorrect
		size_t vertexOffset = _vertices.size();

		aiMesh* mesh = scene->mMeshes[j];

		for (int i = 0; i < mesh->mNumVertices; i++)
		{
			Vertex v;
			aiVector3D pos = mesh->mVertices[i];
			aiVector3D tex = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][i] : aiVector3D(0.f, 0.f, 0.f);
			aiVector3D normal = mesh->mNormals[i];
			aiVector3D tangent;
			aiVector3D biNormal;
			if (mesh->HasTangentsAndBitangents())
			{
				tangent = mesh->mTangents[i];
				biNormal = mesh->mBitangents[i];
			}
			else
			{
				tangent = aiVector3D(0.f, 0.f, 0.f);
				biNormal = aiVector3D(0.f, 0.f, 0.f);
			}
			v.Pos = { pos.x, pos.y, pos.z };
			v.TexC = { tex.x, tex.y };
			v.Normal = { normal.x, normal.y, normal.z };
			v.Tangent = { tangent.x, tangent.y, tangent.z };
			v.BiNormal = { biNormal.x, biNormal.y, biNormal.z };
			_vertices.push_back(v);
		}

		for (int i = 0; i < mesh->mNumFaces; i++)
		{
			if (mesh->mFaces[i].mNumIndices != 3)
			{
				continue;
			}
			_indices.push_back(mesh->mFaces[i].mIndices[0] + vertexOffset);
			_indices.push_back(mesh->mFaces[i].mIndices[1] + vertexOffset);
			_indices.push_back(mesh->mFaces[i].mIndices[2] + vertexOffset);
		}
	}
}

Model::~Model()
{
}

std::vector<Vertex> Model::vertices() const
{
	return _vertices;
}

std::vector<std::int32_t> Model::indices() const
{
	return _indices;
}

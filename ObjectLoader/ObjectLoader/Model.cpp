#include "Model.h"

Model::Model(aiMesh** meshes, unsigned int numMeshes, std::string sceneName)
{
	name = sceneName;
	for (unsigned int j = 0; j < numMeshes; j++)
	{
		ParseMesh(meshes[j]);
	}

	isTesselated = _vertices.size() < 10000;
}

Model::Model(aiMesh* mesh)
{
	name = mesh->mName.C_Str();
	ParseMesh(mesh);
	isTesselated = _vertices.size() < 10000;
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

void Model::ParseMesh(aiMesh* mesh)
{
	size_t vertexOffset = _vertices.size();
	for (unsigned int i = 0; i < mesh->mNumVertices; i++)
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

	for (unsigned int i = 0; i < mesh->mNumFaces; i++)
	{
		if (mesh->mFaces[i].mNumIndices != 3)
		{
			continue;
		}
		_indices.push_back(mesh->mFaces[i].mIndices[0] + (UINT)vertexOffset);
		_indices.push_back(mesh->mFaces[i].mIndices[1] + (UINT)vertexOffset);
		_indices.push_back(mesh->mFaces[i].mIndices[2] + (UINT)vertexOffset);
	}
}

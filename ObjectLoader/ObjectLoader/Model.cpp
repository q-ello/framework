#include "Model.h"

Model::Model(aiMesh** meshes, unsigned int numMeshes, std::string sceneName, aiMaterial* material, aiTexture** textures, std::wstring fileLocation)
	: _fileLocation{fileLocation}
{
	name = sceneName;

	for (unsigned int j = 0; j < numMeshes; j++)
	{
		ParseMesh(meshes[j]);

		aiMesh* mesh = meshes[j];

		aiVector3D vMaxAI = mesh->mAABB.mMax;
		aiVector3D vMinAI = mesh->mAABB.mMin;
		DirectX::XMVECTOR vMax = DirectX::XMVectorSet(vMaxAI.x, vMaxAI.y, vMaxAI.z, 1);
		DirectX::XMVECTOR vMin = DirectX::XMVectorSet(vMinAI.x, vMinAI.y, vMinAI.z, 1);
		if (j > 0)
		{
			DirectX::BoundingBox newAabb;
			DirectX::BoundingBox::CreateFromPoints(newAabb, vMax, vMin);
			DirectX::BoundingBox::CreateMerged(_aabb, _aabb, newAabb);
		}
		else
		{
			DirectX::BoundingBox::CreateFromPoints(_aabb, vMax, vMin);
		}
	}
	ParseMaterial(material, textures);
	isTesselated = _vertices.size() < 10000;
}

Model::Model(aiMesh* mesh, aiMaterial* material, aiTexture** textures, std::wstring fileLocation)
	: _fileLocation{fileLocation}
{
	name = mesh->mName.C_Str();
	ParseMesh(mesh);
	ParseMaterial(material, textures);
	isTesselated = _vertices.size() < 10000;
	aiVector3D vMaxAI = mesh->mAABB.mMax;
	aiVector3D vMinAI = mesh->mAABB.mMin;
	DirectX::XMVECTOR vMax = DirectX::XMVectorSet(vMaxAI.x, vMaxAI.y, vMaxAI.z, 1);
	DirectX::XMVECTOR vMin = DirectX::XMVectorSet(vMinAI.x, vMinAI.y, vMinAI.z, 1);
	DirectX::BoundingBox::CreateFromPoints(_aabb, vMax, vMin);
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

std::unique_ptr<Material> Model::material()
{
	return std::move(_material);
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

void Model::ParseMaterial(aiMaterial* material, aiTexture** textures)
{
	if (material == nullptr)
		return;

	_material = std::make_unique<Material>();

	aiColor4D color;

	if (material->Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS)
	{
		_material->properties[BasicUtil::EnumIndex(MatProp::BaseColor)].value = { color.r, color.g, color.b };
	}
	else if (material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
	{
		_material->properties[BasicUtil::EnumIndex(MatProp::BaseColor)].value = { color.r, color.g, color.b };
	}

	if (material->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS)
	{
		_material->properties[BasicUtil::EnumIndex(MatProp::Emissive)].value = { color.r, color.g, color.b };
	}

	float factor;
	if (material->Get(AI_MATKEY_METALLIC_FACTOR, factor) == AI_SUCCESS)
	{
		_material->properties[BasicUtil::EnumIndex(MatProp::Metallic)].value.x = factor;
	}
	else
	{
		_material->properties[BasicUtil::EnumIndex(MatProp::Metallic)].value.x = 0.f;
	}

	if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, factor) == AI_SUCCESS)
	{
		_material->properties[BasicUtil::EnumIndex(MatProp::Roughness)].value.x = factor;
	}

	if (material->Get(AI_MATKEY_OPACITY, factor) == AI_SUCCESS)
	{
		_material->properties[BasicUtil::EnumIndex(MatProp::Opacity)].value.x = factor;
	}

	if (material->Get(AI_MATKEY_EMISSIVE_INTENSITY, factor) == AI_SUCCESS)
	{
		_material->additionalInfo[BasicUtil::EnumIndex(MatAddInfo::Emissive)] = 0.0f;
	}

	if (!LoadMatPropTexture(material, textures, MatProp::BaseColor, aiTextureType_BASE_COLOR))
		LoadMatPropTexture(material, textures, MatProp::BaseColor, aiTextureType_DIFFUSE);
	if (!LoadMatPropTexture(material, textures, MatProp::Emissive, aiTextureType_EMISSION_COLOR))
		LoadMatPropTexture(material, textures, MatProp::Emissive, aiTextureType_EMISSIVE);
	if (!LoadMatPropTexture(material, textures, MatProp::Metallic, aiTextureType_METALNESS))
		LoadMatPropTexture(material, textures, MatProp::Metallic, aiTextureType_SPECULAR);
	LoadMatPropTexture(material, textures, MatProp::Opacity, aiTextureType_OPACITY);
	LoadMatPropTexture(material, textures, MatProp::Roughness, aiTextureType_DIFFUSE_ROUGHNESS);

	if (!LoadMatTexture(material, textures, MatTex::AmbOcc, aiTextureType_AMBIENT_OCCLUSION))
		if (!LoadMatTexture(material, textures, MatTex::AmbOcc, aiTextureType_AMBIENT))
			LoadMatTexture(material, textures, MatTex::AmbOcc, aiTextureType_LIGHTMAP);
	LoadMatTexture(material, textures, MatTex::ARM, aiTextureType_UNKNOWN);
	LoadMatTexture(material, textures, MatTex::Displacement, aiTextureType_DISPLACEMENT);
	LoadMatTexture(material, textures, MatTex::Normal, aiTextureType_NORMALS);
}

bool Model::LoadMatPropTexture(aiMaterial* material, aiTexture** textures, MatProp property, aiTextureType texType)
{
	aiString texPath;
	if (material->GetTexture(texType, 0, &texPath) == AI_SUCCESS)
	{
		std::string textureFilename = texPath.C_Str();

		if (textureFilename[0] == '*')
		{
			int texIndex = atoi(texPath.C_Str() + 1);
			aiTexture* embeddedTex = textures[texIndex];
			std::wstring texName = std::wstring(name.begin(), name.end()) + L"__embedded_" + std::to_wstring(texIndex);
			_material->properties[BasicUtil::EnumIndex(property)].texture = TextureManager::LoadEmbeddedTexture(texName, embeddedTex);
			return true;
		}

		std::wstring textureFileNameW = std::wstring(textureFilename.begin(), textureFilename.end());
		_material->properties[BasicUtil::EnumIndex(property)].texture = TextureManager::LoadTexture((_fileLocation + textureFileNameW).c_str());
		return true;
	}
	return false;
}

bool Model::LoadMatTexture(aiMaterial* material, aiTexture** textures, MatTex property, aiTextureType texType)
{
	aiString texPath;
	if (material->GetTexture(texType, 0, &texPath) == AI_SUCCESS)
	{
		std::string textureFilename = texPath.C_Str();
		OutputDebugStringA(textureFilename.c_str());

		if (textureFilename[0] == '*')
		{
			int texIndex = atoi(texPath.C_Str() + 1);
			aiTexture* embeddedTex = textures[texIndex];
			std::wstring texName = std::wstring(name.begin(), name.end()) + L"__embedded_" + std::to_wstring(texIndex);
			_material->textures[BasicUtil::EnumIndex(property)] = TextureManager::LoadEmbeddedTexture(texName, embeddedTex);
			return true;
		}

		std::wstring textureFileNameW = std::wstring(textureFilename.begin(), textureFilename.end());
		_material->textures[BasicUtil::EnumIndex(property)] = TextureManager::LoadTexture((_fileLocation + textureFileNameW).c_str());
		return true;
	}
	return false;
}

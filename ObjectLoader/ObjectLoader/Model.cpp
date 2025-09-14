#include "Model.h"


DirectX::XMMATRIX aiToMatrix(aiMatrix4x4 m)
{
	return DirectX::XMMATRIX(m.a1, m.a2, m.a3, m.a4,
		m.b1, m.b2, m.b3, m.b4,
		m.c1, m.c2, m.c3, m.c4,
		m.d1, m.d2, m.d3, m.d4);
}

Model::Model(std::vector<aiNode*> lods, std::string modelName, aiMaterial** materials, UINT materialsCount, aiMesh** meshes, aiTexture** textures, std::wstring fileLocation)
{
	name = modelName;
	_fileLocation = fileLocation;

	auto transform = (*lods.begin())->mTransformation;

	aiVector3D translation;
	aiVector3D rotation;
	aiVector3D scale;

	transform.Decompose(scale, rotation, translation);
	_transform[BasicUtil::EnumIndex(Transform::Translation)] = { translation.x, translation.y, translation.z };
	_transform[BasicUtil::EnumIndex(Transform::Rotation)] = { rotation.x, rotation.y, rotation.z };
	_transform[BasicUtil::EnumIndex(Transform::Scale)] = { scale.x, scale.y, scale.z };
	

	for (auto lodIt = lods.begin(); lodIt != lods.end(); lodIt++)
	{
		_lods.push_back(ParseLOD(*lodIt, meshes));
	}

	for (unsigned int i = 0; i < materialsCount; i++)
	{
		aiMaterial* material = materials[i];
		ParseMaterial(material, textures);
	}

	//centering the aabb because lods are placed in different spaces
	CalculateAABB();

	//moving vertices so that they are in the same place when we change them
	AlignMeshes();
}

Model::Model(aiNode* lod, aiMesh** meshes)
{
	_lods.push_back(ParseLOD(lod, meshes));
}

Model::~Model()
{
}

std::vector<std::unique_ptr<Material>> Model::materials()
{
	return std::move(_materials);
}

Mesh Model::ParseMesh(aiMesh* mesh, LOD& lod, DirectX::XMMATRIX parentWorld)
{
	Mesh meshData;
	meshData.vertexStart = lod.vertices.size();
	meshData.indexStart = lod.indices.size();
	meshData.materialIndex = mesh->mMaterialIndex;
	meshData.defaultWorld = parentWorld;

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

		lod.vMin.x = std::min(pos.x, lod.vMin.x);
		lod.vMin.y = std::min(pos.y, lod.vMin.y);
		lod.vMin.z = std::min(pos.z, lod.vMin.z);
		lod.vMax.x = std::max(pos.x, lod.vMax.x);
		lod.vMax.y = std::max(pos.y, lod.vMax.y);
		lod.vMax.z = std::max(pos.z, lod.vMax.z);

		v.TexC = { tex.x, tex.y };
		v.Normal = { normal.x, normal.y, normal.z };
		v.Tangent = { tangent.x, tangent.y, tangent.z };
		v.BiNormal = { biNormal.x, biNormal.y, biNormal.z };
		lod.vertices.push_back(v);
	}

	for (unsigned int i = 0; i < mesh->mNumFaces; i++)
	{
		if (mesh->mFaces[i].mNumIndices != 3)
		{
			continue;
		}
		lod.indices.push_back(mesh->mFaces[i].mIndices[0]);
		lod.indices.push_back(mesh->mFaces[i].mIndices[1]);
		lod.indices.push_back(mesh->mFaces[i].mIndices[2]);
	}

	meshData.indexCount = lod.indices.size() - meshData.indexStart;

	return std::move(meshData);
}

void Model::ParseMaterial(aiMaterial* material, aiTexture** textures)
{
	if (material == nullptr)
		return;

	auto& newMaterial = std::make_unique<Material>();

	newMaterial->name = material->GetName().C_Str();

	aiColor4D color;

	if (material->Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS)
	{
		newMaterial->properties[BasicUtil::EnumIndex(MatProp::BaseColor)].value = { color.r, color.g, color.b };
	}
	else if (material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
	{
		newMaterial->properties[BasicUtil::EnumIndex(MatProp::BaseColor)].value = { color.r, color.g, color.b };
	}

	if (material->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS)
	{
		newMaterial->properties[BasicUtil::EnumIndex(MatProp::Emissive)].value = { color.r, color.g, color.b };
	}

	float factor;
	if (material->Get(AI_MATKEY_METALLIC_FACTOR, factor) == AI_SUCCESS)
	{
		newMaterial->properties[BasicUtil::EnumIndex(MatProp::Metallic)].value.x = factor;
	}
	else
	{
		newMaterial->properties[BasicUtil::EnumIndex(MatProp::Metallic)].value.x = 0.f;
	}

	if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, factor) == AI_SUCCESS)
	{
		newMaterial->properties[BasicUtil::EnumIndex(MatProp::Roughness)].value.x = factor;
	}

	if (material->Get(AI_MATKEY_OPACITY, factor) == AI_SUCCESS)
	{
		newMaterial->properties[BasicUtil::EnumIndex(MatProp::Opacity)].value.x = factor;
	}

	if (material->Get(AI_MATKEY_EMISSIVE_INTENSITY, factor) == AI_SUCCESS)
	{
		newMaterial->additionalInfo[BasicUtil::EnumIndex(MatAddInfo::Emissive)] = 0.0f;
	}

	if (!LoadMatPropTexture(material, newMaterial.get(), textures, MatProp::BaseColor, aiTextureType_BASE_COLOR))
		LoadMatPropTexture(material, newMaterial.get(), textures, MatProp::BaseColor, aiTextureType_DIFFUSE);
	if (!LoadMatPropTexture(material, newMaterial.get(), textures, MatProp::Emissive, aiTextureType_EMISSION_COLOR))
		LoadMatPropTexture(material, newMaterial.get(), textures, MatProp::Emissive, aiTextureType_EMISSIVE);
	if (!LoadMatPropTexture(material, newMaterial.get(), textures, MatProp::Metallic, aiTextureType_METALNESS))
		LoadMatPropTexture(material, newMaterial.get(), textures, MatProp::Metallic, aiTextureType_SPECULAR);
	LoadMatPropTexture(material, newMaterial.get(), textures, MatProp::Opacity, aiTextureType_OPACITY);
	LoadMatPropTexture(material, newMaterial.get(), textures, MatProp::Roughness, aiTextureType_DIFFUSE_ROUGHNESS);

	if (!LoadMatTexture(material, newMaterial.get(), textures, MatTex::AmbOcc, aiTextureType_AMBIENT_OCCLUSION))
		if (!LoadMatTexture(material, newMaterial.get(), textures, MatTex::AmbOcc, aiTextureType_AMBIENT))
			LoadMatTexture(material, newMaterial.get(), textures, MatTex::AmbOcc, aiTextureType_LIGHTMAP);
	LoadMatTexture(material, newMaterial.get(), textures, MatTex::ARM, aiTextureType_UNKNOWN);
	if (!LoadMatTexture(material, newMaterial.get(), textures, MatTex::Displacement, aiTextureType_DISPLACEMENT))
	{
		if (LoadMatTexture(material, newMaterial.get(), textures, MatTex::Displacement, aiTextureType_HEIGHT))
			_isTesselated = true;
	}
	else
	{
		_isTesselated = true;
	}
	LoadMatTexture(material, newMaterial.get(), textures, MatTex::Normal, aiTextureType_NORMALS);

	_materials.push_back(std::move(newMaterial));
}

std::vector<Mesh> Model::ParseNode(aiNode* node, aiMesh** meshes, LOD& lod, DirectX::XMMATRIX parentWorld)
{
	std::vector<Mesh> nodeMeshesData;
	DirectX::XMMATRIX nodeWorld = aiToMatrix(node->mTransformation) * parentWorld;

	for (unsigned int j = 0; j < node->mNumMeshes; j++)
	{
		nodeMeshesData.push_back(ParseMesh(meshes[node->mMeshes[j]], lod, nodeWorld));
	}

	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		auto& childNodeMeshesData = ParseNode(node->mChildren[i], meshes, lod, nodeWorld);
		nodeMeshesData.insert(nodeMeshesData.end(), childNodeMeshesData.begin(), childNodeMeshesData.end());
	}

	return nodeMeshesData;
}

LOD Model::ParseLOD(aiNode* node, aiMesh** meshes)
{
	LOD lod;

	for (unsigned int j = 0; j < node->mNumMeshes; j++)
	{
		lod.meshes.push_back(ParseMesh(meshes[node->mMeshes[j]], lod));
	}

	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		auto& lodChildMeshesData = ParseNode(node->mChildren[i], meshes, lod);
		lod.meshes.insert(lod.meshes.end(), lodChildMeshesData.begin(), lodChildMeshesData.end());
	}

	//make AABB
	{
		DirectX::XMVECTOR vMax = DirectX::XMLoadFloat3(&lod.vMax);
		DirectX::XMVECTOR vMin = DirectX::XMLoadFloat3(&lod.vMin);
		DirectX::BoundingBox::CreateFromPoints(lod.aabb, vMin, vMax);
	}

	return lod;
}

bool Model::LoadMatPropTexture(aiMaterial* material, Material* newMaterial, aiTexture** textures, MatProp property, aiTextureType texType)
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
			newMaterial->properties[BasicUtil::EnumIndex(property)].texture = TextureManager::LoadEmbeddedTexture(texName, embeddedTex);
			return true;
		}

		std::wstring textureFileNameW = std::wstring(textureFilename.begin(), textureFilename.end());
		newMaterial->properties[BasicUtil::EnumIndex(property)].texture = TextureManager::LoadTexture((_fileLocation + textureFileNameW).c_str());
		return true;
	}
	return false;
}

bool Model::LoadMatTexture(aiMaterial* material, Material* newMaterial, aiTexture** textures, MatTex property, aiTextureType texType)
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
			newMaterial->textures[BasicUtil::EnumIndex(property)] = TextureManager::LoadEmbeddedTexture(texName, embeddedTex);
			return true;
		}

		std::wstring textureFileNameW = std::wstring(textureFilename.begin(), textureFilename.end());
		newMaterial->textures[BasicUtil::EnumIndex(property)] = TextureManager::LoadTexture((_fileLocation + textureFileNameW).c_str());
		return true;
	}
	return false;
}

void Model::CalculateAABB()
{
	_aabb = _lods.begin()->aabb;
	DirectX::XMFLOAT3 aabbCenter = { 0, 0, 0 };
	for (auto lodIt = _lods.begin(); lodIt != _lods.end(); lodIt++)
	{
		aabbCenter.x += lodIt->aabb.Center.x;
		aabbCenter.y += lodIt->aabb.Center.y;
		aabbCenter.z += lodIt->aabb.Center.z;
	}
	aabbCenter.x /= _lods.size();
	aabbCenter.y /= _lods.size();
	aabbCenter.z /= _lods.size();

	_aabb.Center = aabbCenter;
}

void Model::AlignMeshes()
{
	for (auto lodIt = _lods.begin(); lodIt != _lods.end(); lodIt++)
	{
		DirectX::XMFLOAT3 offset;

		if (offset.x == _aabb.Center.x && offset.y == _aabb.Center.y && offset.z == _aabb.Center.z)
			continue;
		offset.x = _aabb.Center.x - lodIt->aabb.Center.x;
		offset.y = _aabb.Center.y - lodIt->aabb.Center.y;
		offset.z = _aabb.Center.z - lodIt->aabb.Center.z;

		for (auto& vertex : lodIt->vertices)
		{
			vertex.Pos.x += offset.x;
			vertex.Pos.y += offset.y;
			vertex.Pos.z += offset.z;
		}
	}
}

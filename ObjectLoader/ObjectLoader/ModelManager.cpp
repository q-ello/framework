#include "ModelManager.h"

bool ModelManager::ImportObject(WCHAR* filename)
{
	std::wstring ws(filename);
	_fileLocation = ws.substr(0, ws.find_last_of('\\') + 1);
	std::string s(ws.begin(), ws.end());
	_scene = _importer.ReadFile(s,
		aiProcess_Triangulate |
		aiProcess_JoinIdenticalVertices |
		aiProcess_MakeLeftHanded |
		aiProcess_FlipWindingOrder |
		aiProcess_FlipUVs |
		aiProcess_CalcTangentSpace
	);
	if (nullptr == _scene) {
		MessageBox(0, L"Failed to open file", L"", MB_OK);
		return false;
	}

	const std::vector<aiTextureType> textureTypes = {
		aiTextureType_DIFFUSE,
		aiTextureType_SPECULAR,
		aiTextureType_AMBIENT,
		aiTextureType_EMISSIVE,
		aiTextureType_HEIGHT,
		aiTextureType_NORMALS,
		aiTextureType_SHININESS,
		aiTextureType_OPACITY,
		aiTextureType_DISPLACEMENT,
		aiTextureType_LIGHTMAP,
		aiTextureType_REFLECTION,
		aiTextureType_BASE_COLOR,             // glTF2
		aiTextureType_NORMAL_CAMERA,
		aiTextureType_EMISSION_COLOR,     // glTF2
		aiTextureType_METALNESS,               // glTF2
		aiTextureType_DIFFUSE_ROUGHNESS,      // glTF2
		aiTextureType_AMBIENT_OCCLUSION,              // glTF2
		aiTextureType_UNKNOWN
	};

	_sceneName = _scene->GetShortFilename(s.c_str());
    return _scene->mNumMeshes > 1;
}

std::unique_ptr<Model> ModelManager::ParseAsOneObject()
{
	if (_scene == nullptr)
	{
		OutputDebugString(L"Cannot parse an empty scene");
		return std::make_unique<Model>();
	}

    return std::make_unique<Model>(_scene->mMeshes, _scene->mNumMeshes, _sceneName, 
		_scene->mMaterials[(*_scene->mMeshes)->mMaterialIndex], _scene->mTextures, _fileLocation);
}

std::vector<std::unique_ptr<Model>> ModelManager::ParseScene()
{
	if (_scene == nullptr)
	{
		OutputDebugString(L"Cannot parse an empty scene");
		return std::vector<std::unique_ptr<Model>>();
	}
	std::vector<std::unique_ptr<Model>> models{};
	for (unsigned int i = 0; i < _scene->mNumMeshes; i++)
	{
		auto& mesh = _scene->mMeshes[i];
		models.push_back(std::make_unique<Model>(mesh, _scene->mMaterials[mesh->mMaterialIndex], _scene->mTextures, _fileLocation));
	}

    return models;
}

std::vector<std::string> ModelManager::MeshNames()
{
	std::vector<std::string> meshNames = {};
	for (UINT i = 0; i < _scene->mNumMeshes; i++)
	{
		meshNames.push_back(_scene->mMeshes[i]->mName.C_Str());
	}

	return meshNames;
}

UINT ModelManager::MeshCount()
{
	return _scene->mNumMeshes;
}

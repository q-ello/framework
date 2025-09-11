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
		aiProcess_CalcTangentSpace |
		aiProcess_GenUVCoords
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
    return _scene->mRootNode->mNumMeshes == 0 && _scene->mRootNode->mNumChildren > 1;
}

std::unique_ptr<Model> ModelManager::ParseAsOneObject()
{
	if (_scene == nullptr)
	{
		OutputDebugString(L"Cannot parse an empty scene");
		return std::make_unique<Model>();
	}
	//sometimes there is a stupid additional depth that breaks my aabb, I don't like that
	if (_scene->mRootNode->mNumMeshes == 0 && _scene->mRootNode->mNumChildren == 1)
	{
		return std::make_unique<Model>(_scene->mRootNode->mChildren[0], _sceneName, _scene->mMaterials, _scene->mNumMaterials,
			_scene->mMeshes, _scene->mTextures, _fileLocation);
	}
    return std::make_unique<Model>(_scene->mRootNode, _sceneName, _scene->mMaterials, _scene->mNumMaterials,
		_scene->mMeshes, _scene->mTextures, _fileLocation);
}

std::vector<std::unique_ptr<Model>> ModelManager::ParseScene()
{
	if (_scene == nullptr)
	{
		OutputDebugString(L"Cannot parse an empty scene");
		return std::vector<std::unique_ptr<Model>>();
	}
	std::vector<std::unique_ptr<Model>> models{};
	
	aiNode* rootNode = _scene->mRootNode;
	for (unsigned int i = 0; i < rootNode->mNumChildren; i++)
	{
		aiNode* childNode = rootNode->mChildren[i];
		models.push_back(std::make_unique<Model>(childNode, childNode->mName.C_Str(), _scene->mMaterials, _scene->mNumMaterials,
			_scene->mMeshes, _scene->mTextures, _fileLocation));
	}

    return models;
}

std::map<std::string, std::vector<std::string>> ModelManager::MeshNames()
{
	std::map<std::string, std::vector<std::string>> meshNames = {};
	for (UINT i = 0; i < _scene->mRootNode->mNumChildren; i++)
	{
		std::vector<std::string> nodeMeshNames = {};
		aiNode* node = _scene->mRootNode->mChildren[i];

		meshNames[node->mName.C_Str()] = NodeMeshNames(node);
	}

	return meshNames;
}

std::vector<std::string> ModelManager::NodeMeshNames(aiNode* node)
{
	std::vector<std::string> nodeMeshNames = {};
	for (UINT j = 0; j < node->mNumMeshes; j++)
	{
		nodeMeshNames.push_back(_scene->mMeshes[node->mMeshes[j]]->mName.C_Str());
	}
	for (UINT j = 0; j < node->mNumChildren; j++)
	{
		aiNode* childNode = node->mChildren[j];
		auto& childMeshNames = NodeMeshNames(childNode);
		nodeMeshNames.insert(nodeMeshNames.end(), childMeshNames.begin(), childMeshNames.end());
	}
	return nodeMeshNames;
}

UINT ModelManager::ModelCount()
{
	return _scene->mRootNode->mNumChildren;
}

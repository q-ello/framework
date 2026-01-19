#include "ModelManager.h"
#include <regex>
#include <string>
#include <assimp/postprocess.h>

struct LodNameInfo {
	std::string BaseName;
	int LodIndex = 0;
};

namespace
{
	static LodNameInfo ParseLodName(const std::string& nodeName)
	{
		// Matches:
		//   name_LOD0
		//   name_LOD.000
		//   name_LOD.000_MESH.005
		//   name_LOD1
		//   nameLOD1
		static std::regex lodRegex(R"(^(.*?)(?:_?LOD[._]?(\d+))(?:_.*)?$)",
			std::regex::icase);

		std::smatch match;

		LodNameInfo info;

		if (std::regex_match(nodeName, match, lodRegex))
		{
			info.BaseName = match[1].str();
			info.LodIndex = std::stoi(match[2].str());
			return info;
		}
		info.BaseName = nodeName;
		info.LodIndex = 0;
		return info;
	}
}



int ModelManager::ImportObject(const WCHAR* filename)
{
	const std::wstring ws(filename);
	_fileLocation = ws.substr(0, ws.find_last_of('\\') + 1);
	const std::string s = BasicUtil::WStringToUtf8(ws);
	_scene = _importer.ReadFile(s,
		aiProcess_Triangulate |
		aiProcess_JoinIdenticalVertices |
		aiProcess_MakeLeftHanded |
		aiProcess_FlipWindingOrder |
		aiProcess_FlipUVs |
		aiProcess_CalcTangentSpace |
		aiProcess_GenUVCoords |
		aiProcess_GenNormals
	);
	if (nullptr == _scene) {
		MessageBox(nullptr, L"Failed to open file", L"", MB_OK);
		return 0;
	}

	const std::string shortName = aiScene::GetShortFilename(s.c_str());
	_sceneName = shortName.substr(0, shortName.find_last_of('.'));

	if (_scene->mRootNode->mNumMeshes > 0 || _scene->mRootNode->mNumChildren == 1)
	{
		return 1;
	}

	for (unsigned int i = 0; i < _scene->mRootNode->mNumChildren; i++)
	{
		aiNode* node = _scene->mRootNode->mChildren[i];
		if (node->mNumMeshes == 0 && node->mNumChildren == 0)
			continue;

		LodNameInfo lodInfo = ParseLodName(node->mName.C_Str());

		if (_modelNodes.find(lodInfo.BaseName) == _modelNodes.end())
		{
			_modelNodes[lodInfo.BaseName] = {};
		}

		std::vector<aiNode*>* lods = &_modelNodes[lodInfo.BaseName];

		if (lods->size() < lodInfo.LodIndex + 1)
			lods->resize(lodInfo.LodIndex + 1);

		lods->at(static_cast<size_t>(lodInfo.LodIndex)) = node;
	}
	
	//check if there are many models, or just one with many lods
    return static_cast<int>(_modelNodes.size());
}

bool ModelManager::ImportLodObject(const WCHAR* filename, const int meshesCount)
{
	const std::wstring ws(filename);
	const std::string s = BasicUtil::WStringToUtf8(ws);
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
		MessageBox(nullptr, L"Failed to open file", L"", MB_OK);
		return false;
	}

	if (_scene->mRootNode->mNumChildren > 1 || (_scene->mRootNode->mNumChildren > 0 && _scene->mRootNode->mNumMeshes > 0))
	{
		MessageBox(nullptr, L"This file cannot be LOD: it has more than one object.", L"", MB_OK);
		return false;
	}

	if (meshesCount != NodeMeshCount(_scene->mRootNode))
	{
		MessageBox(nullptr, L"This file cannot be LOD: the mesh count doesn't match.", L"", MB_OK);
		return false;
	}

	return (_scene->mRootNode->mNumMeshes > 0 || _scene->mRootNode->mNumChildren == 1);
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
		return std::make_unique<Model>(std::vector<aiNode*>{_scene->mRootNode->mChildren[0]}, _sceneName, _scene->mMaterials, _scene->mNumMaterials,
			_scene->mMeshes, _scene->mTextures, _fileLocation);
	}

	//otherwise if we have one model and many lods, we parse it as one object with lods
	if (_modelNodes.size() == 1)
	{
		const auto& pair = *_modelNodes.begin();
		std::vector<aiNode*> lods = pair.second;
		return std::make_unique<Model>(lods, _sceneName, _scene->mMaterials, _scene->mNumMaterials,
			_scene->mMeshes, _scene->mTextures, _fileLocation);
	}

	//or else if there is a lot of models then who cares
    return std::make_unique<Model>(std::vector<aiNode*>{_scene->mRootNode}, _sceneName, _scene->mMaterials, _scene->mNumMaterials,
		_scene->mMeshes, _scene->mTextures, _fileLocation);
}

Lod ModelManager::ParseAsLodObject() const
{
	if (_scene == nullptr)
	{
		OutputDebugString(L"Cannot parse an empty scene");
		return {};
	}

	std::unique_ptr<Model> lodModel = nullptr;

	if (_scene->mRootNode->mNumMeshes > 0)
	{
		lodModel = std::make_unique<Model>(_scene->mRootNode, _scene->mMeshes);
	}
	else if (_scene->mRootNode->mNumChildren > 0)
	{
		lodModel = std::make_unique<Model>(_scene->mRootNode->mChildren[0], _scene->mMeshes);
	}
	else
	{
		OutputDebugString(L"LOD file contains no meshes or child nodes");
		return {};
	}

	return *lodModel->lods().begin();
}

std::vector<std::unique_ptr<Model>> ModelManager::ParseScene()
{
	if (_scene == nullptr)
	{
		OutputDebugString(L"Cannot parse an empty scene");
		return std::vector<std::unique_ptr<Model>>();
	}
	
	std::vector<std::unique_ptr<Model>> models{};
	
	for (auto& pair : _modelNodes)
	{
		if (pair.second.size() == 1)
		{
			aiNode* node = *pair.second.begin();
			models.push_back(std::make_unique<Model>(std::vector<aiNode*>{node}, pair.first, _scene->mMaterials, _scene->mNumMaterials,
				_scene->mMeshes, _scene->mTextures, _fileLocation));
		}
		else
		{
			models.push_back(std::make_unique<Model>(pair.second, pair.first, _scene->mMaterials, _scene->mNumMaterials,
				_scene->mMeshes, _scene->mTextures, _fileLocation));
		}
	}
    return models;
}

std::map<std::string, std::vector<std::string>> ModelManager::MeshNames()
{
	std::map<std::string, std::vector<std::string>> meshNames = {};
	for (auto& pair : _modelNodes)
	{
		std::vector<std::string> nodeMeshNames = {};
		aiNode* node = *pair.second.begin();

		meshNames[pair.first] = NodeMeshNames(node);
	}

	return meshNames;
}

std::vector<std::string> ModelManager::NodeMeshNames(const aiNode* node)
{
	std::vector<std::string> nodeMeshNames = {};
	for (UINT j = 0; j < node->mNumMeshes; j++)
	{
		nodeMeshNames.push_back(_scene->mMeshes[node->mMeshes[j]]->mName.C_Str());
	}
	for (UINT j = 0; j < node->mNumChildren; j++)
	{
		const aiNode* childNode = node->mChildren[j];
		auto childMeshNames = NodeMeshNames(childNode);
		nodeMeshNames.insert(nodeMeshNames.end(), childMeshNames.begin(), childMeshNames.end());
	}
	return nodeMeshNames;
}

int ModelManager::NodeMeshCount(const aiNode* node)
{
	int meshCount = 0;
	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		meshCount += NodeMeshCount(node->mChildren[i]);
	}

	meshCount += static_cast<int>(node->mNumMeshes);

	return meshCount;
}

UINT ModelManager::ModelCount() const
{
	return static_cast<UINT>(_modelNodes.size());
}

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

	_sceneName = _scene->GetShortFilename(s.c_str());
	if (_scene->mRootNode->mNumMeshes > 0 || _scene->mRootNode->mNumChildren == 1)
	{
		return false;
	}

	//we can have lods, so we need to group them properly
	std::vector<aiNode*> lods;

	for (int i = 0; i < _scene->mRootNode->mNumChildren; i++)
	{
		aiNode* node = _scene->mRootNode->mChildren[i];
		if (std::string(node->mName.C_Str()).find("LOD") != std::string::npos)
		{
			if (node->mName.C_Str()[node->mName.length - 1] == '0')
			{
				std::string nodeName(node->mName.C_Str());
				_modelNodes[nodeName.substr(0, nodeName.find("LOD"))] = { node };
			}
			else
			{
				//just store it for later processing
				lods.push_back(node);
			}
		}
		else
		{
			if (node->mNumMeshes == 0)
			{
				continue;
			}
			_modelNodes[node->mName.C_Str()] = { node };
		}
	}
	//now process the lods and add them to the right model
	for (aiNode* lodNode : lods)
	{
		for (auto& pair : _modelNodes)
		{
			if (std::string(lodNode->mName.C_Str()).find(pair.first) != std::string::npos)
			{
				int index = lodNode->mName.C_Str()[lodNode->mName.length - 1] - '0';
				// Ensure the vector is large enough
				if (pair.second.size() <= index)
				{
					pair.second.resize(index + 1, nullptr);
				}
				pair.second[index] = lodNode;
				break;
			}
		}
	}

	//check if there is many models, or just one with many lods
    return _modelNodes.size() > 1;
}

bool ModelManager::ImportLODObject(WCHAR* filename, int meshesCount)
{
	std::wstring ws(filename);
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

	if (_scene->mRootNode->mNumChildren > 1 || (_scene->mRootNode->mNumChildren > 0 && _scene->mRootNode->mNumMeshes > 0))
	{
		MessageBox(0, L"This file cannot be LOD: it has more than one object.", L"", MB_OK);
		return false;
	}

	if (meshesCount != NodeMeshCount(_scene->mRootNode))
	{
		MessageBox(0, L"This file cannot be LOD: the mesh count doesn't match.", L"", MB_OK);
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
		auto& pair = *_modelNodes.begin();
		std::vector<aiNode*> lods = pair.second;
		return std::make_unique<Model>(lods, _sceneName, _scene->mMaterials, _scene->mNumMaterials,
			_scene->mMeshes, _scene->mTextures, _fileLocation);
	}

	//or else if there is a lot models then who cares
    return std::make_unique<Model>(std::vector<aiNode*>{_scene->mRootNode->mChildren[0]}, _sceneName, _scene->mMaterials, _scene->mNumMaterials,
		_scene->mMeshes, _scene->mTextures, _fileLocation);
}

LOD ModelManager::ParseAsLODObject()
{
	if (_scene == nullptr)
	{
		OutputDebugString(L"Cannot parse an empty scene");
		return LOD();
	}

	std::unique_ptr<Model> lodModel = nullptr;

	if (_scene->mRootNode->mNumMeshes > 0)
	{
		lodModel = std::make_unique<Model>(_scene->mRootNode, _scene->mMeshes);
	}
	else
	{
		lodModel = std::make_unique<Model>(_scene->mRootNode->mChildren[0], _scene->mMeshes);
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

int ModelManager::NodeMeshCount(aiNode* node)
{
	int meshCount = 0;
	for (int i = 0; i < node->mNumChildren; i++)
	{
		meshCount += NodeMeshCount(node->mChildren[i]);
	}

	meshCount += node->mNumMeshes;

	return meshCount;
}

UINT ModelManager::ModelCount()
{
	return _modelNodes.size();
}
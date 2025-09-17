#pragma once
#include <string>
#include <vector>
#include <Windows.h>
#include "Model.h"
#include <fstream>
#include <sstream>
#include <set>
#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/scene.h>           // Output data structure
#include <assimp/postprocess.h>     // Post processing flags
#include <map>
#include "RenderItem.h"

class ModelManager
{
public:
	ModelManager() = default;
	~ModelManager() = default;

	ModelManager(const ModelManager&) = delete;
	ModelManager& operator=(const ModelManager&) = delete;

	ModelManager(ModelManager&&) = default;
	ModelManager& operator=(ModelManager&&) = default;

	//import object and say is there a single model (false) or is there more (true)
	int ImportObject(WCHAR* filename);
	//import object as lod, true if file is acceptable
	bool ImportLODObject(WCHAR* filename, int meshesCount);
	//parse everything as a single mesh even if it is a scene
	std::unique_ptr<Model> ParseAsOneObject();
	//parse model as lod
	LOD ParseAsLODObject();
	//parse scene as different objects
	std::vector<std::unique_ptr<Model>> ParseScene();

	std::map<std::string, std::vector<std::string>> MeshNames();
	
	UINT ModelCount();
private:
	Assimp::Importer _importer;
	const aiScene* _scene = nullptr;
	std::string _sceneName = "";
	std::wstring _fileLocation;
	std::map<std::string, std::vector<aiNode*>> _modelNodes;
	std::vector<std::string> NodeMeshNames(aiNode* node);
	int NodeMeshCount(aiNode* node);
};
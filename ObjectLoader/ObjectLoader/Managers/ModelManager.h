#pragma once
#include <string>
#include <vector>
#include <Windows.h>
#include "../Helpers/Model.h"
#include <sstream>
#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/scene.h>           // Output data structure
#include <map>
#include "../Helpers/RenderItem.h"

class ModelManager
{
public:
	ModelManager() = default;
	~ModelManager() = default;

	ModelManager(const ModelManager&) = delete;
	ModelManager& operator=(const ModelManager&) = delete;

	ModelManager(ModelManager&&) = delete;
	ModelManager& operator=(ModelManager&&) = delete;

	//import object and say is there a single model (false) or is there more (true)
	int ImportObject(const WCHAR* filename);
	//import object as lod, true if file is acceptable
	bool ImportLodObject(const WCHAR* filename, int meshesCount);
	//parse everything as a single mesh even if it is a scene
	std::unique_ptr<Model> ParseAsOneObject();
	//parse model as lod
	LOD ParseAsLodObject() const;
	//parse scene as different objects
	std::vector<std::unique_ptr<Model>> ParseScene();

	std::map<std::string, std::vector<std::string>> MeshNames();
	
	UINT ModelCount() const;
private:
	Assimp::Importer _importer;
	const aiScene* _scene = nullptr;
	std::string _sceneName = "";
	std::wstring _fileLocation;
	std::map<std::string, std::vector<aiNode*>> _modelNodes;
	std::vector<std::string> NodeMeshNames(const aiNode* node);
	static int NodeMeshCount(const aiNode* node);
};
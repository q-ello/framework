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

class ModelManager
{
public:
	ModelManager() {};
	~ModelManager() {};

	//import object and say is there a single mesh (false) or is there more (true)
	bool ImportObject(WCHAR* filename);
	//parse everything as a single mesh even if it is a scene
	std::unique_ptr<Model> ParseAsOneObject();
	//parse scene as different objects
	std::vector<std::unique_ptr<Model>> ParseScene();
private:
	Assimp::Importer _importer;
	const aiScene* _scene = nullptr;
	std::string _sceneName = "";
};
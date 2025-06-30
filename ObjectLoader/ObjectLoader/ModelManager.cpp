#include "ModelManager.h"

bool ModelManager::ImportObject(WCHAR* filename)
{
	std::wstring ws(filename);
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

    return std::make_unique<Model>(_scene->mMeshes, _scene->mNumMeshes, _sceneName);
}

std::vector<std::unique_ptr<Model>> ModelManager::ParseScene()
{
	if (_scene == nullptr)
	{
		OutputDebugString(L"Cannot parse an empty scene");
		return std::vector<std::unique_ptr<Model>>();
	}
	std::vector<std::unique_ptr<Model>> models{};
	for (int i = 0; i < _scene->mNumMeshes; i++)
	{
		models.push_back(std::make_unique<Model>(_scene->mMeshes[i]));
	}

    return models;
}
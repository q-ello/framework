#pragma once
#include "Windows.h"
#include "unordered_map"
#include "../../Common/GeometryGenerator.h"
#include "../../Common/d3dUtil.h"
#include "VertexData.h"
#include "UploadManager.h"
#include "BasicUtil.h"
#include "Model.h"

struct ModelData
{
	std::wstring croppedName = L"";
	bool isTesselated = false;
};

class GeometryManager
{
public:
	static std::unordered_map<std::wstring, std::unique_ptr<MeshGeometry>>& geometries();
	static std::unordered_map<std::wstring, bool>& tesselatable();

	static void BuildNecessaryGeometry();
	static ModelData GeometryManager::BuildModelGeometry(WCHAR* filename = L"obj\\african_head.obj");
	static void UnloadModel(const std::wstring& modelName);
};
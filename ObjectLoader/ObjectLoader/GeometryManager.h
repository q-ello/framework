#pragma once
#include "Windows.h"
#include "unordered_map"
#include "../../Common/GeometryGenerator.h"
#include "../../Common/d3dUtil.h"
#include "VertexData.h"
#include "UploadManager.h"
#include "BasicUtil.h"
#include "Model.h"

class GeometryManager
{
public:
	static std::unordered_map<std::wstring, std::unique_ptr<MeshGeometry>>& geometries();

	static void BuildGridGeometry();
	static std::wstring GeometryManager::BuildModelGeometry(WCHAR* filename = L"obj\\african_head.obj");
	static void UnloadModel(const std::wstring& modelName);
};
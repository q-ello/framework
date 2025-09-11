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
	std::string croppedName = "";
	bool isTesselated = false;
	std::vector<std::unique_ptr<Material>> materials;
	std::vector<Mesh> meshesData;
	BoundingBox AABB;
	std::array<DirectX::XMFLOAT3, 3> transform = {};
};

class GeometryManager
{
public:
	static std::unordered_map<std::string, std::unique_ptr<MeshGeometry>>& geometries();
	static std::unordered_map<std::string, bool>& tesselatable();

	static void BuildNecessaryGeometry();
	static ModelData GeometryManager::BuildModelGeometry(Model* model);
	static void UnloadModel(const std::string& modelName);
};
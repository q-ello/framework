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
	std::vector<LODData> lodsData{};
	BoundingBox AABB;
	std::array<DirectX::XMFLOAT3, 3> transform = {};
};

class GeometryManager
{
public:
	static std::unordered_map<std::string, std::vector<std::shared_ptr<MeshGeometry>>>& geometries();
	static std::unordered_map<std::string, bool>& tesselatable();

	static void BuildNecessaryGeometry();
	static ModelData BuildModelGeometry(Model* model);
	static void UnloadModel(const std::string& modelName);
	static void AddLODGeometry(std::string name, int lodIdx, LOD lod);
	static void DeleteLODGeometry(std::string name, int lodIdx);
};
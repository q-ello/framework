#pragma once
#include "unordered_map"
#include "../../../Common/GeometryGenerator.h"
#include "../../../Common/d3dUtil.h"
#include "../Helpers/Model.h"

struct ModelData
{
	std::string CroppedName = "";
	bool IsTesselated = false;
	std::vector<std::unique_ptr<Material>> Materials;
	std::vector<LodData> LodsData{};
	BoundingBox Aabb;
	std::array<DirectX::XMFLOAT3, 3> Transform = {};
};

class GeometryManager
{
public:
	static std::unordered_map<std::string, std::vector<std::shared_ptr<MeshGeometry>>>& Geometries();
	static std::unordered_map<std::string, bool>& Tesselatable();

	static void BuildNecessaryGeometry();
	static ModelData BuildModelGeometry(Model* model);
	static void UnloadModel(const std::string& modelName);
	static void AddLodGeometry(const std::string& name, int lodIdx, const Lod& lod);
	static void DeleteLodGeometry(const std::string& name, int lodIdx);
	static void BuildBlasForMesh(MeshGeometry& geo, ID3D12GraphicsCommandList4* cmdList);
};
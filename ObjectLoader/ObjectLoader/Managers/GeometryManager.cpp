#include "GeometryManager.h"

#include "UploadManager.h"

std::unordered_map<std::string, std::vector<std::shared_ptr<MeshGeometry>>>& GeometryManager::Geometries()
{
	static std::unordered_map<std::string, std::vector<std::shared_ptr<MeshGeometry>>> geometries;
	return geometries;
}

std::unordered_map<std::string, bool>& GeometryManager::Tesselatable()
{
	static std::unordered_map<std::string, bool> tesselatable;
	return tesselatable;
}

void GeometryManager::BuildNecessaryGeometry()
{
	UploadManager::Reset();

	GeometryGenerator geoGen;
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(1000.f, 1000.f, 10.f);
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 0);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData terrainGrid = geoGen.CreateTerrainGrid(33, 33);

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = static_cast<UINT>(grid.Indices32.size());
	gridSubmesh.StartIndexLocation = 0;
	gridSubmesh.BaseVertexLocation = 0;

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = static_cast<UINT>(box.Indices32.size());
	boxSubmesh.StartIndexLocation = gridSubmesh.IndexCount;
	boxSubmesh.BaseVertexLocation = static_cast<INT>(grid.Vertices.size());

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = static_cast<UINT>(sphere.Indices32.size());
	sphereSubmesh.StartIndexLocation = boxSubmesh.StartIndexLocation + boxSubmesh.IndexCount;
	sphereSubmesh.BaseVertexLocation = boxSubmesh.BaseVertexLocation + static_cast<UINT>(box.Vertices.size());

	SubmeshGeometry terrainGridSubmesh;
	terrainGridSubmesh.IndexCount = static_cast<UINT>(terrainGrid.Indices32.size());
	terrainGridSubmesh.StartIndexLocation = sphereSubmesh.StartIndexLocation + sphereSubmesh.IndexCount;
	terrainGridSubmesh.BaseVertexLocation = sphereSubmesh.BaseVertexLocation + static_cast<UINT>(sphere.Vertices.size());

	const size_t gridVerticesCount = grid.Vertices.size();

	std::vector<LightVertex> vertices(gridVerticesCount + box.Vertices.size() + sphere.Vertices.size() + terrainGrid.Vertices.size());

	for (size_t i = 0; i < gridVerticesCount; ++i)
	{
		vertices[i].Pos = grid.Vertices[i].Position;
	}

	for (size_t i = 0; i < box.Vertices.size(); ++i)
	{
		vertices[boxSubmesh.BaseVertexLocation + i].Pos = box.Vertices[i].Position;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); i++)
	{
		vertices[sphereSubmesh.BaseVertexLocation + i].Pos = sphere.Vertices[i].Position;
	}

	for (size_t i = 0; i < terrainGrid.Vertices.size(); i++)
	{
		vertices[terrainGridSubmesh.BaseVertexLocation + i].Pos = terrainGrid.Vertices[i].Position;
	}

	std::vector<std::uint16_t> indices = grid.GetIndices16();
	indices.insert(indices.end(), box.GetIndices16().begin(), box.GetIndices16().end());
	indices.insert(indices.end(), sphere.GetIndices16().begin(), sphere.GetIndices16().end());
	indices.insert(indices.end(), terrainGrid.GetIndices16().begin(), terrainGrid.GetIndices16().end());

	const UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(LightVertex);
	const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(std::uint16_t);

	auto geo = std::make_shared<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(UploadManager::Device,
		UploadManager::UploadCmdList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(UploadManager::Device,
		UploadManager::UploadCmdList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(LightVertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["terrainGrid"] = terrainGridSubmesh;

	Geometries().emplace(
		geo->Name,
		std::vector<std::shared_ptr<MeshGeometry>>{ geo }
	);
}

ModelData GeometryManager::BuildModelGeometry(Model* model)
{
	//check if geometry already exists
	ModelData data;
	data.CroppedName = model->name;
	data.Materials = std::move(model->materials());

	for (auto& lod : model->lods())
	{
		LodData lodData;
		lodData.Meshes = lod.Meshes;
		lodData.TriangleCount = static_cast<int>(lod.Indices.size()) / 3;
		data.LodsData.push_back(lodData);
	}

	data.Transform = model->transform();
	data.IsTesselated = model->isTesselated();
	data.Aabb = model->AABB();

	if (Geometries().find(model->name) != Geometries().end())
	{
		return data;
	}

	//verifying that model does actually have some data
	if (model->lods().empty())
	{
		OutputDebugString(L"[ERROR] Model data is empty! Aborting geometry creation.\n");
		return {};
	}

	//making different buffers for different lods
	std::vector <std::shared_ptr<MeshGeometry>> lodBuffers{};
	for (auto& lod : model->lods())
	{
		// Pack the indices of all the meshes into one index buffer.
		const UINT vbByteSize = static_cast<UINT>(lod.Vertices.size()) * sizeof(Vertex);

		const UINT ibByteSize = static_cast<UINT>(lod.Indices.size()) * sizeof(std::int32_t);

		auto geo = std::make_shared<MeshGeometry>();

		ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
		CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), lod.Vertices.data(), vbByteSize);

		ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
		CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), lod.Indices.data(), ibByteSize);

		geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(UploadManager::Device,
			UploadManager::UploadCmdList.Get(), lod.Vertices.data(), vbByteSize, geo->VertexBufferUploader);

		geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(UploadManager::Device,
			UploadManager::UploadCmdList.Get(), lod.Indices.data(), ibByteSize, geo->IndexBufferUploader);

		geo->VertexByteStride = sizeof(Vertex);
		geo->VertexBufferByteSize = vbByteSize;
		geo->IndexFormat = DXGI_FORMAT_R32_UINT;
		geo->IndexBufferByteSize = ibByteSize;

		lodBuffers.push_back(std::move(geo));
	}

	Geometries().emplace(
		model->name,
		std::move(lodBuffers)
	);

	Tesselatable()[model->name] = data.IsTesselated;

	return data;
}

void GeometryManager::AddLodGeometry(const std::string& name, const int lodIdx, const Lod& lod)
{
	LodData data;

	data.Meshes = lod.Meshes;
	data.TriangleCount = static_cast<int>(lod.Indices.size());

	if (Geometries().find(name) == Geometries().end())
	{
		return;
	}

	//verifying that model does actually have some data
	if (lod.Indices.empty())
	{
		OutputDebugString(L"[ERROR] LOD data is empty! Aborting geometry creation.\n");
		return;
	}

	//making a buffer for given lod
	const UINT vbByteSize = static_cast<UINT>(lod.Vertices.size()) * sizeof(Vertex);

	
	
	const UINT ibByteSize = static_cast<UINT>(lod.Indices.size()) * sizeof(std::int32_t);

	auto geo = std::make_shared<MeshGeometry>();

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), lod.Vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), lod.Indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(UploadManager::Device,
		UploadManager::UploadCmdList.Get(), lod.Vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(UploadManager::Device,
		UploadManager::UploadCmdList.Get(), lod.Indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	auto& geos = Geometries()[name];

	geos.emplace(geos.begin() + lodIdx, std::move(geo));

	UploadManager::ExecuteUploadCommandList();
}

void GeometryManager::DeleteLodGeometry(const std::string& name, const int lodIdx)
{
	if (Geometries().find(name) == Geometries().end())
	{
		return;
	}
	auto& geos = Geometries()[name];
	if (lodIdx < 0 || lodIdx >= geos.size())
	{
		return;
	}
	geos.erase(geos.begin() + lodIdx);
	UploadManager::ExecuteUploadCommandList();
}

void GeometryManager::UnloadModel(const std::string& modelName)
{
	if (Geometries().find(modelName) != Geometries().end())
	{
		Geometries().erase(modelName);
	}
}



#include "GeometryManager.h"

std::unordered_map<std::string, std::unique_ptr<MeshGeometry>>& GeometryManager::geometries()
{
	static std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> geometries;
	return geometries;
}

std::unordered_map<std::string, bool>& GeometryManager::tesselatable()
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

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = 0;
	gridSubmesh.BaseVertexLocation = 0;

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = gridSubmesh.IndexCount;
	boxSubmesh.BaseVertexLocation = (INT)grid.Vertices.size();

	const size_t gridVerticesCount = grid.Vertices.size();

	std::vector<LightVertex> vertices(gridVerticesCount + box.Vertices.size());

	for (size_t i = 0; i < gridVerticesCount; ++i)
	{
		vertices[i].Pos = grid.Vertices[i].Position;
		vertices[i].Color = grid.Vertices[i].Color;
	}

	for (size_t i = 0; i < box.Vertices.size(); ++i)
	{
		vertices[gridVerticesCount + i].Pos = box.Vertices[i].Position;
	}

	std::vector<std::uint16_t> indices = grid.GetIndices16();
	indices.insert(indices.end(), box.GetIndices16().begin(), box.GetIndices16().end());

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(LightVertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(UploadManager::device,
		UploadManager::uploadCmdList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(UploadManager::device,
		UploadManager::uploadCmdList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(LightVertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["box"] = boxSubmesh;

	geometries()[geo->Name] = std::move(geo);
}

ModelData GeometryManager::BuildModelGeometry(Model* model)
{
	//check if geometry already exists
	if (geometries().find(model->name) != geometries().end())
	{
		ModelData data;
		data.croppedName = model->name;
		data.isTesselated = tesselatable()[model->name];
		data.material = std::move(model->material());
		return data;
	}

	//veryfying that model does actually have some data
	if (model->vertices().empty() || model->indices().empty())
	{
		OutputDebugString(L"[ERROR] Model data is empty! Aborting geometry creation.\n");
		return ModelData();
	}

	// Pack the indices of all the meshes into one index buffer.
	//

	const UINT vbByteSize = (UINT)model->vertices().size() * sizeof(Vertex);

	const UINT ibByteSize = (UINT)model->indices().size() * sizeof(std::int32_t);

	auto geo = std::make_unique<MeshGeometry>();

	geo->Name = model->name;

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), model->vertices().data(), vbByteSize);


	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), model->indices().data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(UploadManager::device,
		UploadManager::uploadCmdList.Get(), model->vertices().data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(UploadManager::device,
		UploadManager::uploadCmdList.Get(), model->indices().data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)model->indices().size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs[model->name] = submesh;

	geometries()[geo->Name] = std::move(geo);

	ModelData data;
	data.croppedName = model->name;
	data.isTesselated = model->isTesselated;
	data.material = model->material();

	tesselatable()[model->name] = data.isTesselated;

	return data;
}

void GeometryManager::UnloadModel(const std::string& modelName)
{
	if (geometries().find(modelName) != geometries().end())
	{
		geometries().erase(modelName);
	}
}



#include "GeometryManager.h"

std::unordered_map<std::wstring, std::unique_ptr<MeshGeometry>>& GeometryManager::geometries()
{
	static std::unordered_map<std::wstring, std::unique_ptr<MeshGeometry>> geometries;
	return geometries;
}

void GeometryManager::BuildNecessaryGeometry()
{
	UploadManager::Reset();
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.f, 20.f, 0.1f);
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 0);

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = 0;
	gridSubmesh.BaseVertexLocation = 0;

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = gridSubmesh.IndexCount;
	boxSubmesh.BaseVertexLocation = grid.Vertices.size();

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
	geo->Name = L"shapeGeo";

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

	geo->DrawArgs[L"grid"] = gridSubmesh;
	geo->DrawArgs[L"box"] = boxSubmesh;

	geometries()[geo->Name] = std::move(geo);
}

std::wstring GeometryManager::BuildModelGeometry(WCHAR* filename)
{
	std::wstring croppedName = BasicUtil::getCroppedName(filename);

	//check if geometry already exists
	if (geometries().find(croppedName) != geometries().end())
	{
		return croppedName;
	}

	//load new model
	std::unique_ptr<Model> model = std::make_unique<Model>(filename);

	//veryfying that model does actually have some data
	if (model->vertices().empty() || model->indices().empty())
	{
		OutputDebugString(L"[ERROR] Model data is empty! Aborting geometry creation.\n");
		return L"";
	}

	// Pack the indices of all the meshes into one index buffer.
	//

	const UINT vbByteSize = (UINT)model->vertices().size() * sizeof(Vertex);

	const UINT ibByteSize = (UINT)model->indices().size() * sizeof(std::int32_t);

	auto geo = std::make_unique<MeshGeometry>();

	geo->Name = croppedName;

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

	geo->DrawArgs[croppedName] = submesh;

	geometries()[geo->Name] = std::move(geo);

	return croppedName;
}

void GeometryManager::UnloadModel(const std::wstring& modelName)
{
	if (geometries().find(modelName) != geometries().end())
	{
		geometries().erase(modelName);
	}
}



#pragma once
#include <string>
#include "BasicUtil.h"
#include "Material.h"
#include "VertexData.h"

using namespace DirectX;

struct RenderItem
{
	RenderItem() = default;

	std::uint32_t uid = 0;

	std::string Name;
	int nameCount = 0;

	int NumFramesDirty = gNumFrameResources;

	std::vector<std::shared_ptr<MeshGeometry>>* Geo{ nullptr };

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};

struct Mesh
{
	DirectX::XMMATRIX defaultWorld;
	size_t vertexStart;
	size_t indexStart;
	size_t indexCount;
	size_t materialIndex;
	int cbOffset;
	int matOffset;
};

struct LOD
{
	std::vector<Vertex> vertices{};
	std::vector<std::int32_t> indices{};
	std::vector<Mesh> meshes{};
	DirectX::XMFLOAT3 vMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
	DirectX::XMFLOAT3 vMin = { FLT_MAX, FLT_MAX, FLT_MAX };
	BoundingBox aabb;
};

struct LODData
{
	int triangleCount = 0;
	std::vector<Mesh> meshes{};
};

struct EditableRenderItem : public RenderItem
{
	std::array<DirectX::XMFLOAT3, 3> transform = {};
	bool lockedScale = true;
	std::vector<std::unique_ptr<Material>> materials;
	bool isTransparent = false;
	BoundingBox Bounds;
	std::vector<LODData> lodsData;
	int currentLODIdx = 0;
	bool isTesselated = false;
	DirectX::XMMATRIX world = DirectX::XMMatrixIdentity();
};

struct UnlitRenderItem : public RenderItem
{
	XMFLOAT4 Color = {.0f, .0f, .0f, 1.f};
};
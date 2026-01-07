#pragma once
#include <string>
#include "BasicUtil.h"
#include "Material.h"
#include "VertexData.h"

using namespace DirectX;

struct RenderItem
{
	RenderItem() = default;

	std::uint32_t Uid = 0;

	std::string Name;
	int NameCount = 0;

	int NumFramesDirty = gNumFrameResources;

	std::vector<std::shared_ptr<MeshGeometry>>* Geo{ nullptr };

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};

struct Mesh
{
	DirectX::XMMATRIX DefaultWorld;
	size_t VertexStart;
	size_t IndexStart;
	size_t IndexCount;
	size_t MaterialIndex;
	int CbOffset;
	int MatOffset;
};

struct Lod
{
	std::vector<Vertex> Vertices{};
	std::vector<std::int32_t> Indices{};
	std::vector<Mesh> Meshes{};
	DirectX::XMFLOAT3 VMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
	DirectX::XMFLOAT3 VMin = { FLT_MAX, FLT_MAX, FLT_MAX };
	BoundingBox Aabb;
};

struct LodData
{
	int TriangleCount = 0;
	std::vector<Mesh> Meshes{};
};

struct EditableRenderItem : public RenderItem
{
	std::array<DirectX::XMFLOAT3, 3> Transform = {};
	bool LockedScale = true;
	std::vector<std::unique_ptr<Material>> Materials;
	bool IsTransparent = false;
	BoundingBox Bounds;
	std::vector<LodData> LodsData;
	int CurrentLodIdx = 0;
	bool IsTesselated = false;
	DirectX::XMMATRIX World = DirectX::XMMatrixIdentity();
	DirectX::XMMATRIX PrevWorld = DirectX::XMMatrixIdentity();
};

struct UnlitRenderItem : public RenderItem
{
	XMFLOAT4 Color = {.0f, .0f, .0f, 1.f};
};
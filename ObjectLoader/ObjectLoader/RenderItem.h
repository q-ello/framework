#pragma once
#include <string>
#include "BasicUtil.h"
#include "Material.h"

using namespace DirectX;

struct RenderItem
{
	RenderItem() = default;

	std::uint32_t uid = 0;

	std::string Name;
	int nameCount = 0;

	int NumFramesDirty = gNumFrameResources;

	MeshGeometry* Geo = nullptr;

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

struct EditableRenderItem : public RenderItem
{
	std::array<DirectX::XMFLOAT3, 3> transform = {};
	bool lockedScale = true;
	std::vector<std::unique_ptr<Material>> materials;
	bool isTransparent = false;
	BoundingBox Bounds;
	std::vector<std::vector<Mesh>> lodsData;
	std::vector<Mesh>* currentLod = nullptr;
	bool isTesselated = false;
};

struct UnlitRenderItem : public RenderItem
{
	XMFLOAT4 Color = {.0f, .0f, .0f, 1.f};
	UINT IndexCount = 0;
};
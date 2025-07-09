#pragma once
#include <string>
#include "BasicUtil.h"
#include "Material.h"

using namespace DirectX;

struct RenderItem
{
	RenderItem() = default;
	XMFLOAT4X4 World = MathHelper::Identity4x4();

	std::uint32_t uid = 0;

	std::string Name;
	int nameCount = 0;

	int NumFramesDirty = gNumFrameResources;

	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	UINT IndexCount = 0;
};

struct EditableRenderItem : public RenderItem
{
	DirectX::XMFLOAT3 transform[3] = { {0., 0., 0.}, {0., 0., 0.}, {1., 1., 1.} };
	bool lockedScale = true;

	std::unique_ptr<Material> material;

	bool isTransparent = false;
};

struct UnlitRenderItem : public RenderItem
{
	XMFLOAT4 Color = {.0f, .0f, .0f, 1.f};
};
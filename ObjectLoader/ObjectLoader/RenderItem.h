#pragma once
#include <string>
#include "GeometryManager.h"

using namespace DirectX;

enum class TextureType
{
	Diffuse = 0,
	Normal,
	Displacement
};

//textures for opaque items
struct TextureHandle
{
	std::string name = "load";
	UINT index = 0;
	bool isRelevant = false;
};

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
	std::unordered_map<TextureType, TextureHandle> texHandles = {
		{TextureType::Diffuse, TextureHandle()},
		{TextureType::Normal, TextureHandle()},
		{TextureType::Displacement, TextureHandle()}
	};
	float dispScale = 1.0f;
};

struct UnlitRenderItem : public RenderItem
{
	XMFLOAT4 Color = {.0f, .0f, .0f, 1.f};
};
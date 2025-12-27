#pragma once
#include <DirectXMath.h>
#include <string>
#include <wrl.h>
#include "BasicUtil.h"

using namespace DirectX;

struct TextureHandle
{
	std::string Name = "load";
	UINT Index = 0;
	bool UseTexture = false;
};

enum class MatProp : int8_t
{
	BaseColor = 0,
	Emissive,
	Opacity,
	Roughness,
	Metallic,
	Count
};

enum class MatTex : int8_t
{
	Normal = 0,
	AmbOcc,
	Displacement,
	ARM,
	Count
};

enum class MatAddInfo : int8_t
{
	Emissive = 0,
	Displacement,
	Count
};

enum class ARMLayout : int8_t
{
	AO_Rough_Metal,   // R=AO, G=Rough, B=Metal
	Rough_Metal_AO,   // R=Rough, G=Metal, B=AO
};

struct MaterialProperty
{
	TextureHandle texture;
	XMFLOAT3 value = { 1.0f, 1.0f, 1.0f };
};

struct Material
{
	std::string name;
	std::array<MaterialProperty, BasicUtil::EnumIndex(MatProp::Count)> properties;
	std::array<TextureHandle, BasicUtil::EnumIndex(MatTex::Count)> textures;
	std::array<float, BasicUtil::EnumIndex(MatAddInfo::Count)> additionalInfo{};
	int numFramesDirty = 0;
	bool useARMTexture = false;
	ARMLayout armLayout = ARMLayout::AO_Rough_Metal;
	bool isUsed = false;
};
#pragma once
#include <string>
#include "VertexData.h"
#include <vector>
#include <Windows.h>

class Model
{
public:
	Model(WCHAR* filename);
	~Model();

	std::vector<Vertex> vertices() const;
	std::vector<std::int32_t> indices() const;
private:
	std::vector<Vertex> _vertices;
	std::vector<std::int32_t> _indices;
	std::vector<DirectX::XMFLOAT2> _texCoords;
	std::vector<DirectX::XMFLOAT3> _normals;
};
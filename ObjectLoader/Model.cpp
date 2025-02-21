#include "Model.h"
#include <fstream>
#include <Windows.h>
#include <sstream>
#include <set>

Model::Model(WCHAR* filename)
{
	std::ifstream file(filename);

	if (!file.is_open())
	{
		MessageBox(0, L"Failed to open file", L"", MB_OK);
		return;
	}

	Vertex v1;
	v1.Pos = { 0.0f, 0.0f, 0.0f };
	_vertices.push_back(v1);

	DirectX::XMFLOAT2 vt1;
	vt1 = { 0.0f, 0.0f };
	_texCoords.push_back(vt1);

	DirectX::XMFLOAT3 vn1;
	vn1 = { 0.0f, 0.0f, 0.0f };
	_normals.push_back(vn1);

	std::set<int> verticesFilled;

	std::string line;
	while (std::getline(file, line))
	{
		if (line.substr(0, 2) == "v ")
		{
			std::istringstream s(line.substr(2));
			Vertex v;
			s >> v.Pos.x >> v.Pos.y >> v.Pos.z;
			_vertices.push_back(v);
		}
		else if (line.substr(0, 3) == "vt ")
		{
			std::istringstream s(line.substr(4));
			DirectX::XMFLOAT2 vt;
			s >> vt.x >> vt.y;
			_texCoords.push_back(vt);
		}
		else if (line.substr(0, 3) == "vn ")
		{
			std::istringstream s(line.substr(4));
			DirectX::XMFLOAT3 vn;
			s >> vn.x >> vn.y >> vn.z;
			_normals.push_back(vn);
		}
		else if (line.substr(0, 2) == "f ")
		{
			std::istringstream s(line.substr(2));
			unsigned int vt;
			unsigned int vn;
			char c;

			unsigned int v[4];
			for (int i = 0; i < 3; i++)
			{
				s >> v[i] >> c >> vt >> c >> vn;
				_indices.push_back(v[i]);
				if (verticesFilled.find(v[i]) == verticesFilled.end())
				{
					_vertices[v[i]].TexC = _texCoords[vt];
					_vertices[v[i]].Normal = _normals[vn];
					verticesFilled.insert(v[i]);
				}
			}

			if (s.peek() == ' ')
			{
				s >> v[3] >> c >> vt >> c >> vn;
				_indices.push_back(v[0]);
				_indices.push_back(v[2]);
				_indices.push_back(v[3]);
				if (verticesFilled.find(v[3]) == verticesFilled.end())
				{
					_vertices[v[3]].TexC = _texCoords[vt];
					_vertices[v[3]].Normal = _normals[vn];
					verticesFilled.insert(v[3]);
				}
			}
		}
	}

	file.close();
}

Model::~Model()
{
}

std::vector<Vertex> Model::vertices() const
{
	return _vertices;
}

std::vector<std::int32_t> Model::indices() const
{
	return _indices;
}

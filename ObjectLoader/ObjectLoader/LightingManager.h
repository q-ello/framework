#pragma once
#include "../../Common/d3dUtil.h"

/*
* TODOS:
* - build geometry: cube
* - checkbox: turn on/off directional light
* - the direction of directional light
* - add ambience?
* - add point/spot lights
* - change transform? need to think. I guess radius and maybe the angle??
* - change color
* - add lightRenderItem for every light and do everything else, with analogy to object render items? store world matrix?
* - draw it blending with indexed instanced
* - everything else is a work in the shader, we'll get to that
* - two draws: one for small lights and the other one for directional lighting
*/

struct Light
{
    int type;
    DirectX::XMFLOAT3 position;
    float radius;
    DirectX::XMFLOAT3 direction;
    float angle;
    DirectX::XMFLOAT3 color;
    float intensity;
    bool active;
};

struct LightGrid
{
	int offset = 0;
	int lightCount = 0;
};

class LightingManager
{
public:
	LightingManager();
private:

};
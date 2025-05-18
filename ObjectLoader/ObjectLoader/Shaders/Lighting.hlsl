#include "LightingUtil.hlsl"

struct Light
{
    float4x4 world;
    int type;
    float3 position;
    float radius;
    float3 direction;
    float angle;
    float3 color;
    float intensity;
    bool active;
};

Texture2D gDiffuse : register(t0);
Texture2D gNormal : register(t1);
Texture2D gDepth : register(t2);
StructuredBuffer<Light> lights : register(t3);

cbuffer cbLightingPass : register(b0)
{
    float4x4 gInvViewProj;
    float2 gRTSize;
    float3 gEyePosW;
    float gPad1;
};

cbuffer cbDirLight : register(b1)
{
    float3 mainLightDirection;
    int mainLightIsOn;
    float3 mainLightColor;
}

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VertexOut LightingVS(uint id : SV_VertexID)
{
    VertexOut vout;
	
    float2 positions[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
    
    float2 texcoords[3] = { float2(0, 1), float2(0, -1), float2(2, 1) };
    
    vout.PosH = float4(positions[id], 0, 1);
    vout.TexC = texcoords[id];
    
    return vout;
}

float3 ComputeWorldPos(float2 texcoord)
{
    float depth = gDepth.Load(int3(texcoord * gRTSize, 0)).r;
    
    float4 ndc;
    ndc = float4(texcoord * 2.f - 1.f, depth, 1);
    
    float4 viewPos = mul(ndc, gInvViewProj);
    
    return viewPos.xyz;
}

float4 LightingPS(VertexOut pin) : SV_Target
{
    if (!mainLightIsOn)
    {
        return float4(0, 0, 0, 1);
    }
    
    float4 albedo = gDiffuse.Load(int3(pin.TexC * gRTSize, 0));
    
    float3 normal = gNormal.Load(int3(pin.TexC * gRTSize, 0)).xyz;
    
    float3 posW = ComputeWorldPos(pin.TexC);
    
    float3 finalColor = albedo.rgb * mainLightColor * dot(normal, -mainLightDirection);
    
    return float4(finalColor, albedo.a);
}
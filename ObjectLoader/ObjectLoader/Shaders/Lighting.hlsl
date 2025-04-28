#include "LightingUtil.hlsl"

Texture2D gDiffuse : register(t0);
Texture2D gNormal : register(t1);
Texture2D gDepth : register(t2);

cbuffer cbLightingPass : register(b0)
{
    float3 gLightPosW; // Light world position
    float gLightRange; // Light range
    float3 gLightColor; // Light color (RGB)
    float gPad0;
    
    float4x4 gInvViewProj;
    
    float2 gRTSize;
    float3 gEyePosW;
    float gPad1;
};

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
    vout.TexC = texcoords[id] * .5f + .5f;
    
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
    float4 albedo = gDiffuse.Load(int3(pin.TexC * gRTSize, 0));
    float3 normal = gNormal.Load(int3(pin.TexC * gRTSize, 0)).xyz;
    
    float3 posW = ComputeWorldPos(pin.TexC);
    
    float3 lightVec = gLightPosW - posW;
    float dist = length(lightVec);
    lightVec /= dist;
    
    float atten = saturate(1.f - dist / gLightRange);
    
    float diff = saturate(dot(normal, lightVec));
    
    float3 finalColor = albedo.rgb * gLightColor * diff * atten;
    
    return float4(finalColor, 1);
}
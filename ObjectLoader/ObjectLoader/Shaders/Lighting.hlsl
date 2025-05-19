#include "LightingUtil.hlsl"

struct Light
{
    float4x4 world;
    float3 position;
    int type;
    float3 direction;
    float radius;
    float3 color;
    float angle;
    int active;
    float intensity;
};

Texture2D gDiffuse : register(t0);
Texture2D gNormal : register(t1);
Texture2D gDepth : register(t2);
StructuredBuffer<Light> lights : register(t0, space1);

cbuffer cbLightingPass : register(b0)
{
    float4x4 gInvViewProj;
    float4x4 gViewProj;
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

struct VertexIn
{
    float3 PosL : POSITION;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
    int InstanceId : SV_InstanceID;
};

VertexOut DirLightingVS(uint id : SV_VertexID)
{
    VertexOut vout;
	
    float2 positions[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
    
    float2 texcoords[3] = { float2(0, 1), float2(0, -1), float2(2, 1) };
    
    vout.PosH = float4(positions[id], 0, 1);
    vout.TexC = texcoords[id];
    vout.InstanceId = 0;
    
    return vout;
}

float3 ComputeWorldPos(float2 texcoord)
{
    float depth = gDepth.Load(int3(texcoord * gRTSize, 0)).r;
    
    float4 ndc = float4(texcoord * 2.f - 1.f, depth, 1.0f); // now full NDC space
    float4 viewPos = mul(ndc, gInvViewProj);
    return viewPos.xyz / viewPos.w;
}

float4 DirLightingPS(VertexOut pin) : SV_Target
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


VertexOut LocalLightingVS(VertexIn vin, uint id : SV_InstanceID)
{
    VertexOut vout;
    
    float4 posW = mul(float4(vin.PosL, 1), lights[id].world);
    vout.PosH = mul(posW, gViewProj);
    vout.TexC = vout.PosH.xy * .5f + .5f;
    vout.InstanceId = id;
    
    return vout;
}

float4 LocalLightingPS(VertexOut pin) : SV_Target
{
    Light light = lights[pin.InstanceId];
    if (light.active == 0)
    {
        discard;
    }
    
    float4 albedo = gDiffuse.Load(int3(pin.TexC * gRTSize, 0));
    
    float3 normal = gNormal.Load(int3(pin.TexC * gRTSize, 0)).xyz;
    
    float3 posW = ComputeWorldPos(pin.TexC);
    
    float depth = gDepth.Load(int3(pin.TexC * gRTSize, 0)).r;
    
    return float4(depth, depth, depth, 1);
    
    if (length(light.position - posW) > light.radius)
    {
        discard;
    }
    
    
    float3 lightDir = normalize(posW - light.position);
    
    if (light.type == 1)
    {
        float angle = dot(lightDir, normalize(light.direction));
        if (angle < cos(light.angle))
        {
            discard;
        }
    }
    
    float diff = saturate(dot(normal, lightDir));
    float attenuation = saturate(1.0f - length(lightDir) / light.radius);
    
    float3 finalColor = albedo.rgb * light.color * diff * attenuation * light.intensity;
    
    return float4(finalColor, albedo.a);
}
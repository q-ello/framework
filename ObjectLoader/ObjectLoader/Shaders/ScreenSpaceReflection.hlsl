#include "LightingCommon.hlsl"

Texture2D gARM : register(t0);
Texture2D gNormal : register(t1);
Texture2D gLitScene : register(t2);

cbuffer cbGodRaysParameters : register(b2)
{
    int gSamples;
    float gDecay;
    float gExposure;
    float gDensity;
    float gWeight;
    float3 pad;
};

float4 GodRaysPS(VertexOut pin) : SV_Target
{
    float2 uv = pin.PosH.xy / gRTSize;

    float farDist = length(gEyePosW) + 100.0f;
    float3 lightPointWS = gEyePosW - normalize(mainLightDirection) * farDist;
    float4 clipLight = mul(float4(lightPointWS, 1.0f), gViewProj);
    if (abs(clipLight.w) < 1e-6)
        return float4(0, 0, 0, 1);

    float2 lightUV = (clipLight.xy / clipLight.w) * 0.5f + 0.5f;
    lightUV.y = 1 - lightUV.y;

    float2 deltaTexCoord = uv - lightUV;
    deltaTexCoord *= 1.0f / (float) gSamples * gDensity;
    float illuminationDecay = 1.0f;
    float3 color = gLightOcclusionMask.Sample(gsamLinear, uv, 0).r * mainLightColor * 0.4f;

    for (int i = 0; i < gSamples; ++i)
    {
        uv -= deltaTexCoord;
        float3 sample = gLightOcclusionMask.Sample(gsamLinear, uv, 0).r * mainLightColor;
        sample *= illuminationDecay * gWeight;
        color += sample;
        illuminationDecay *= gDecay;
    }

    return float4(color.rgb * gExposure, 1.0f);
}
#include "FullscreenVS.hlsl"
#include "Helpers.hlsl"

Texture2D gLitScene : register(t0);

cbuffer ChromaticAberrationParameters : register(b1)
{
    float strength;
};

float4 PS(VertexOut pin) : SV_Target
{
    float3 coords = (int3) pin.PosH.xyz;
    float2 uv = coords.xy / gRTSize;
    
    float2 centeredUV = uv - 0.5;
    float radius = length(uv);
    float aberration = radius * strength;

    float2 uvR = 0.5 + centeredUV * (1.0 + aberration);
    float2 uvG = uv;
    float2 uvB = 0.5 + centeredUV * (1.0 - aberration);
    
    float3 col;
    col.r = gLitScene.Sample(gsamLinear, uvR);
    col.g = gLitScene.Sample(gsamLinear, uvG);
    col.b = gLitScene.Sample(gsamLinear, uvB);
    
    // fade near screen edges (to avoid hard clamp artifacts)
    float edgeFade = smoothstep(1.0, 0.95, max(abs(centeredUV.x * 2.0), abs(centeredUV.y * 2.0)));
    col *= edgeFade;
    
    return float4(col, 1);
}
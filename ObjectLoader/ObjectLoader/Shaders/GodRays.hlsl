#include "LightingCommon.hlsl"

Texture2D gDepth : register(t0);
Texture2DArray gCascadesShadowMap : register(t1);

float4 LightOcclusionPS(VertexOut pin) : SV_Target
{
    float3 coords = pin.PosH.xyz;
    
    //directional light
    float4 finalColor = float4(0, 0, 0, 1);
    float3 posW = ComputeWorldPos(coords.xy / gRTSize, gDepth);

    float3 mask = 0.0f;
    
    if (mainLightIsOn)
    {
        int cascadeIdx = CalculateCascadeIndex(posW);
        //main light intensity is 3

        mask = mainLightColor * ShadowFactor(posW, cascades[cascadeIdx].viewProj, cascadeIdx, gCascadesShadowMap);
    }
    
    return float4(mask, 1.f);
}

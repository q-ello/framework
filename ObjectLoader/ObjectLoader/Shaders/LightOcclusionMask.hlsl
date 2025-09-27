#include "LightingCommon.hlsl"

Texture2D gDepth : register(t0);
Texture2DArray gCascadesShadowMap : register(t1);

float LightOcclusionPS(VertexOut pin) : SV_Target
{
    float3 coords = pin.PosH.xyz;
    
    float visibility = 0.0f;
    
    float3 posW = ComputeWorldPos(coords.xy / gRTSize * 2, gDepth);

    int cascadeIdx = CalculateCascadeIndex(posW);
        //main light intensity is 3

    visibility = ShadowFactor(posW, cascades[cascadeIdx].viewProj, cascadeIdx, gCascadesShadowMap);
    
    return visibility;
}
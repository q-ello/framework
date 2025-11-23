#include "FullscreenVS.hlsl"

Texture2D gLitScene : register(t0);
Texture2D gHistory : register(t1);
Texture2D gDepth : register(t2);

cbuffer TAACB : register(b0, space1)
{
    float4x4 gPrevViewProj;
    float4x4 gCurrInvViewProj;
    float2 gScreenSize;
    float2 pad;
}

SamplerComparisonState gsamShadow : register(s0);
SamplerState gsamLinear : register(s1);
SamplerState gsamLinearWrap : register(s2);

static const float velocityThreshold = 0.01f;

float3 reconstructWorldPosition(float2 uv, float depth)
{
    float4 clipPos = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 viewPos = mul(clipPos, gCurrInvViewProj);
    viewPos /= viewPos.w;
    return viewPos.xyz;
}

float4 TAAPS(VertexOut pin) : SV_Target
{
    float4 currentColor = gLitScene.Load(pin.PosH.xyz);
    
    float4 output;
    
     //getting world position but from previous frame
    float3 worldPos = reconstructWorldPosition(pin.PosH.xy / gScreenSize, gDepth.Load(pin.PosH.xyz).r);
    float4 prevScreenNDC = mul(float4(worldPos, 1.0f), gPrevViewProj);
    prevScreenNDC /= prevScreenNDC.w;
    float2 prevUV = float2(prevScreenNDC.x * 0.5f + 0.5f, 0.5f - prevScreenNDC.y * 0.5f);
    
    float4 historyColor = gHistory.Sample(gsamLinear, prevUV);
    
    output = lerp(currentColor, historyColor, 0.9f);
    
    return output;
}

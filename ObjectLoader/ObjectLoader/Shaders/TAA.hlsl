#include "FullscreenVS.hlsl"

Texture2D gLitScene : register(t0);
Texture2D gHistory : register(t1);
Texture2D gCurrentDepth : register(t2);
Texture2D gPreviousDepth : register(t3);

cbuffer TAACB : register(b0, space1)
{
    float4x4 gPrevView;
    float4x4 gPrevProj;
    float4x4 gPrevInvProj;
    float4x4 gCurrInvView;
    float4x4 gCurrInvProj;
    float2 gScreenSize;
    float2 pad;
}

SamplerComparisonState gsamShadow : register(s0);
SamplerState gsamLinear : register(s1);
SamplerState gsamLinearWrap : register(s2);

static const float depthThreshold = 0.2f;

float4 TAAPS(VertexOut pin) : SV_Target
{
    float4 currentColor = gLitScene.Load(pin.PosH.xyz);
    
    float4 output;

    float2 uv = pin.PosH.xy / gScreenSize;
    float depth = gCurrentDepth.Load(pin.PosH.xyz).r;
    
    float4 clipPos = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 currViewPos = mul(clipPos, gCurrInvProj);
    currViewPos /= currViewPos.w;
    
    float4 prevViewPos = mul(currViewPos, mul(gCurrInvView, gPrevView));
    float4 prevScreenNDC = mul(prevViewPos, gPrevProj);
    prevScreenNDC /= prevViewPos.w;
    float2 prevUV = float2(prevScreenNDC.x * 0.5f + 0.5f, 0.5f - prevScreenNDC.y * 0.5f);

    float predictedPrevViewDepth = prevViewPos.z;

    float4 actuaPrevClipPos = float4(prevScreenNDC.x, prevScreenNDC.y, gPreviousDepth.Sample(gsamLinear, prevUV).r, 1.0f);
    float4 actualPrevViewPos = mul(actuaPrevClipPos, gPrevInvProj);
    actualPrevViewPos /= actualPrevViewPos.w;

    float actualPrevViewDepth = actualPrevViewPos.z;

    if (abs(predictedPrevViewDepth - actualPrevViewDepth) > depthThreshold)
    {
        return currentColor;
    }

    float4 historyColor = gHistory.Sample(gsamLinear, prevUV);
    
    output = lerp(currentColor, historyColor, 0.9f);
    
    return output;
}

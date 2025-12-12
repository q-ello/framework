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

float4 TAAPS(VertexOut pin) : SV_Target
{
    int3 currCoords = int3(pin.PosH.xyz);
    float4 currentColor = gLitScene.Load(currCoords);
    
    float4 output;

    float2 uv = pin.PosH.xy / gScreenSize;
    float depth = gCurrentDepth.Load(currCoords).r;
    
    float4 clipPos = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 currViewPos = mul(clipPos, gCurrInvProj);
    currViewPos /= currViewPos.w;
    float4 world = mul(currViewPos, gCurrInvView);
    float4 prevViewPos = mul(world, gPrevView);
    float4 prevScreenNDC = mul(prevViewPos, gPrevProj);
    prevScreenNDC /= prevScreenNDC.w;
    float2 prevUV = float2(prevScreenNDC.x * 0.5f + 0.5f, 0.5f - prevScreenNDC.y * 0.5f);   

    if (prevUV.x < 0 || prevUV.x > 1 || prevUV.y < 0 || prevUV.y > 1)
        return currentColor;

    float predictedPrevViewDepth = prevViewPos.z;

    int3 prevCoords = int3(prevUV * gScreenSize, 0);

    float prevDepthNDC = gPreviousDepth.Load(prevCoords).r;

    float4 actuaPrevClipPos = float4(prevScreenNDC.x, prevScreenNDC.y, prevDepthNDC, 1.0f);
    float4 actualPrevViewPos = mul(actuaPrevClipPos, gPrevInvProj);
    actualPrevViewPos /= actualPrevViewPos.w;

    float actualPrevViewDepth = actualPrevViewPos.z;

    float depthDiff = abs(predictedPrevViewDepth - actualPrevViewDepth);
    float depthFactor = saturate(1.0f - depthDiff / predictedPrevViewDepth);  

    float4 historyColor = gHistory.Load(prevCoords);
    
    if (depthFactor == 0.0f)
    {
        float4 minC = 1e9, maxC = -1e9;
        float minDepth = -1e9, maxDepth = -1e9;
        static const int2 offs[4] = {int2(1,0), int2(-1,0), int2(0,1), int2(0,-1)};
        for(int i=0;i<4;i++){
            int3 neighbourCoords = int3(currCoords + offs[i], 0);
            float neighbourDepth = gCurrentDepth.Load(neighbourCoords).r;
            minDepth = min(minDepth, neighbourDepth);
            maxDepth = max(maxDepth, neighbourDepth);
            if (abs(neighbourDepth - depth) > 0.05)
                continue;

            minC = min(minC, neighbourDepth);
            maxC = max(maxC, neighbourDepth);
        }
        float depthSlope = maxDepth - minDepth;
        float tolerance = max(0.002, depthSlope * 2.0);

        bool badHistory = abs(prevDepthNDC - depth) > tolerance;

        if (badHistory)
            return currentColor;

        depthFactor = 0.9f;
        historyColor = clamp(historyColor, minC, maxC);
    }

    float histWeight = 0.9f * depthFactor;
    float3 finalColor = lerp(currentColor, historyColor, histWeight);

    output = float4(finalColor, 1.0f);

    return output;
}

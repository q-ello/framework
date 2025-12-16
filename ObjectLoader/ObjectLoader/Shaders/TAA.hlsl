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
    
    float4 minColor = 9999.0, maxColor = -9999.0;
    
    float2 uvOffset = float2(1.f, 1.f) / gScreenSize;
 
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float4 color = gLitScene.Sample(gsamLinear, uv + float2(x, y) * uvOffset); // Sample neighbor
            minColor = min(minColor, color); // Take min and max
            maxColor = max(maxColor, color);
        }
    }
 
    float4 historyColorClamped = clamp(historyColor, minColor, maxColor);
    
    output = lerp(currentColor, historyColorClamped, 0.9f);

    return output;
}

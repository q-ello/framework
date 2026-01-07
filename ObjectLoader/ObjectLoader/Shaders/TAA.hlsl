#include "FullscreenVS.hlsl"

Texture2D gLitScene : register(t0);
Texture2D gHistory : register(t1);
Texture2D gCurrentDepth : register(t2);
Texture2D gPreviousDepth : register(t3);
Texture2D gVelocity : register(t4);

cbuffer TAACB : register(b0)
{
    float2 gScreenSize;
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
    
    float2 velocity = gVelocity.Load(currCoords).xy;
    
    float2 historyUV = uv + velocity;
    float depth = gCurrentDepth.Load(currCoords).r;
    
    float prevDepth = gPreviousDepth.Sample(gsamLinear, historyUV).r;
    
    bool outOfBounds = historyUV.x < 0 || historyUV.x > 1 || historyUV.y < 0 || historyUV.y > 1;
    
    if (outOfBounds)
        return currentColor;

    historyUV = clamp(historyUV, 0.0f, 1.0f);
    
    float depthDiff = abs(prevDepth - depth);
    float depthThreshold = max(0.001f, depth * 0.01f);

    float historyConfidence = 1.0f;
    
    bool noHistory = prevDepth >= 0.9999f;

    float historyWeight = 0.9f;

    if (!noHistory)
    {
        float depthConfidence =
            saturate(1.0f - depthDiff / depthThreshold);
        //okay that one just makes a lot of flickering bruh
        //historyWeight *= depthConfidence;
    }
    
    float4 historyColor = gHistory.Sample(gsamLinear, historyUV);
    
    float4 minColor = 9999.0, maxColor = -9999.0;
    
    float2 uvOffset = float2(1.f, 1.f) / gScreenSize;
 
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            float4 color = gLitScene.Sample(gsamLinear, uv + float2(x, y) * uvOffset); // Sample neighbor
            minColor = min(minColor, color); // Take min and max
            maxColor = max(maxColor, color);
        }
    }
 
    float4 historyColorClamped = clamp(historyColor, minColor, maxColor);
    
    output = lerp(currentColor, historyColorClamped, historyWeight);
    
    return output;
}

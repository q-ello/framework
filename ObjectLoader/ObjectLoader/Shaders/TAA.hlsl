#include "FullscreenVS.hlsl"
#include "Helpers.hlsl"

Texture2D gLitScene : register(t0);
Texture2D gHistory : register(t1);

float4 TAAPS(VertexOut pin) : SV_Target
{
    float4 currentColor = gLitScene.Load(pin.PosH.xyz);
    float4 historyColor = gHistory.Load(pin.PosH.xyz);
    
    float4 output = lerp(currentColor, historyColor, 0.9f);
    
    return output;
}

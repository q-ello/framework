#include "FullscreenVS.hlsl"

Texture2D gMiddlewareTexture : register(t0);

float4 PS(VertexOut pin) : SV_Target
{
    return gMiddlewareTexture.Load(pin.PosH.xyz);
}
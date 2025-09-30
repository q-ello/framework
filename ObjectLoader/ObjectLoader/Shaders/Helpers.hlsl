cbuffer cbLightingPass : register(b0)
{
    float4x4 gInvViewProj;
    float4x4 gViewProj;
    float3 gEyePosW;
    float pad1;
    float2 gRTSize;
    float2 mousePos;
};

SamplerComparisonState gsamShadow : register(s0);
SamplerState gsamLinear : register(s1);
SamplerState gsamLinearWrap : register(s2);

float3 ComputeWorldPos(float2 uv, Texture2D depthTexture)
{
    float depth = depthTexture.SampleLevel(gsamLinear, uv, 0).r;
    
    float4 ndc = float4(uv.x * 2.f - 1.f, 1.f - uv.y * 2.f, depth, 1.0f);
    float4 viewPos = mul(ndc, gInvViewProj);
    return viewPos.xyz / viewPos.w;
}

#include "FullscreenVS.hlsl"
#include "Helpers.hlsl"

Texture2D gLitScene : register(t0);
Texture2D gNormal : register(t1);
Texture2D gARM : register(t2);
Texture2D gDepth : register(t3);

cbuffer SSRParameters
{
    float4x4 gInvProj;
    int StepScale;
    int MaxSteps;
    int MaxScreenDistance;
    float pad;
};

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float3 ReconstructViewPos(float2 uv, float depth)
{
    float4 ndc;
    ndc.x = uv.x * 2.0f - 1.0f;
    ndc.y = (1.0f - uv.y) * 2.0f - 1.0f;
    ndc.z = depth;
    ndc.w = 1.0f;

    float4 posView = mul(ndc, gInvProj);
    posView /= posView.w;

    return posView.xyz;
}

float4 PS(VertexOut pin) : SV_Target
{
    float3 coords = (int3) pin.PosH.xyz;
    float depth = gDepth.Load(coords).r;
    if (depth >= 1.0f)
        return gLitScene.Load(coords);
    
    float3 posW = ComputeWorldPos(coords.xy / gRTSize, gDepth);
    float3 posView = ReconstructViewPos(coords.xy / gRTSize, gDepth.Load(coords).r);
    float3 normalW = gNormal.Load(coords).xyz;
    
    float3 V = normalize(posW - gEyePosW);
    float3 N = normalize(normalW);
    float3 R = normalize(reflect(V, N));
    
    float4 arm = gARM.Load(coords);
    float ao = arm.r;
    float roughness = arm.g;
    float metallic = arm.b;
    
    float3 albedo = gLitScene.Load(coords).rgb;
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    
    float cosTheta = saturate(dot(V, N));
    float3 fresnel = FresnelSchlick(cosTheta, F0);
    
    float3 marchPos = posW;
    
    float2 uv1 = coords.xy / gRTSize;
    
    float3 pos2 = posW + R;
    float4 ndc2 = mul(float4(pos2, 1), gViewProj);
    float2 uv2 = (ndc2.xy / ndc2.w) * 0.5f + 0.5f;
    uv2.y = 1 - uv2.y;
    
    float2 currUV = uv1;
    
    float2 uvStep = normalize(uv2 - uv1) * ((int) StepScale / gRTSize.x);;
    int traveled = 0.0f;
    
    [loop]
    for (int i = 0; i < MaxSteps; i++)
    {
        currUV += uvStep;
        traveled += StepScale;

        if (currUV.x < 0 || currUV.x > 1 || currUV.y < 0 || currUV.y > 1)
            break;
        
        if (traveled > MaxScreenDistance)
            break;

        float depthSample = gDepth.SampleLevel(gsamLinearWrap, currUV, 0).r;
        float3 posViewSample = ReconstructViewPos(currUV, depthSample);

        if (posViewSample.z < posView.z) // hit found
        {
            float lod = roughness * 5.0f;
            float3 reflectedColor = gLitScene.SampleLevel(gsamLinearWrap, currUV, lod).rgb;
            float3 baseColor = gLitScene.Load(coords).rgb;
            float3 result = lerp(baseColor, reflectedColor, fresnel * metallic);
           
            return float4(result, 1.0f);
        }
    }
    
    return gLitScene.Load(coords);
}
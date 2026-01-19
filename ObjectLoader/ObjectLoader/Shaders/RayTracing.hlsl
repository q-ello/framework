#include "Helpers.hlsl"

RaytracingAccelerationStructure SceneBVH : register(t0);
Texture2D<float4> gBufferNorm : register(t1);
Texture2D<float4> gBufferDepth : register(t2);
RWTexture2D<float> shadowMask : register(u0);

cbuffer SunCB : register(b1)
{
    float3 SunDirectionWS;
    float  Pad;
};

struct ShadowPayload
{
    uint occluded;
};


[shader("raygeneration")]
void RayGenShadows()
{
    uint2 pixel = DispatchRaysIndex().xy;

    uint2 dim   = DispatchRaysDimensions().xy;

    float2 uv = (pixel + 0.5) / dim;

    float3 pos  = ComputeWorldPos(uv, gBufferDepth);
    float3 norm = normalize(gBufferNorm[pixel].xyz);
    float depth = gBufferDepth[pixel].x;

    if (depth >= 0.99f)
    {
        shadowMask[pixel] = 1;
        return;
    }

    float3 rayDir = normalize(-SunDirectionWS);

    RayDesc ray;
    ray.Origin = pos + norm * 1e-3; // bias
    ray.Direction = rayDir;
    ray.TMin = 0.001;
    ray.TMax = 1e6;

    ShadowPayload payload;
    payload.occluded = 0;

    TraceRay(
        SceneBVH,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
        RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        0xFF,
        0, 1, 0,
        ray,
        payload
    );

    float shadow = payload.occluded ? 0.0 : 1.0;
    shadowMask[pixel] = shadow.x;
}

[shader("closesthit")]
void ShadowClosestHit(inout ShadowPayload payload,
                      BuiltInTriangleIntersectionAttributes attr)
{
    payload.occluded = 0;
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
    payload.occluded = 0;
}

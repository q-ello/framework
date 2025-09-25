struct Light
{
    float4x4 world;
    float3 position;
    int type;
    float3 direction;
    float radius;
    float3 color;
    float angle;
    int active;
    float intensity;
    float2 pad;
};

struct LightIndex
{
    int index;
    int pad[3]; // keep aligned
};

static const int gCascadesCount = 3;
static const float gShadowMapResolution = 1024.0f;

SamplerComparisonState gsamShadow : register(s0);
SamplerState gsamLinear : register(s1);

static const float PI = 3.14159265f;

StructuredBuffer<Light> lights : register(t0, space1);
StructuredBuffer<LightIndex> lightIndices : register(t1, space1);

cbuffer cbLightingPass : register(b0)
{
    float4x4 gInvViewProj;
    float4x4 gViewProj;
    float3 gEyePosW;
    float pad1;
    float2 gRTSize;
    float2 mousePos;
};

struct Cascade
{
    float splitNear;
    float splitFar;
    float4x4 viewProj;
};

cbuffer cbDirLight : register(b1)
{
    float3 mainLightDirection;
    int mainLightIsOn;
    float3 mainLightColor;
    int lightsContainingFrustum;
    Light mainSpotlight;
    Cascade cascades[3];
};

struct VertexIn
{
    float3 PosL : POSITION;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    int lightIndex : TEXCOORD;
};

bool IsPixelLit(Light light, float3 posW)
{
    if (light.active == 0)
    {
        return false;
    }
    
    float3 lightDir = posW - light.position;
    float3 normalizedLightDir = normalize(lightDir);
    
    if (length(lightDir) > light.radius)
    {
        return false;
    }
    
    float angle = dot(normalizedLightDir, normalize(light.direction));

    if (light.type == 1)
    {
        
        if (angle < cos(light.angle))
        {
            return false;
        }
    }
    
    return true;
}

//compute shadows
float ShadowFactor(float3 worldPos, float4x4 transformMatrix, int index, Texture2DArray array)
{
    float4 posLS = mul(float4(worldPos, 1), transformMatrix);
    float3 ndc = posLS.xyz / posLS.w;
    float2 uv = ndc.xy * 0.5f + 0.5f;
    //damn you flipping
    uv.y = 1 - uv.y;
    
    //bias here because bias on cpu doesn't fucking work
    float depth = ndc.z - 0.005;
    
    // PCF
    float2 texelSize = 1.0f / gShadowMapResolution;
    float shadow = 0.0f;
    int count = 0;
    
    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            float2 offset = float2(x, y) * texelSize;
            shadow += array.SampleCmpLevelZero(
                          gsamShadow,
                          float3(uv + offset, index),
                          depth);
            count++;
        }
    }

    return lerp(0.2f, 1.f, shadow / count);
}

int CalculateCascadeIndex(float3 worldPos)
{
    float depth = length(worldPos - gEyePosW);
    for (int i = 0; i < gCascadesCount; i++)
    {
        if (depth < cascades[i].splitFar)
        {
            return i;
        }
    }
    return gCascadesCount - 1;
}

float3 ComputeWorldPos(float2 uv, Texture2D depthTexture)
{
    float depth = depthTexture.SampleLevel(gsamLinear, uv, 0).r;
    
    float4 ndc = float4(uv.x * 2.f - 1.f, 1.f - uv.y * 2.f, depth, 1.0f);
    float4 viewPos = mul(ndc, gInvViewProj);
    return viewPos.xyz / viewPos.w;
}

//full screen quad
VertexOut DirLightingVS(uint id : SV_VertexID)
{
    VertexOut vout;
	
    float2 positions[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
    
    vout.PosH = float4(positions[id], 0, 1);
    vout.lightIndex = 0;
    
    return vout;
}

VertexOut LocalLightingVS(VertexIn vin, uint id : SV_InstanceID)
{
    VertexOut vout;
    
    float4 posW = mul(float4(vin.PosL, 1), lights[lightIndices[id].index].world);
    vout.PosH = mul(posW, gViewProj);
    vout.lightIndex = lightIndices[id].index;
    
    return vout;
}
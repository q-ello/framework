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

Texture2D gDiffuse : register(t0);
Texture2D gNormal : register(t1);
Texture2D gDepth : register(t2);
StructuredBuffer<Light> lights : register(t0, space1);

cbuffer cbLightingPass : register(b0)
{
    float4x4 gInvViewProj;
    float4x4 gViewProj;
    float3 gEyePosW;
    float pad1;
    float2 gRTSize;
    float2 mousePos;
};

cbuffer cbDirLight : register(b1)
{
    float3 mainLightDirection;
    int mainLightIsOn;
    float3 mainLightColor;
    float pad2;
    Light mainSpotlight;
};

struct VertexIn
{
    float3 PosL : POSITION;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    int InstanceId : SV_InstanceID;
};

VertexOut DirLightingVS(uint id : SV_VertexID)
{
    VertexOut vout;
	
    float2 positions[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
    
    vout.PosH = float4(positions[id], 0, 1);
    vout.InstanceId = 0;
    
    return vout;
}

float3 ComputeWorldPos(float3 texcoord)
{
    float depth = gDepth.Load(texcoord).r;
    
    float2 uv = texcoord.xy / gRTSize;
    
    float4 ndc = float4( uv.x * 2.f - 1.f, 1.f - uv.y * 2.f, depth, 1.0f);
    float4 viewPos = mul(ndc, gInvViewProj);
    return viewPos.xyz / viewPos.w;
}

float4 DirLightingPS(VertexOut pin) : SV_Target
{   
    float3 coords = pin.PosH.xyz;
    
    float4 albedo = gDiffuse.Load(coords);
    if (albedo.a == 0)
    {
        discard;
    }
    if (!mainLightIsOn && mainSpotlight.active == 0)
    {
        return float4(0, 0, 0, 1);
    }
    
    float3 normal = gNormal.Load(coords).xyz;
    
    float3 finalColor = albedo.rgb * mainLightColor * dot(normal, -mainLightDirection);

    if (mainSpotlight.active == 0)
    {
        return float4(finalColor, albedo.a);
    }
    
    float3 mousePosW = ComputeWorldPos(float3(mousePos, 0));
    float3 spotlightDir = mousePosW - gEyePosW;

    float3 posW = ComputeWorldPos(coords);
    float3 currDir = posW - gEyePosW;
    float3 normalizedCurrDir = normalize(currDir);
    
    if (length(currDir) > mainSpotlight.radius)
    {
        return float4(finalColor, albedo.a);
    }
    
    float angle = dot(normalizedCurrDir, normalize(spotlightDir));
    if (angle < cos(mainSpotlight.angle))
    {
        return float4(finalColor, albedo.a);
    }
    
    float diff = saturate(dot(normal, -normalizedCurrDir));
    
    float attenuation = saturate(1.0f - length(currDir) / mainSpotlight.radius);
    float spotFactor = saturate((angle - cos(mainSpotlight.angle)) / (1.0f - cos(mainSpotlight.angle)));
    spotFactor = pow(spotFactor, 4.f);
    
    finalColor += albedo.rgb * mainSpotlight.color * diff * attenuation * mainSpotlight.intensity * spotFactor;
    
    return float4(finalColor, albedo.a);
}

VertexOut LocalLightingVS(VertexIn vin, uint id : SV_InstanceID)
{
    VertexOut vout;
    
    float4 posW = mul(float4(vin.PosL, 1), lights[id].world);
    vout.PosH = mul(posW, gViewProj);
    vout.InstanceId = id;
    
    return vout;
}

float4 LocalLightingPS(VertexOut pin) : SV_Target
{
    Light light = lights[pin.InstanceId];
    if (light.active == 0)
    {
        discard;
    }
    
    float3 coords = pin.PosH.xyz;
    
    float4 albedo = gDiffuse.Load(coords);
    
    float3 normal = gNormal.Load(coords).xyz;
    
    float3 posW = ComputeWorldPos(coords);
    
    float3 lightDir = posW - light.position;
    float3 normalizedLightDir = normalize(lightDir);
    
    if (length(lightDir) > light.radius)
    {
        discard;
    }
    
    float angle = dot(normalizedLightDir, normalize(light.direction));
    float spotFactor = 1.f;

    if (light.type == 1)
    {
        
        if (angle < cos(light.angle))
        {
            discard;
        }
        spotFactor = saturate((angle - cos(light.angle)) / (1.0f - cos(light.angle)));
        spotFactor = pow(spotFactor, 4.f);
    }
    
    float diff = saturate(dot(normal, -normalizedLightDir));
    
    float attenuation = saturate(1.0f - length(lightDir) / light.radius);

    float3 finalColor = albedo.rgb * light.color * diff * attenuation * light.intensity * spotFactor;
    
    return float4(finalColor, albedo.a);
}



float4 LocalLightingWireframePS(VertexOut pin) : SV_Target
{
    Light light = lights[pin.InstanceId];
    if (light.active == 0)
    {
        discard;
    }
    return float4(light.color, 1);
}
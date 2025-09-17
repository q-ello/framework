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

Texture2D gBaseColor : register(t0);
Texture2D gNormal : register(t1);
Texture2D gEmissive : register(t2);
Texture2D gORM : register(t3);
Texture2D gDepth : register(t4);

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

cbuffer cbDirLight : register(b1)
{
    float3 mainLightDirection;
    int mainLightIsOn;
    float3 mainLightColor;
    int lightsContainingFrustum;
    Light mainSpotlight;
    float4x4 mainLightInvView;
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

//pbr stuff
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;

    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}


float3 ComputeWorldPos(float3 texcoord)
{
    float depth = gDepth.Load(texcoord).r;
    
    float2 uv = texcoord.xy / gRTSize;
    
    float4 ndc = float4(uv.x * 2.f - 1.f, 1.f - uv.y * 2.f, depth, 1.0f);
    float4 viewPos = mul(ndc, gInvViewProj);
    return viewPos.xyz / viewPos.w;
}

float3 PBRShading(float3 coords, float3 lightDir, float3 lightColor)
{
    float3 N = normalize(gNormal.Load(coords));
    float3 worldPos = ComputeWorldPos(coords);
    float3 V = normalize(gEyePosW - worldPos);
    float3 L = normalize(lightDir);
    float3 H = normalize(V + L);
    
    float3 albedo = gBaseColor.Load(coords).xyz;
    float3 orm = gORM.Load(coords).xyz;
    float ao = orm.x;
    float roughness = orm.g;
    float metallic = orm.b;

    float3 F0 = float3(0.04, 0.04, 0.04); // default for dielectrics
    F0 = lerp(F0, albedo, metallic); // metals use albedo as F0

    // cook-torrance BRDF
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(saturate(dot(H, V)), F0);

    float3 numerator = D * G * F;
    float denominator = 4 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    float NdotL = max(dot(N, L), 0.0);

    float3 diffuse = kD * albedo / PI;

    float3 ambient = 0.1 * ao * albedo;

    float3 color = (diffuse + specular) * lightColor * NdotL;

    return ambient + color;
}

//shader stuff
VertexOut DirLightingVS(uint id : SV_VertexID)
{
    VertexOut vout;
	
    float2 positions[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
    
    vout.PosH = float4(positions[id], 0, 1);
    vout.lightIndex = 0;
    
    return vout;
}

//helper for local lights
float4 ComputeLocalLighting(int lightIndex, float3 coords)
{
    Light light = lights[lightIndex];
    if (light.active == 0)
    {
        return float4(0, 0, 0, 0);
    }
    
    float4 albedo = gBaseColor.Load(coords);
    
    float3 normal = gNormal.Load(coords).xyz;
    
    float3 posW = ComputeWorldPos(coords);
    
    float3 lightDir = posW - light.position;
    float3 normalizedLightDir = normalize(lightDir);
    
    if (length(lightDir) > light.radius)
    {
        return float4(0, 0, 0, 0);
    }
    
    float angle = dot(normalizedLightDir, normalize(light.direction));
    float spotFactor = 1.f;

    if (light.type == 1)
    {
        
        if (angle < cos(light.angle))
        {
            return float4(0, 0, 0, 0);
        }
        spotFactor = saturate((angle - cos(light.angle)) / (1.0f - cos(light.angle)));
        spotFactor = pow(spotFactor, 4.f);
    }
    
    float attenuation = saturate(1.0f - length(lightDir) / light.radius);
    
    float finalIntensity = attenuation * spotFactor * light.intensity;
    
    float3 finalColor = PBRShading(coords, -normalizedLightDir, light.color) * finalIntensity;

    return float4(finalColor, albedo.a);
}

//actually just a full quad pass, not only for directional light
float4 DirLightingPS(VertexOut pin) : SV_Target
{   
    float3 coords = pin.PosH.xyz;
    
    float4 albedo = gBaseColor.Load(coords);
    if (albedo.a == 0)
    {
        discard;
    }
    
    //directional light
    float4 finalColor = float4(0, 0, 0, albedo.a);
    if (mainLightIsOn)
    {
        //main light intensity is 3
        finalColor.xyz = PBRShading(coords, -mainLightDirection, mainLightColor) * 3.f;
    }
    
    //spotlight in hand
    if (mainSpotlight.active == 1)
    {
        float3 mousePosW = ComputeWorldPos(float3(mousePos, 0));
        float3 spotlightDir = mousePosW - gEyePosW;

        float3 posW = ComputeWorldPos(coords);
        float3 currDir = posW - gEyePosW;
        float3 normalizedCurrDir = normalize(currDir);
    
        if (length(currDir) <= mainSpotlight.radius)
        {
            float angle = dot(normalizedCurrDir, normalize(spotlightDir));
            float cutoff = cos(mainSpotlight.angle);
            
            if (angle >= cutoff)
            {
                float attenuation = saturate(1.0f - length(currDir) / mainSpotlight.radius);
                float spotFactor = saturate((angle - cutoff) / (1.0f - cutoff));
                spotFactor = pow(spotFactor, 4.f);
    
                float finalIntensity = mainSpotlight.intensity * attenuation * spotFactor;
    
                finalColor.xyz += PBRShading(coords, -normalizedCurrDir, mainSpotlight.color) * finalIntensity;
            }
        }
    }
    
    //lighting for every light we are inside of
    for (int i = 0; i < lightsContainingFrustum; i++)
    {
        finalColor.xyz += ComputeLocalLighting(lightIndices[i].index, coords).xyz;
    }
    
    return finalColor;
}

VertexOut LocalLightingVS(VertexIn vin, uint id : SV_InstanceID)
{
    VertexOut vout;
    
    float4 posW = mul(float4(vin.PosL, 1), lights[lightIndices[id].index].world);
    vout.PosH = mul(posW, gViewProj);
    vout.lightIndex = lightIndices[id].index;
    
    return vout;
}

float4 LocalLightingPS(VertexOut pin) : SV_Target
{
    float3 coords = pin.PosH.xyz;
    
    float4 finalColor = ComputeLocalLighting(pin.lightIndex, coords);
    if (finalColor.a == 0)
        discard;
    
    return finalColor;
}

float4 LocalLightingWireframePS(VertexOut pin) : SV_Target
{
    Light light = lights[pin.lightIndex];
    if (light.active == 0)
    {
        discard;
    }
    return float4(light.color, 1);
}

float4 EmissivePS(VertexOut pin) : SV_Target
{
    float3 coords = pin.PosH.xyz;
    float4 emissive = gEmissive.Load(pin.PosH.xyz);
    if (gEmissive.Load(coords).a <= 0)
    {
        discard;
    }
    return float4(emissive.xyz * emissive.a, 0);
}
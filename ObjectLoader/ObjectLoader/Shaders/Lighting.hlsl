#include "LightingCommon.hlsl"

Texture2D gBaseColor : register(t0);
Texture2D gEmissive : register(t1);
Texture2D gNormal : register(t2);
Texture2D gORM : register(t3);
Texture2D gDepth : register(t4);
Texture2DArray gCascadesShadowMap : register(t5);
Texture2DArray gShadowMap : register(t6);
TextureCube gSky : register(t7);

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

float3 PBRShading(float3 coords, float3 lightDir, float3 lightColor, float3 worldPos)
{
    float3 N = normalize(gNormal.Load(coords).xyz);
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

//helper for local lights
float4 ComputeLocalLighting(int lightIndex, float3 posW, float3 coords)
{
    Light light = lights[lightIndex];
    
    if (!IsPixelLit(light, posW))
        return float4(0, 0, 0, 0);
    
    float4 albedo = gBaseColor.Load(coords);
    
    float3 normal = gNormal.Load(coords).xyz;
    
    float3 lightDir = posW - light.position;
    float3 normalizedLightDir = normalize(lightDir);
    
    float spotFactor = 1.f;

    if (light.type == 1)
    {
        float angle = dot(normalizedLightDir, normalize(light.direction));
        spotFactor = saturate((angle - cos(light.angle)) / (1.0f - cos(light.angle)));
        spotFactor = pow(spotFactor, 4.f);
    }
    
    float attenuation = saturate(1.0f - length(lightDir) / light.radius);
    
    float finalIntensity = attenuation * spotFactor * light.intensity;
    
    float3 finalColor = PBRShading(coords, -normalizedLightDir, light.color, posW) * finalIntensity;

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
    float3 posW = ComputeWorldPos(coords.xy / gRTSize, gDepth);
    
    //tiny normal offset to avoid acne
    float3 normalW = normalize(gNormal.Load(coords).xyz);
    float3 posOffseted = posW + normalW * 0.01f;
    
    float shadowFactor;
    
    if (mainLightIsOn)
    {
        int cascadeIdx = CalculateCascadeIndex(posW);
        //main light intensity is 3
        finalColor.xyz = PBRShading(coords, -mainLightDirection, mainLightColor, posW) * 3.f;
        shadowFactor = ShadowFactor(posOffseted, cascades[cascadeIdx].viewProj, cascadeIdx, gCascadesShadowMap);
        finalColor.xyz *= lerp(0.2f, 1.f, shadowFactor);
    }
    
    //spotlight in hand
    if (mainSpotlight.active == 1)
    {
        float3 mousePosW = ComputeWorldPos(float3(mousePos, 0).xy / gRTSize, gDepth);
        float3 spotlightDir = mousePosW - gEyePosW;

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
    
                finalColor.xyz += PBRShading(coords, -normalizedCurrDir, mainSpotlight.color, posW) * finalIntensity;
            }
        }
    }
    
    //lighting for every light we are inside of
    for (int i = 0; i < lightsContainingFrustum; i++)
    {
        finalColor.xyz += ComputeLocalLighting(lightIndices[i].index, posW, coords).xyz;
    }
    
    //reflection
    float3 N = normalize(gNormal.Load(coords).xyz);
    float3 V = normalize(gEyePosW - posW);
    float3 R = reflect(-V, N);

    float3 orm = gORM.Load(coords).xyz;
    float metallic = orm.b;

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo.rgb, metallic);
    float3 F = FresnelSchlick(saturate(dot(N, V)), F0);

    float3 envReflection = gSky.Sample(gsamLinearWrap, R).rgb;
    finalColor.xyz += F * envReflection;
    
    return finalColor;
}

float4 LocalLightingPS(VertexOut pin) : SV_Target
{
    float3 coords = pin.PosH.xyz;
    
    float3 posW = ComputeWorldPos(coords.xy / gRTSize, gDepth);
    
    float4 finalColor = ComputeLocalLighting(pin.lightIndex, posW, coords);
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
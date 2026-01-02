#include "LightingCommon.hlsl"

Texture2D gBaseColor : register(t0);
Texture2D gNormal : register(t1);
Texture2D gEmissive : register(t2);
Texture2D gORM : register(t3);
Texture2D gTexCoord : register(t4);
Texture2D gDepth : register(t5);
#ifdef CSM
Texture2DArray gCascadesShadowMap : register(t6);
Texture2DArray gShadowMap : register(t7);
Texture2D gShadowMask : register(t8);
#endif

TextureCube gSkyIrradiance : register(t0, space2);
TextureCube gSkyPrefiltered : register(t1, space2);
Texture2D gSkyBRDF : register(t2, space2);

#ifdef CSM
cbuffer shadowMaskUVScale : register(b2)
{
    float scale;
};
#endif

//pbr stuff
float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.f * PI * Xi.x;
    float cosTheta = sqrt((1.0f - Xi.y) / (1.0f + (a * a - 1.0f) * Xi.y));
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
// from spherical coordinates to cartesian coordinates - halfway vector
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
// from tangent-space H vector to world-space sample vector
    float3 up = abs(N.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0) - F0) * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
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

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float3 PBRShading(float3 coords, float3 lightDir, float3 lightColor, float3 worldPos, float shadowFactor, float lightIntensity)
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

    float3 F0 = float3(0.04f, 0.04f, 0.04f); // default for dielectrics
    F0 = lerp(F0, albedo, metallic); // metals use albedo as F0

    // cook-torrance BRDF
    float D = DistributionGGX(N, H, roughness);
    float3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    float G = GeometrySmith(N, V, L, roughness);

    float3 numerator = D * G * F;
    float denominator = 4 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
    float3 specular = numerator / denominator;
    
    float3 kS = F;
    float3 kD = 1.f - kS;
    kD *= 1.f - metallic;
    float3 irradiance = gSkyIrradiance.Sample(gsamLinear, N).rgb;
    float3 diffuse = irradiance * albedo;
    float3 R = reflect(-V, N);
    const float MAX_REFLECTION_LOD = 7.0f;
    float3 prefilteredColor = gSkyPrefiltered.SampleLevel(gsamLinear, R, roughness * MAX_REFLECTION_LOD).rgb;
    float2 brdf = gSkyBRDF.Sample(gsamLinear, float2(max(dot(N, V), 0.0f), roughness)).rg;
    
    specular += prefilteredColor * (F * brdf.x + brdf.y);
    float3 ambient = (kD * diffuse + specular) * ao;
    
    float NdotL = max(dot(N, L), 0.0f);

    float3 color = (diffuse + specular) * lightColor * NdotL * shadowFactor * lightIntensity;

    return ambient + color;
}

//helper for local lights
float4 ComputeLocalLighting(int lightIndex, float3 posW, float3 coords)
{
    Light light = lights[lightIndex];

    float4 outputColor = float4(0, 0, 0, 0);
    
    if (IsPixelLit(light, posW))
    {
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
    
        float3 finalColor = PBRShading(coords, -normalizedLightDir, light.color, posW, 1.f, finalIntensity);

        outputColor = float4(finalColor, albedo.a);
    }
    return outputColor;
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
    
    float shadowFactor = 1.f;
    
    if (mainLightIsOn)
    {
        int cascadeIdx = CalculateCascadeIndex(posW);
        //main light intensity is 3
#ifdef CSM
        shadowFactor = ShadowFactor(posOffseted, cascades[cascadeIdx].viewProj, cascadeIdx, gCascadesShadowMap);
        shadowFactor += gShadowMask.Sample(gsamLinearWrap, gTexCoord.Load(coords).xy).a * scale;
#endif
        finalColor.xyz = PBRShading(coords, -mainLightDirection, mainLightColor, posW, saturate(shadowFactor), 1.f);
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
    
                finalColor.xyz += PBRShading(coords, -normalizedCurrDir, mainSpotlight.color, posW, 1.f, finalIntensity);
            }
        }
    }
    
    //lighting for every light we are inside of
    for (int i = 0; i < lightsContainingFrustum; i++)
    {
        finalColor.xyz += ComputeLocalLighting(lightIndices[i].index, posW, coords).xyz;
    }
    
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
#include "LightingCommon.hlsl"

Texture2D gBaseColor : register(t0);
Texture2D gEmissive : register(t1);
Texture2D gNormal : register(t2);
Texture2D gORM : register(t3);
Texture2D gTexCoord : register(t4);
Texture2D gDepth : register(t5);
Texture2DArray gCascadesShadowMap : register(t6);
Texture2DArray gShadowMap : register(t7);
Texture2D gShadowMask : register(t8);

TextureCube gSkyIrradiance : register(t0, space2);
TextureCube gSkyPrefiltered : register(t1, space2);
Texture2D gSkyBRDF : register(t2, space2);

cbuffer shadowMaskUVScale : register(b2)
{
    float scale;
};

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

    float3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    float3 kS = F;
    float3 kD = 1.f - kS;
    kD *= 1.f - metallic;
    float3 irradiance = gSkyIrradiance.Sample(gsamLinear, N).rgb;
    float3 diffuse = irradiance * albedo;
    float3 R = reflect(-V, N);
    const float MAX_REFLECTION_LOD = 7.0f;
    float3 prefilteredColor = gSkyPrefiltered.SampleLevel(gsamLinear, R, roughness * MAX_REFLECTION_LOD).rgb;
    float2 brdf = gSkyBRDF.Sample(gsamLinear, float2(max(dot(N, V), 0.0f), roughness)).rg;
    float3 specular = prefilteredColor * (F * brdf.x + brdf.y);
    float3 ambient = (kD * diffuse + specular) * ao;
    
    float NdotL = max(dot(N, L), 0.0f);

    float3 color = (diffuse + specular) * lightColor * NdotL * shadowFactor * lightIntensity;

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
    
    float3 finalColor = PBRShading(coords, -normalizedLightDir, light.color, posW, 1.f, finalIntensity);

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
        shadowFactor = ShadowFactor(posOffseted, cascades[cascadeIdx].viewProj, cascadeIdx, gCascadesShadowMap);
        shadowFactor += gShadowMask.Sample(gsamLinearWrap, gTexCoord.Load(coords).xy).a * scale;
        
        finalColor.xyz = PBRShading(coords, -mainLightDirection, mainLightColor, posW, saturate(shadowFactor), 1.f);
        //finalColor.xyz *= lerp(shadowFactor, 1.0f, gShadowMask.Sample(gsamLinearWrap, gTexCoord.Load(coords).xy * scale).a);
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
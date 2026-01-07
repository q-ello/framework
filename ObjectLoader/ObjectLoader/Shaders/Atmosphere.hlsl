#include "Helpers.hlsl"
#include "FullscreenVS.hlsl"

Texture2D gLitScene : register(t0);
Texture2D gDepth : register(t1);

static const float maxFloat = 3.402823466e+38F;

cbuffer cbScene : register(b1)
{
    float3 dirToSun;
    float atmosphereRadius;
    float3 planetCenter;
    float planetRadiusf;
    float3 wavelengths;
    int numInScatteringPoints;
    int numOpticalDepthPoints;
    float densityFalloff;
    int2 pad;
};


float2 RaySphere(float3 sphereCenter, float sphereRadius, float3 rayOrigin, float3 rayDir)
{
    float3 offset = rayOrigin - sphereCenter;
    float a = 1; //raydir should be normilized
    float b = 2 * dot(offset, rayDir);
    float c = dot(offset, offset) - sphereRadius * sphereRadius;
    float d = b*b - 4*a*c;
    
    float2 result;
    
    if (d > 0)
    {
        float s = sqrt(d);
        float dstToSphereNear = max(0, (-b - s) / (2 * a));
        float dstToSphereFar = (-b + s) / (2 * a);
        
        if (dstToSphereFar >= 0)
        {
            result = float2(dstToSphereNear, dstToSphereFar - dstToSphereNear);
        }
        else
        {
            result = float2(maxFloat, 0);
        }
    }
    else
    {
        result = float2(maxFloat, 0);
    }
    
    return result;
}

float densityAtPoint(float3 densitySamplePoint)
{
    float height = length(densitySamplePoint - planetCenter) - planetRadiusf; // Earth radius in km
    float height01 = saturate(height / (atmosphereRadius - planetRadiusf)); 
    float localDensity = exp(-height01 * densityFalloff) * (1 - height01); // scale height factor * 0.2
    return localDensity;
}

float opticalDepth(float3 rayOrigin, float3 rayDir, float rayLength)
{
    float3 samplePoint = rayOrigin;
    float stepSize = rayLength / (float) (numOpticalDepthPoints - 1);
    float opticalDepth = 0;
    
    for (int i = 0; i < numOpticalDepthPoints; i++)
    {
        float localDensity = densityAtPoint(samplePoint);
        opticalDepth += localDensity * stepSize;
        samplePoint += rayDir * stepSize;
    }
    
    return opticalDepth;
}

float3 calculateLight(float3 rayOrigin, float3 rayDir, float rayLength, float3 originalCol)
{
    float3 inScatterPoint = rayOrigin;
    float stepSize = rayLength / (float) (numInScatteringPoints - 1);
    float3 inScatteredLight = 0;
    float viewRayOpticalDepth = 0;
    
    float3 scatteringCoefficients = float3(
        pow(400.f / wavelengths.x, 4),
        pow(400.f / wavelengths.y, 4),
        pow(400.f / wavelengths.z, 4)
    );
    
    float sunDot = dot(rayDir, dirToSun);
    float sunDisc = smoothstep(0.9995, 1.0, sunDot);

    float3 sunColor = float3(1.0, 0.95, 0.9);
    float3 sun = sunDisc * sunColor * 20.0;
    
    for (int i = 0; i < numInScatteringPoints; i++)
    {
        float sunRayLength = RaySphere(planetCenter, atmosphereRadius, inScatterPoint, dirToSun).y;
        float sunRayOpticalDepth = opticalDepth(inScatterPoint, dirToSun, sunRayLength);
        viewRayOpticalDepth = opticalDepth(inScatterPoint, -rayDir, stepSize * i);
        float3 transmittance = exp(-(sunRayOpticalDepth + viewRayOpticalDepth) * scatteringCoefficients);
        float localDensity = densityAtPoint(inScatterPoint);
        
        inScatteredLight += localDensity * transmittance * scatteringCoefficients * stepSize;
        inScatterPoint += rayDir * stepSize;
    }
    
    //for sun
    float sunRayLength =
    RaySphere(planetCenter, atmosphereRadius, rayOrigin, dirToSun).y;

    float sunOpticalDepth =
        opticalDepth(rayOrigin, dirToSun, sunRayLength);

    float3 sunTransmittance =
        exp(-sunOpticalDepth * scatteringCoefficients);

    sun *= sunTransmittance;
    
    float3 originalColTransmittance = exp(-viewRayOpticalDepth * scatteringCoefficients);
    
    float maxAtmosphereDistance = (atmosphereRadius - planetRadiusf) * 1.5f;
    
    float viewDistance01 = saturate(rayLength / maxAtmosphereDistance);
    float atmosphereStrength = smoothstep(0.0, 1.0, viewDistance01);

    float3 finalColor =
        lerp(originalCol, originalCol * originalColTransmittance + inScatteredLight,
             atmosphereStrength);

    finalColor += sun; 
    
    //return float3(sunOpticalDepth / 100, 0.f, 0.f);
    
    return finalColor;
}

float4 PS(VertexOut pin) : SV_Target
{
    float4 originalCol = gLitScene.Load(pin.PosH.xyz);
    float2 uv = pin.PosH.xy / gRTSize;
    float3 worldPos = ComputeWorldPos(uv, gDepth);
    float3 viewDir = worldPos - gEyePosW;
    float3 rayDir = normalize(viewDir);
    float sceneDepth = length(viewDir);
    
    float3 rayOrigin = gEyePosW;
    
    float2 hitInfo = RaySphere(planetCenter, atmosphereRadius, rayOrigin, rayDir);
    float dstToAtmosphere = hitInfo.x;
    float dstThroughAtmosphere = min(hitInfo.y, sceneDepth);
    
    float4 outputColor = originalCol;
    
    if (dstThroughAtmosphere > 0)
    {
        const float epsilon = 0.1f;
        float3 pointInAtmosphere = rayOrigin + rayDir * (dstToAtmosphere + epsilon);
        float3 light = calculateLight(pointInAtmosphere, rayDir, dstThroughAtmosphere - epsilon * 2, originalCol.rgb);
        outputColor = float4(light, 1);
    }
    
    return outputColor;
}

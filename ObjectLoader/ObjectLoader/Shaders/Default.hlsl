//***************************************************************************************
// Default.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//
// Default shader, currently supports lighting.
//***************************************************************************************

// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS 1
#endif

#ifndef NUM_POINT_LIGHTS
#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
#define NUM_SPOT_LIGHTS 0
#endif

// Include structures and functions for lighting.
#include "LightingUtil.hlsl"

Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamLinearBorder : register(s4);
SamplerState gsamAnisotropicWrap : register(s5);
SamplerState gsamAnisotropicClamp : register(s6);
SamplerState gsamLinearMirror : register(s7);


// Constant data that varies per frame.
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldView;
    float4x4 gWorldInvTranspose;
    float4x4 gTexTransform;
    uint2 normalMapSize;
    bool useNormalMap;
};

// Constant data that varies per material.
cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;

    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
    // are spot lights for a maximum of MaxLights per object.
    Light gLights[MaxLights];
};

cbuffer cbMaterial : register(b2)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
};

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float4 ColorL : COLOR;
    float3 TangentL : TANGENT;
    float3 BinormalL : BINORMAL;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
    float4 ColorW : COLOR;
    float3 TangentW : TANGENT;
    float3 BiNormalW : BINORMAL;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;
	
    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    // Transform to homogeneous clip space.
    vout.PosH = mul(posW, gViewProj);
    
    vout.ColorW = vin.ColorL;
    
    vout.TexC = vin.TexC;
    
    if (useNormalMap)
    {
        vout.NormalW = normalize(mul(vin.NormalL, (float3x3) gWorldView));
        vout.TangentW = normalize(mul(vin.TangentL, (float3x3) gWorldView));
        vout.BiNormalW = normalize(mul(vin.BinormalL, (float3x3) gWorldView));
    }
    else
    {
        vout.NormalW = normalize(mul(vin.NormalL, (float3x3) gWorldInvTranspose));
    }
    
    return vout;
}


float4 PSGrid(VertexOut pin) : SV_Target
{
    return pin.ColorW;
}

float4 PS(VertexOut pin) : SV_Target
{    
    float4 diffuseAlbedo = gDiffuseMap.Sample(gsamLinearMirror, pin.TexC) * gDiffuseAlbedo;
    
    if (useNormalMap)
    {
        float3x3 tbnMat =
        {
            normalize(pin.TangentW),
            normalize(pin.BiNormalW),
            normalize(pin.NormalW)
        };
        
        pin.NormalW = gNormalMap.Load(int3(pin.TexC * normalMapSize, 0)).xyz * 2.0f - 1.0f;
        pin.NormalW = normalize(mul(pin.NormalW, tbnMat));
    }
    else
    {
        pin.NormalW = normalize(pin.NormalW);
    }
    
    // Vector from point being lit to eye. 
    float3 toEyeW = normalize(gEyePosW - pin.PosW);

    // Light terms.
    float4 ambient = gAmbientLight * diffuseAlbedo;

    const float shininess = 1.0f - gRoughness;
    Material mat = { diffuseAlbedo, gFresnelR0, shininess };
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
        pin.NormalW, toEyeW, shadowFactor);

    float4 litColor = ambient + directLight;

    // Common convention to take alpha from diffuse material.
    litColor.a = diffuseAlbedo.a;
    
    return litColor;
}


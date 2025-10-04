Texture2D gBaseColorMap : register(t0);
Texture2D gEmissiveMap : register(t1);
Texture2D gRoughnessMap : register(t2);
Texture2D gMetallicMap : register(t3);
Texture2D gNormalMap : register(t4);
Texture2D gAOMap : register(t5);
Texture2D gDisplacementMap : register(t6);
Texture2D gARMMap : register(t7);

struct Sphere
{
    float4x4 World;
    float roughness;
    float metallic;
    float2 pad;
};

StructuredBuffer<Sphere> spheres : register(t0, space1);

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
    float4x4 gWorldInvTranspose;
};

cbuffer cbPerMaterial : register(b1)
{
    int useBaseColorMap;
    int useEmissiveMap;
    int useOpacityMap;
    int useRoughnessMap;

    float3 baseColor;
    int useMetallicMap;
    
    int useNormalMap;
    int useAOMap;
    int useDisplacementMap;
    float emissiveIntensity;

    float3 emissive;
    float opacity;
    
    float roughness;
    float metallic;
    float displacementScale;
    int useARMMap;
    
    int ARMLayout;
    float3 padcpm;
}

cbuffer cbPass : register(b2)
{
    float4x4 gViewProj;
    float3 gEyePosW;
    float gDeltaTime;
    float2 gScreenSize;
};


struct VertexIn
{
    float3 PosL : POSITION;
    float4 Color : COLOR;
    float3 NormalL : NORMAL;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    int id : TEXCOORD1;
};

VertexOut GBufferVS(VertexIn vin, uint id : SV_InstanceID)
{
    VertexOut vout = (VertexOut) 0.0f;
	
    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), spheres[id].World);
    vout.PosW = posW.xyz;

    // Transform to homogeneous clip space.
    vout.PosH = mul(posW, gViewProj);
    
    vout.NormalW = normalize(mul(vin.NormalL, (float3x3) spheres[id].World));
    
    vout.id = id;
    
    return vout;
}

struct GBufferInfo
{
    float4 BaseColor : SV_Target0;
    float4 Emissive : SV_Target1;
    float4 Normal : SV_Target2;
    float4 ORM : SV_Target3;
};

GBufferInfo GBufferPS(VertexOut pin)
{
    GBufferInfo res;
    
    res.BaseColor = float4(1.f, 1.f, 1.f, 1.f);
    
    res.Normal = float4(normalize(pin.NormalW), 1);
    res.ORM.a = 1.f;
    res.ORM.r = 1.f;
    res.Emissive.a = emissiveIntensity;
    
    Sphere sphereInfo = spheres[pin.id];
    
    res.ORM.g = sphereInfo.roughness;
    res.ORM.b = sphereInfo.metallic;
    
    return res;
}

float4 WireframePS(VertexOut pin) : SV_Target
{
    return float4(0.2, 0.5, 0.2, 1);
}
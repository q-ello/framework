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

VertexIn TessVS(VertexIn vin)
{
    return vin;
}

struct PatchTess
{
    float EdgeTess[3] : SV_TessFactor;
    float InsideTess : SV_InsideTessFactor;
};

float TessEdge(float3 p0, float3 p1, float2 s0, float2 s1)
{   
    //screen area size tess factor
    const float wantedScreenEdge = 32.f;
    float edgeScreenLength = length(s0 - s1);
    
    float screenTess = edgeScreenLength / wantedScreenEdge;
    
    if (screenTess <= 1.f)
    {
        return 1.f;
    }
    
    float maxTess = 64.0f;
    
    screenTess = clamp(screenTess, 1.0f, maxTess);
    
    return screenTess;
}

PatchTess ConstantHS(InputPatch<VertexIn, 3> patch, uint patchID : SV_PrimitiveID)
{
    PatchTess pt;
    
    //default - no tesselation, for early returns
    pt.EdgeTess[0] = 1;
    pt.EdgeTess[1] = 1;
    pt.EdgeTess[2] = 1;
	
    pt.InsideTess = 1;
    
    //orientation tess factor
    float3 centerL = (patch[0].PosL + patch[1].PosL + patch[2].PosL) / 3.0f;
    float3 centerW = mul(float4(centerL, 1.0f), gWorld).xyz;
    
    float3 vecToTri = gEyePosW - centerW;
    
    float3 posW[3];
    for (int i = 0; i < 3; i++)
    {
        posW[i] = mul(float4(patch[i].PosL, 1), gWorld).xyz;
    }
    
    float3 triNormal = normalize(cross(posW[1] - posW[0], posW[2] - posW[0]));
    float orientaion = saturate(dot(triNormal, normalize(vecToTri)));
    
    //don't want to tesselate something facing away
    if (orientaion <= 0)
    {
        return pt;
    }
	
    float2 screenPts[3];
    for (int j = 0; j < 3; j++)
    {
        float4 posH = mul(float4(posW[j], 1.f), gViewProj);
        screenPts[j] = ((posH / posH.w).xy + 1.f) * gScreenSize * 0.5f;
    }
    
    float tess[3];
    tess[0] = TessEdge(posW[1], posW[2], screenPts[1], screenPts[2]);
    tess[1] = TessEdge(posW[2], posW[0], screenPts[2], screenPts[0]);
    tess[2] = TessEdge(posW[0], posW[1], screenPts[0], screenPts[1]);
    
    
    pt.EdgeTess[0] = tess[0];
    pt.EdgeTess[1] = tess[1];
    pt.EdgeTess[2] = tess[2];
	
    pt.InsideTess = (tess[0] + tess[1] + tess[2]) / 3.f;
	
    return pt;
}

[domain("tri")]
[outputtopology("triangle_cw")]
[partitioning("integer")]
[outputcontrolpoints(3)]
[patchconstantfunc("ConstantHS")]
[maxtessfactor(64.0f)]
VertexIn TessHS(InputPatch<VertexIn, 3> p,
           uint i : SV_OutputControlPointID,
           uint patchId : SV_PrimitiveID)
{
    return p[i];
}

[domain("tri")]
VertexOut TessDS(PatchTess patchTess,
             float3 uvw : SV_DomainLocation,
             const OutputPatch<VertexIn, 3> tri)
{
    VertexOut vout = (VertexOut) 0.0f;
	
	// Baricentric interpolation.
    float3 p = tri[0].PosL * uvw.x + tri[1].PosL * uvw.y + tri[2].PosL * uvw.z;
    float4 posW = mul(float4(p, 1.0f), gWorld);
    
    float3 normalL = tri[0].NormalL * uvw.x + tri[1].NormalL * uvw.y + tri[2].NormalL * uvw.z;
    
    if (useNormalMap)
    {
        vout.NormalW = normalize(mul(normalL, (float3x3) gWorld));
    }
    else
    {
        vout.NormalW = normalize(mul(normalL, (float3x3) gWorldInvTranspose));
    }
    
    vout.PosW = posW.xyz;
    vout.PosH = mul(posW, gViewProj);
    
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
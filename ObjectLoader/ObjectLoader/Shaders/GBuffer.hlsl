Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gDisplacementMap : register(t2);

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
    uint2 normalMapSize;
    uint useNormalMap;
    uint useDisplacementMap;
    float displacementScale;
    float3 pad;
};

cbuffer cbPass : register(b1)
{
    float4x4 gViewProj;
    float3 gEyePosW;
    float gDeltaTime;
    float2 gScreenSize;
};

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float3 TangentL : TANGENT;
    float3 BinormalL : BINORMAL;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
    float3 TangentW : TANGENT;
    float3 BiNormalW : BINORMAL;
};

VertexOut GBufferVS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;
	
    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    // Transform to homogeneous clip space.
    vout.PosH = mul(posW, gViewProj);
    
    vout.TexC = vin.TexC;
    
    if (useNormalMap)
    {
        vout.NormalW = normalize(mul(vin.NormalL, (float3x3) gWorld));
        vout.TangentW = normalize(mul(vin.TangentL, (float3x3) gWorld));
        vout.BiNormalW = normalize(mul(vin.BinormalL, (float3x3) gWorld));
    }
    else
    {
        vout.NormalW = normalize(mul(vin.NormalL, (float3x3) gWorldInvTranspose));
    }
    
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
    //distance from camera tess factor
    const float d0 = 10.0f;
    const float d1 = 30.0f;
    const float distWeight = 0.3f;
    
    float3 mid = 0.5f * (p0 + p1);
    float d = distance(gEyePosW, mid);
    float distTess = saturate((d1 - d) / (d1 - d0));
    distTess *= distWeight;
    
    //screen area size tess factor
    const float screenWeight = 1 - distWeight;
    
    const float wantedScreenEdge = 32.f;
    float edgeScreenLength = length(s0 - s1);
    
    float screenTess = edgeScreenLength / wantedScreenEdge;
    
    if (screenTess <= 1.f)
    {
        return 1.f;
    }
    
    float maxTess = 64.0f;
    
    screenTess = clamp(screenTess, 1.0f, maxTess);
    //screenTess *= screenWeight;
    
    //I just didn't like that
    //float tess = maxTess * distTess + screenTess;
    
    //return floor(clamp(tess, 1.0f, maxTess));
    
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
    
    vout.TexC = tri[0].TexC * uvw.x + tri[1].TexC * uvw.y + tri[2].TexC * uvw.z;
    
    if (useNormalMap)
    {
        float3 tangentL = tri[0].TangentL * uvw.x + tri[1].TangentL * uvw.y + tri[2].TangentL * uvw.z;
        float3 binormalL = tri[0].BinormalL * uvw.x + tri[1].BinormalL * uvw.y + tri[2].BinormalL * uvw.z;
        vout.NormalW = normalize(mul(normalL, (float3x3) gWorld));
        vout.TangentW = normalize(mul(tangentL, (float3x3) gWorld));
        vout.BiNormalW = normalize(mul(binormalL, (float3x3) gWorld));
    }
    else
    {
        vout.NormalW = normalize(mul(normalL, (float3x3) gWorldInvTranspose));
    }
    
    if (useDisplacementMap)
    {
	    // Displacement mapping
        posW += float4(vout.NormalW, 0) * gDisplacementMap.SampleLevel(gsamLinearMirror, vout.TexC, 0).r * displacementScale;
    }
    
    vout.PosW = posW.xyz;
    vout.PosH = mul(posW, gViewProj);
    
    return vout;
}

struct GBufferInfo
{
    float4 Diffuse : SV_Target0;
    float4 Normal : SV_Target1;
};

GBufferInfo GBufferPS(VertexOut pin)
{
    if (useNormalMap)
    {
        float3x3 tbnMat =
        {
            normalize(pin.TangentW),
            normalize(pin.BiNormalW),
            normalize(pin.NormalW)
        };
        
        pin.NormalW = gNormalMap.Load(int3(pin.TexC * normalMapSize, 0)).xyz * 2.0f - 1.0f;
        pin.NormalW = mul(pin.NormalW, tbnMat);
    }
    
    GBufferInfo res;
    
    res.Diffuse = gDiffuseMap.Sample(gsamLinearMirror, pin.TexC);
    res.Normal = float4(normalize(pin.NormalW), 1);
    
    return res;
}

float4 WireframePS(VertexOut pin) : SV_Target
{
    return float4(0.2, 0.5, 0.2, 1);
}
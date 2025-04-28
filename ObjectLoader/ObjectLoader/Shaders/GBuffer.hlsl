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
    float4x4 gWorldInvTranspose;
    uint2 normalMapSize;
    bool useNormalMap;
};

cbuffer cbPass : register(b1)
{
    float4x4 gViewProj;
    float gDeltaTime;
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

VertexOut GBufferVS(VertexIn vin)
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
    res.Normal = float4(normalize(pin.NormalW), 0);
    
    return res;
}

GBufferInfo GBufferGridPS(VertexOut pin)
{
    GBufferInfo res;
    res.Diffuse = float4(pin.ColorW);
    res.Normal = float4(0, 1, 0, 0);
    
    return res;
}
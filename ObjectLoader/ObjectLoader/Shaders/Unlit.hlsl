cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
}

cbuffer cbPass : register(b1)
{
    float4x4 gViewProj;
    float gDeltaTime;
};

struct VertexIn
{
    float3 PosL : POSITION;
    float4 ColorL : COLOR;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float4 ColorW : COLOR;
};

VertexOut UnlitVS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;
	
    // Transform to homogeneous clip space.
    float4 posW = mul(float4(vin.PosL, 1), gWorld);
    vout.PosH = mul(posW, gViewProj);

    vout.ColorW = vin.ColorL;
    
    return vout;
}

float4 UnlitPS(VertexOut pin) : SV_Target
{
    return pin.ColorW;
}
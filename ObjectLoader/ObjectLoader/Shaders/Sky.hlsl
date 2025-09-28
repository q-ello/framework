TextureCube gSky : register(t0);

cbuffer cbPass : register(b0)
{
    float4x4 gViewProj;
    float3 gEyePosW;
    float gDeltaTime;
    float2 gScreenSize;
};

cbuffer cbSky : register(b1)
{
    float4x4 gWorld;
};

struct VertexIn
{
    float3 PosL : POSITION;
};

struct VertexOut
{
    float3 PosL : POSITION;
    float4 PosH : SV_POSITION;
};

SamplerState gsamLinear : register(s0);

VertexOut VS(VertexIn vin)
{
    VertexOut vout;

	// Use local vertex position as cubemap lookup vector.
    vout.PosL = vin.PosL;
	
	// Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);

	// Always center sky about camera.
    posW.xyz += gEyePosW;

	// Set z = w so that z/w = 1 (i.e., skydome always on far plane).
    vout.PosH = mul(posW, gViewProj).xyww;
	
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    return gSky.Sample(gsamLinear, pin.PosL);
}


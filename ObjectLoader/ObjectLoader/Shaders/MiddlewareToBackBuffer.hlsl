struct VertexOut
{
    float4 PosH : SV_POSITION;
};

Texture2D gMiddlewareTexture : register(t0);

VertexOut VS(uint id : SV_VertexID)
{
    VertexOut vout;
	
    float2 positions[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
    
    vout.PosH = float4(positions[id], 0, 1);
    
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    return gMiddlewareTexture.Load(pin.PosH.xyz);
}
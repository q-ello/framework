struct VertexIn
{
    float3 position : POSITION;
};

struct VertexOut
{
    float4 posH : SV_POSITION;
    float3 posW : POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
};

cbuffer TerrainConstants : register (b0)
{
    int2 heightmapSize;
    float maxHeight;
    int gridSize;
	float slopeThreshold;
	float heightThreshold;
	float2 pad;
}

cbuffer cbPass : register(b1)
{
    float4x4 gViewProj;
    float3 gEyePosW;
    float gDeltaTime;
    float2 gScreenSize;
    float2 pad2;
};

cbuffer cbTerrainTextures : register(b2)
{
    int UseLowTexture;
    float3 LowColor;
    int UseSlopeTexture;
    float3 SlopeColor;
    int UseHighTexture;
    float3 HighColor;
};

struct Grid
{
    float4x4 world;
    int texelXStart;
    int texelYStart;
    int texelStride;
    int pad;
};

StructuredBuffer<Grid> grids : register(t0);

Texture2D heightmap : register(t0, space1);
Texture2D lowDiffuseMap : register(t1, space1);
Texture2D slopeDiffuseMap : register(t2, space1);
Texture2D highDiffuseMap : register(t3, space1);

SamplerComparisonState gsamShadow : register(s0);
SamplerState gsamLinear : register(s1);
SamplerState gsamLinearWrap : register(s2);

float3 ComputeNormal(float2 uv)
{
    float2 texelSize = 1.0 / (float2) heightmapSize;

    float heightL = heightmap.SampleLevel(gsamLinear, uv - float2(texelSize.x, 0), 0).r;
    float heightR = heightmap.SampleLevel(gsamLinear, uv + float2(texelSize.x, 0), 0).r;
    float heightD = heightmap.SampleLevel(gsamLinear, uv - float2(0, texelSize.y), 0).r;
    float heightU = heightmap.SampleLevel(gsamLinear, uv + float2(0, texelSize.y), 0).r;

    float3 tangent = float3(1, (heightR - heightL) * maxHeight, 0);
    float3 bitangent = float3(0, (heightU - heightD) * maxHeight, 1);

    float3 normal = normalize(cross(bitangent, tangent));
    return normal;
}

VertexOut VS(VertexIn vin, uint instanceID : SV_InstanceID, uint vertexID : SV_VertexID)
{
    VertexOut vout;
    
    Grid grid = grids[instanceID];
    vout.posW = mul(float4(vin.position, 1.0), grid.world).xyz;
    
    int texelX = grid.texelXStart + (vertexID % gridSize) * grid.texelStride;
    int texelY = grid.texelYStart + (vertexID / gridSize) * grid.texelStride;
    
    uint planeGridCount = gridSize * gridSize;
    
    uint edge;
    
    if (vertexID >= planeGridCount)
    {
        edge = (vertexID - gridSize * gridSize) / gridSize;
        switch (edge)
        {
            //first top
            case 0:
                texelY = grid.texelYStart;
                vout.normal = float3(0, 0, 1);
                break;
            //first bottom
            case 1:
                texelY = grid.texelYStart;
                vout.normal = float3(0, 0, 1);
                break;
            //second top
            case 2:
                texelY = grid.texelYStart + (gridSize - 1) * grid.texelStride;
                vout.normal = float3(0, 0, -1);
                break;
            //second bottom
            case 3:
                texelY = grid.texelYStart + (gridSize - 1) * grid.texelStride;
                vout.normal = float3(0, 0, -1);
                break;
            //third top
            case 4:
                texelX = grid.texelXStart;
                texelY = grid.texelYStart + (vertexID % gridSize) * grid.texelStride;
                vout.normal = float3(-1, 0, 0);
                break;
            //third bottom
            case 5:
                texelX = grid.texelXStart;
                texelY = grid.texelYStart + (vertexID % gridSize) * grid.texelStride;
                vout.normal = float3(-1, 0, 0);
                break;
            //fourth top
            case 6:
                texelX = grid.texelXStart + (gridSize - 1) * grid.texelStride;
                texelY = grid.texelYStart + (vertexID % gridSize) * grid.texelStride;
                vout.normal = float3(1, 0, 0);
                break;
            //forth bottom
            default:
                texelX = grid.texelXStart + (gridSize - 1) * grid.texelStride;
                texelY = grid.texelYStart + (vertexID % gridSize) * grid.texelStride;
                vout.normal = float3(1, 0, 0);
                break;
        }
    }
    
    float yOffset = maxHeight * heightmap.Load(int3(texelX, texelY, 0)).r;
    
    if (vertexID < planeGridCount || edge % 2 == 0)
    {
        vout.posW.y += yOffset;
    }
    
    vout.uv = float2(texelX, texelY) / heightmapSize;
    
    if (vertexID < planeGridCount)
        vout.normal = ComputeNormal(vout.uv);
    vout.posH = mul(float4(vout.posW, 1.0), gViewProj);
    
    return vout;
}

struct GBufferInfo
{
    float4 BaseColor : SV_Target0;
    float4 Emissive : SV_Target1;
    float4 Normal : SV_Target2;
    float4 ORM : SV_Target3;
    float4 TexCoord : SV_Target4;
};

GBufferInfo PS(VertexOut pin)
{
    GBufferInfo gbuffer;

	float3 normal = normalize(pin.normal);
	float slope = 1 - dot(normal, float3(0.f, 1.f, 0.f));
	float slopeFactor = smoothstep(0.f, slopeThreshold, slope);

	float height = heightmap.Sample(gsamLinear, pin.uv);
	float heightFactor = smoothstep(heightThreshold - 0.1f, heightThreshold + 0.1f, height);	

    float3 lowDiffuseColor = UseLowTexture == 1 ? lowDiffuseMap.Sample(gsamLinear, pin.uv) : LowColor;
    float3 slopeDiffuseColor = UseSlopeTexture == 1 ? slopeDiffuseMap.Sample(gsamLinear, pin.uv) : SlopeColor;
    float3 highDiffuseColor = UseHighTexture == 1 ? highDiffuseMap.Sample(gsamLinear, pin.uv) : HighColor;
	float3 heightColor = lerp(lowDiffuseColor, highDiffuseColor, heightFactor);
	float3 finalColor = lerp(heightColor, slopeDiffuseColor, slopeFactor);

    gbuffer.BaseColor = float4(finalColor, 1.f);

    gbuffer.Emissive = float4(0, 0, 0, 0);
    gbuffer.Normal = float4(normalize(pin.normal), 1.0f);
    gbuffer.ORM = float4(0.2, 1.0, 0.0, 0);
    gbuffer.TexCoord = float4(pin.uv, 0, 0);
    return gbuffer;
}
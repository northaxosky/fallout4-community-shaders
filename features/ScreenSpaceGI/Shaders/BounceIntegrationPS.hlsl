cbuffer BounceIntegrationCB : register(b0)
{
    uint2 gTargetExtent;
    uint2 gSourceExtent;
};

Texture2D<float4> gBounce : register(t0);

struct VSOutput
{
    float4 position : SV_Position;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    const float2 corner = float2((vertexID << 1) & 2, vertexID & 2);
    VSOutput output;
    output.position = float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    const uint2 targetPixel = min(uint2(input.position.xy), gTargetExtent - 1);
    const uint2 sourcePixel = min(
        targetPixel * gSourceExtent / gTargetExtent,
        gSourceExtent - 1);

    // The stock ambient pass multiplies AmbientDiffuseA + AmbientDiffuseB by three.
    return float4(gBounce.Load(int3(sourcePixel, 0)).rgb / 3.0, 0.0);
}

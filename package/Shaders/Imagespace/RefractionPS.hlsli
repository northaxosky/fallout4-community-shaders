struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

cbuffer RefractionParameters : register(b2)
{
    float4 TintAndAmount;
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);
Texture2D<float4> Refraction : register(t1);
SamplerState RefractionSampler : register(s1);

float4 main(PS_INPUT input) : SV_Target
{
    float4 refraction = Refraction.Sample(RefractionSampler, input.TexCoord);
    float scale = refraction.z * 0.125;
    refraction.xy -= 0.5;
    refraction.y = dot(float2(scale, scale), refraction.yy);
    refraction.x = dot(float2(scale, scale), refraction.xx);
    float2 coordinate = float2(
        input.TexCoord.x - refraction.x,
        input.TexCoord.y + refraction.y);

    float2 high = (coordinate - 0.85) * 0.78 + 0.85;
    float2 bounded = float2(
        coordinate.x > 0.85 ? high.x : coordinate.x,
        coordinate.y > 0.85 ? high.y : coordinate.y);
    float2 low = 0.15 - (0.15 - coordinate) * 0.78;
    bounded = float2(
        coordinate.x < 0.15 ? low.x : bounded.x,
        coordinate.y < 0.15 ? low.y : bounded.y);
    coordinate = lerp(coordinate, bounded, refraction.z);

    float4 refracted = Image.Sample(ImageSampler, coordinate);
    float mask = Refraction.Sample(RefractionSampler, coordinate).w;
    bool active = (float4(0.0, 0.0, 0.0, 0.0) != mask.xxxx).x;
    float luma = dot(float3(0.299, 0.587, 0.114), refracted.rgb);
    float3 tint = luma * TintAndAmount.w * TintAndAmount.rgb;
    float4 original = Image.Sample(ImageSampler, input.TexCoord);
    float4 selected = refracted * (active ? 1.0 : 0.0);
    selected = original * (active ? 0.0 : 1.0) + selected;
    float3 colored = (1.0 - TintAndAmount.w) * selected.rgb + tint;
    bool tintGate = refraction.w > 0.8 && refraction.w < 1.0;
    return float4(tintGate ? colored : selected.rgb, selected.w);
}

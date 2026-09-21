#if defined(IMAGESPACE_HUD_GLASS_DROPSHADOW)

struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float3 TexCoord : TEXCOORD0;
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);

float4 main(PS_INPUT input) : SV_Target
{
	float4 image = Image.Sample(ImageSampler, input.TexCoord.xy);
	float4 shadow = 0.0;
	[loop] for (int offset = -2; offset <= 2; ++offset)
	{
		shadow += Image.Sample(ImageSampler, input.TexCoord.xy + float2(-0.0005, -0.001) + offset * 0.0001);
	}
	float3 color = image.rgb + shadow.rgb * 0.02;
	float alpha = saturate(image.a + shadow.a * 2.5) * input.TexCoord.z;
	return float4(color, alpha);
}

#elif defined(IMAGESPACE_HUD_GLASS_BLUR_Y) || defined(IMAGESPACE_HUD_GLASS_BLUR_X)

struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float3 TexCoord : TEXCOORD0;
};

cbuffer BlurParameters : register(b2)
{
	float4 BlurStep;
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);

float4 main(PS_INPUT input) : SV_Target
{
	float4 sum = 0.0;
	[loop] for (int offset = -2; offset <= 2; ++offset)
	{
#if defined(IMAGESPACE_HUD_GLASS_BLUR_Y)
		sum += Image.Sample(ImageSampler, input.TexCoord.xy + float2(0.0, offset * BlurStep.y));
#else
		sum += Image.Sample(ImageSampler, input.TexCoord.xy + float2(offset * BlurStep.x, 0.0));
#endif
	}
	return sum * 0.2;
}

#elif defined(IMAGESPACE_HUD_GLASS_BASE)

struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float3 TexCoord : TEXCOORD0;
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);
Texture2D<float4> Glass : register(t1);
SamplerState GlassSampler : register(s1);

float4 main(PS_INPUT input) : SV_Target
{
	float4 glass = Glass.Sample(GlassSampler, input.TexCoord.xy);
	glass.a = saturate(glass.a * 0.9);
	float4 adjusted = float4(glass.rgb * 2.0, glass.a * 0.5);
	float4 image = Image.Sample(ImageSampler, input.TexCoord.xy);
	return lerp(adjusted, image, image.a);
}

#elif defined(IMAGESPACE_HUD_GLASS_CLEAR)

struct PS_INPUT
{
	float4 Position : SV_POSITION;
};

float4 main(PS_INPUT input) : SV_Target
{
	return 0.0;
}

#elif defined(IMAGESPACE_HUD_GLASS_COPY)

struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float3 TexCoord : TEXCOORD0;
};

cbuffer CopyParameters : register(b2)
{
	float4 CopyColor;
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);

float4 main(PS_INPUT input) : SV_Target
{
	return Image.Sample(ImageSampler, input.TexCoord.xy) * CopyColor;
}

#else
#error "define an Imagespace HUD Glass producer macro"
#endif

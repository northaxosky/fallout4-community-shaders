#if defined(LUM) && defined(RGB2LUM)
#error "LUM and RGB2LUM are mutually exclusive"
#endif

#if defined(LUM)

struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
};

cbuffer HDRParameters : register(b2)
{
	float4 Parameters[24];
};

Texture2D<float> Image : register(t0);
SamplerState ImageSampler : register(s0);

float4 main(PS_INPUT input) : SV_Target
{
	float3 result = float3(0.0, 9999999.0, -9999999.0);

	[loop] for (int index = 0; index < 16; ++index)
	{
		float2 coordinate =
			Parameters[index + 8].xy * Parameters[7].xy + input.TexCoord;
		float sample = Image.Sample(ImageSampler, coordinate);
		bool infinity =
			(asuint(sample) & 0x7f800000) == 0x7f800000;
		float weighted = sample * Parameters[index + 8].z;
		weighted = infinity ? 0.0 : weighted;
		result = result.x + weighted;
	}

	return float4(result, Parameters[7].z);
}

#elif defined(RGB2LUM)

struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
};

cbuffer HDRParameters : register(b2)
{
	float4 Base[8];
	float4 SampleOffsets[5];
};

Texture2D<float3> Image : register(t0);
SamplerState ImageSampler : register(s0);

float4 main(PS_INPUT input) : SV_Target
{
	float3 result = float3(0.0, 9999999.0, -9999999.0);

	[loop] for (int index = 0; index < 4; ++index)
	{
		float2 coordinate =
			SampleOffsets[index].xy * Base[7].xy + input.TexCoord;
		float3 sample = Image.Sample(ImageSampler, coordinate);
		bool3 infinity =
			(asuint(sample) & 0x7f800000) == 0x7f800000;
		sample = infinity ? 0.0 : sample;
		float luminance = dot(
			float3(
				asfloat(0x3e59999a),
				asfloat(0x3f372474),
				asfloat(0x3d93a92a)),
			sample);
		result = mad(luminance, SampleOffsets[index].z, result.x);
	}

	return float4(result, Base[7].z);
}

#else

struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
};

cbuffer HDRParameters : register(b2)
{
	float4 Parameters[24];
};

Texture2D<float> Image : register(t0);
Texture2D<float> LightAdapt : register(t1);
SamplerState ImageSampler : register(s0);
SamplerState LightAdaptSampler : register(s1);

float4 main(PS_INPUT input) : SV_Target
{
	float4 result = float4(0.0, 9999999.0, -9999999.0, 0.0);

	[loop] for (int index = 0; index < 16; ++index)
	{
		float2 coordinate =
			Parameters[index + 8].xy * Parameters[7].xy + input.TexCoord;
		float sample = Image.Sample(ImageSampler, coordinate);
		bool infinity =
			(asuint(sample) & 0x7f800000) == 0x7f800000;
		float weighted = sample * Parameters[index + 8].z;
		weighted = infinity ? 0.0 : weighted;
		result.w += weighted;
		result.yz = result.ww;
	}

	result.x = LightAdapt.Sample(LightAdaptSampler, input.TexCoord);
	bool invalid = result.w != result.w;
	float adapted =
		(result.w - result.x) * Parameters[1].z + result.x;
	float3 color = invalid ? result.xyz : adapted.xxx;
	return float4(color, Parameters[7].z);
}

#endif

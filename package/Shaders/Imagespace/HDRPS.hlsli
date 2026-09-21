#if (defined(DOWNSAMPLE) && (defined(LUM) || defined(RGB2LUM) || defined(BLEND))) || \
	(defined(LUM) && (defined(RGB2LUM) || defined(BLEND))) || \
	(defined(RGB2LUM) && defined(BLEND))
#error "HDR downsample selectors are mutually exclusive"
#endif

#if defined(BLEND)

struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
};

cbuffer HDRParameters : register(b2)
{
	float4 Parameters[5];
};

Texture2D<float4> Scene : register(t0);
Texture2D<float4> Exposure : register(t1);
Texture2D<float4> Adaptation : register(t2);
Texture2D<float4> Sentinel : register(t3);
SamplerState SceneSampler : register(s0);
SamplerState ExposureSampler : register(s1);
SamplerState AdaptationSampler : register(s2);
SamplerState SentinelSampler : register(s3);

float4 main(PS_INPUT input) : SV_Target
{
	float3 scene = Scene.Sample(SceneSampler, input.TexCoord).xyz;
	float marker =
		Sentinel.Sample(SentinelSampler, input.TexCoord).w * 255.0 - 4.0;
	if (abs(marker) < 0.25)
	{
		return float4(scene, 1.0);
	}

	float3 exposure =
		Exposure.Sample(ExposureSampler, input.TexCoord * Parameters[4].xy).xyz;
	float adaptation =
		Adaptation.Sample(AdaptationSampler, input.TexCoord).x;
	float scale = Parameters[1].z / (adaptation + 0.001);
	if (scale < Parameters[1].y) scale = Parameters[1].y;
	if (Parameters[1].x < scale) scale = Parameters[1].x;
	float3 color = (scene + exposure) * scale;
	float3 twiceColor = color + color;
	float3 numerator = mad(color, 0.3, 0.05);
	float2 cinematicCarrier =
		Parameters[1].w * float2(0.2, asfloat(0x40555555));
	numerator = mad(twiceColor, numerator, cinematicCarrier.x);
	float3 denominator = mad(
		twiceColor,
		mad(color, 0.3, 0.5),
		asfloat(0x3d75c290));
	color = numerator / denominator - cinematicCarrier.y;
	float inverseCurve = 1.0 / mad(
		mad(Parameters[1].w, 0.2, 19.375999),
		asfloat(0x3d2758fd),
		-cinematicCarrier.y);
	color *= inverseCurve;

	float luminance = dot(color, float3(0.2125, 0.7154, 0.0721));
	float4 controls = float4(color, 0.0) - luminance.xxxx;
	controls = mad(Parameters[2].x, controls, luminance.xxxx);
	float4 target = mad(luminance.xxxx, Parameters[3], -controls);
	controls = mad(Parameters[3].w, target, controls);
	controls = mad(Parameters[2].w, controls, -adaptation.xxxx);
	return mad(Parameters[2].z, controls, adaptation.xxxx);
}

#elif defined(DOWNSAMPLE)

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
	float4 result = 0.0;

	[loop] for (int index = 0; index < 4; ++index)
	{
		float2 coordinate =
			SampleOffsets[index].xy * Base[7].xy + input.TexCoord;
		coordinate *= Base[4].xy;
		float3 sample = Image.Sample(ImageSampler, coordinate);
		bool3 infinity =
			(asuint(sample) & 0x7f800000) == 0x7f800000;
		[flatten] if (infinity.x) sample.x = 0.0;
		[flatten] if (infinity.y) sample.y = 0.0;
		[flatten] if (infinity.z) sample.z = 0.0;
		result.xyz = mad(sample, SampleOffsets[index].z, result.xyz);
	}

	return float4(result.xyz, Base[7].z);
}

#elif defined(LUM)

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

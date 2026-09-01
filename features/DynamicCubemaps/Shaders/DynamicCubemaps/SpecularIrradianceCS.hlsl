static const float PI = 3.14159265358979323846;
static const float TAU = 6.28318530717958647692;
static const float Epsilon = 0.00001;
static const uint NumSamples = 16;
static const float InvNumSamples = 1.0 / float(NumSamples);

cbuffer SpecularMapFilterSettings : register(b0)
{
	float roughness;
}

TextureCube inputTexture : register(t0);
RWTexture2DArray<float4> outputTexture : register(u0);
SamplerState linearWrapSampler : register(s0);

float3 IrradianceToLinear(float3 color)
{
	return pow(abs(color), 1.6);
}

float3 LinearToIrradiance(float3 color)
{
	return pow(abs(color), 1.0 / 1.6);
}

float RadicalInverseVdC(uint bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) |
		((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) |
		((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) |
		((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) |
		((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10;
}

float2 SampleHammersley(uint index)
{
	return float2(index * InvNumSamples, RadicalInverseVdC(index));
}

float3 SampleGGX(float u1, float u2, float sampleRoughness)
{
	float alpha = sampleRoughness * sampleRoughness;
	float cosTheta = sqrt(
		(1.0 - u2) / (1.0 + (alpha * alpha - 1.0) * u2));
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	float phi = TAU * u1;
	return float3(
		sinTheta * cos(phi),
		sinTheta * sin(phi),
		cosTheta);
}

float NdfGGX(float cosLh, float sampleRoughness)
{
	float alpha = sampleRoughness * sampleRoughness;
	float alphaSquared = alpha * alpha;
	float denominator =
		cosLh * cosLh * (alphaSquared - 1.0) + 1.0;
	return alphaSquared / (PI * denominator * denominator);
}

float3 GetSamplingVector(uint3 threadID)
{
	uint width;
	uint height;
	uint depth;
	outputTexture.GetDimensions(width, height, depth);
	float2 st = (float2(threadID.xy) + 0.5) / float2(width, height);
	float2 uv = 2.0 * float2(st.x, 1.0 - st.y) - 1.0;
	float3 result = 0.0;
	switch (threadID.z)
	{
	case 0: result = float3(1.0, uv.y, -uv.x); break;
	case 1: result = float3(-1.0, uv.y, uv.x); break;
	case 2: result = float3(uv.x, 1.0, -uv.y); break;
	case 3: result = float3(uv.x, -1.0, uv.y); break;
	case 4: result = float3(uv.x, uv.y, 1.0); break;
	case 5: result = float3(-uv.x, uv.y, -1.0); break;
	}
	return normalize(result);
}

void ComputeBasis(float3 normal, out float3 tangent, out float3 bitangent)
{
	bitangent = cross(normal, float3(0.0, 1.0, 0.0));
	bitangent = lerp(
		cross(normal, float3(1.0, 0.0, 0.0)),
		bitangent,
		step(Epsilon, dot(bitangent, bitangent)));
	bitangent = normalize(bitangent);
	tangent = normalize(cross(normal, bitangent));
}

float3 TangentToWorld(
	float3 value,
	float3 normal,
	float3 tangent,
	float3 bitangent)
{
	return tangent * value.x +
		bitangent * value.y +
		normal * value.z;
}

[numthreads(8, 8, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
	uint outputWidth;
	uint outputHeight;
	uint outputDepth;
	outputTexture.GetDimensions(outputWidth, outputHeight, outputDepth);
	if (threadID.x >= outputWidth || threadID.y >= outputHeight)
		return;

	uint inputWidth;
	uint inputHeight;
	uint inputLevels;
	inputTexture.GetDimensions(0, inputWidth, inputHeight, inputLevels);
	float texelSolidAngle =
		4.0 * PI / (6.0 * inputWidth * inputHeight);
	float3 normal = GetSamplingVector(threadID);
	float3 outgoing = normal;
	float3 tangent;
	float3 bitangent;
	ComputeBasis(normal, tangent, bitangent);

	float3 color = 0.0;
	float weight = 0.0;
	for (uint index = 0; index < NumSamples; ++index)
	{
		float2 samplePoint = SampleHammersley(index);
		float3 halfVector = TangentToWorld(
			SampleGGX(samplePoint.x, samplePoint.y, roughness),
			normal,
			tangent,
			bitangent);
		float3 incoming =
			2.0 * dot(outgoing, halfVector) * halfVector - outgoing;
		float cosIncoming = dot(normal, incoming);
		if (cosIncoming > 0.0)
		{
			float cosHalf = max(dot(normal, halfVector), 0.0);
			float pdf = NdfGGX(cosHalf, roughness) * 0.25;
			float sampleSolidAngle = 1.0 / (NumSamples * pdf);
			float mipLevel = max(
				0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0,
				0.0);
			color += IrradianceToLinear(
				inputTexture.SampleLevel(
					linearWrapSampler, incoming, mipLevel).rgb) *
				cosIncoming;
			weight += cosIncoming;
		}
	}
	color /= max(weight, Epsilon);
	outputTexture[threadID] =
		float4(LinearToIrradiance(color), 1.0);
}

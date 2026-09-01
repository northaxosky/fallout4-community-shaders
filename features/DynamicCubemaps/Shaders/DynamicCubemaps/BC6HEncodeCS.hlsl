// SPDX-License-Identifier: MIT
// Copyright (c) 2015 Krzysztof Narkowicz

#pragma warning(disable: 3078)

#define QUALITY 0
#define ENCODE_P2 0
#define INSET_COLOR_BBOX 1
#define OPTIMIZE_ENDPOINTS 1
#define LUMINANCE_WEIGHTS 1

static const float HALF_MAX = 65504.0f;

Texture2DArray<float4> SrcTexture : register(t0);
RWTexture2DArray<uint4> OutputTexture : register(u0);

cbuffer BC6HEncodeCB : register(b0)
{
	uint2 TextureSizeInBlocks;
	uint MipLevel;
	uint pad;
};

float CalcMSLE(float3 a, float3 b)
{
	float3 delta = log2((b + 1.0f) / (a + 1.0f));
	float3 deltaSq = delta * delta;

#if LUMINANCE_WEIGHTS
	deltaSq *= float3(0.299f, 0.587f, 0.114f);
#endif

	return deltaSq.x + deltaSq.y + deltaSq.z;
}

float3 Quantize10(float3 x)
{
	return (f32tof16(x) * 1024.0f) / (0x7bff + 1.0f);
}

float3 Unquantize10(float3 x)
{
	return (x * 65536.0f + 0x8000) / 1024.0f;
}

float3 FinishUnquantize(
	float3 endpoint0Unq,
	float3 endpoint1Unq,
	float weight)
{
	float3 comp =
		(endpoint0Unq * (64.0f - weight) +
		 endpoint1Unq * weight +
		 32.0f) *
		(31.0f / 4096.0f);
	return f16tof32(uint3(comp));
}

void Swap(inout float3 a, inout float3 b)
{
	float3 value = a;
	a = b;
	b = value;
}

void Swap(inout float a, inout float b)
{
	float value = a;
	a = b;
	b = value;
}

uint ComputeIndex4(
	float texelPosition,
	float endpoint0Position,
	float endpoint1Position)
{
	float ratio =
		(texelPosition - endpoint0Position) /
		(endpoint1Position - endpoint0Position);
	return (uint)clamp(
		ratio * 14.93333f + 0.03333f + 0.5f,
		0.0f,
		15.0f);
}

void InsetColorBBoxP1(
	float3 texels[16],
	inout float3 blockMin,
	inout float3 blockMax)
{
	float3 refinedBlockMin = blockMax;
	float3 refinedBlockMax = blockMin;

	for (uint i = 0; i < 16; ++i) {
		refinedBlockMin = min(
			refinedBlockMin,
			texels[i] == blockMin ? refinedBlockMin : texels[i]);
		refinedBlockMax = max(
			refinedBlockMax,
			texels[i] == blockMax ? refinedBlockMax : texels[i]);
	}

	float3 logRefinedBlockMax = log2(refinedBlockMax + 1.0f);
	float3 logRefinedBlockMin = log2(refinedBlockMin + 1.0f);
	float3 logBlockMax = log2(blockMax + 1.0f);
	float3 logBlockMin = log2(blockMin + 1.0f);
	float3 logBlockMaxExtension =
		(logBlockMax - logBlockMin) * (1.0f / 32.0f);

	logBlockMin += min(
		logRefinedBlockMin - logBlockMin,
		logBlockMaxExtension);
	logBlockMax -= min(
		logBlockMax - logRefinedBlockMax,
		logBlockMaxExtension);

	blockMin = exp2(logBlockMin) - 1.0f;
	blockMax = exp2(logBlockMax) - 1.0f;
}

void OptimizeEndpointsP1(
	float3 texels[16],
	inout float3 blockMin,
	inout float3 blockMax,
	float3 blockMinNonInset,
	float3 blockMaxNonInset)
{
	float3 blockDirection = blockMax - blockMin;
	blockDirection /=
		blockDirection.x + blockDirection.y + blockDirection.z;

	float endpoint0Position = f32tof16(dot(blockMin, blockDirection));
	float endpoint1Position = f32tof16(dot(blockMax, blockDirection));

	float3 alphaTexelSum = 0.0f;
	float3 betaTexelSum = 0.0f;
	float alphaBetaSum = 0.0f;
	float alphaSquaredSum = 0.0f;
	float betaSquaredSum = 0.0f;

	for (int i = 0; i < 16; ++i) {
		float texelPosition = f32tof16(dot(texels[i], blockDirection));
		uint texelIndex = ComputeIndex4(
			texelPosition,
			endpoint0Position,
			endpoint1Position);
		float beta = saturate(texelIndex / 15.0f);
		float alpha = 1.0f - beta;
		float3 texelF16 = f32tof16(texels[i]);
		alphaTexelSum += alpha * texelF16;
		betaTexelSum += beta * texelF16;
		alphaBetaSum += alpha * beta;
		alphaSquaredSum += alpha * alpha;
		betaSquaredSum += beta * beta;
	}

	float determinant =
		alphaSquaredSum * betaSquaredSum -
		alphaBetaSum * alphaBetaSum;
	if (abs(determinant) > 0.00001f) {
		float determinantRcp = rcp(determinant);
		blockMin = clamp(
			f16tof32(clamp(
				determinantRcp *
					(alphaTexelSum * betaSquaredSum -
					 betaTexelSum * alphaBetaSum),
				0.0f,
				HALF_MAX)),
			blockMinNonInset,
			blockMaxNonInset);
		blockMax = clamp(
			f16tof32(clamp(
				determinantRcp *
					(betaTexelSum * alphaSquaredSum -
					 alphaTexelSum * alphaBetaSum),
				0.0f,
				HALF_MAX)),
			blockMinNonInset,
			blockMaxNonInset);
	}
}

void EncodeP1(
	inout uint4 block,
	inout float blockMSLE,
	float3 texels[16])
{
	float3 blockMin = texels[0];
	float3 blockMax = texels[0];
	for (uint i = 1; i < 16; ++i) {
		blockMin = min(blockMin, texels[i]);
		blockMax = max(blockMax, texels[i]);
	}

	float3 blockMinNonInset = blockMin;
	float3 blockMaxNonInset = blockMax;
#if INSET_COLOR_BBOX
	InsetColorBBoxP1(texels, blockMin, blockMax);
#endif
#if OPTIMIZE_ENDPOINTS
	OptimizeEndpointsP1(
		texels,
		blockMin,
		blockMax,
		blockMinNonInset,
		blockMaxNonInset);
#endif

	float3 blockDirection = blockMax - blockMin;
	blockDirection /=
		blockDirection.x + blockDirection.y + blockDirection.z;

	float3 endpoint0 = Quantize10(blockMin);
	float3 endpoint1 = Quantize10(blockMax);
	float endpoint0Position = f32tof16(dot(blockMin, blockDirection));
	float endpoint1Position = f32tof16(dot(blockMax, blockDirection));

	float fixupTexelPosition =
		f32tof16(dot(texels[0], blockDirection));
	uint fixupIndex = ComputeIndex4(
		fixupTexelPosition,
		endpoint0Position,
		endpoint1Position);
	if (fixupIndex > 7) {
		Swap(endpoint0Position, endpoint1Position);
		Swap(endpoint0, endpoint1);
	}

	uint indices[16] = {
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0
	};
	for (uint i = 0; i < 16; ++i) {
		float texelPosition = f32tof16(dot(texels[i], blockDirection));
		indices[i] = ComputeIndex4(
			texelPosition,
			endpoint0Position,
			endpoint1Position);
	}

	float3 endpoint0Unquantized = Unquantize10(endpoint0);
	float3 endpoint1Unquantized = Unquantize10(endpoint1);
	float msle = 0.0f;
	for (uint i = 0; i < 16; ++i) {
		float weight = floor(
			(indices[i] * 64.0f) / 15.0f + 0.5f);
		float3 texelUncompressed = FinishUnquantize(
			endpoint0Unquantized,
			endpoint1Unquantized,
			weight);
		msle += CalcMSLE(texels[i], texelUncompressed);
	}

	blockMSLE = msle;
	block.x = 0x03;
	block.x |= (uint)endpoint0.x << 5;
	block.x |= (uint)endpoint0.y << 15;
	block.x |= (uint)endpoint0.z << 25;
	block.y |= (uint)endpoint0.z >> 7;
	block.y |= (uint)endpoint1.x << 3;
	block.y |= (uint)endpoint1.y << 13;
	block.y |= (uint)endpoint1.z << 23;
	block.z |= (uint)endpoint1.z >> 9;
	block.z |= indices[0] << 1;
	block.z |= indices[1] << 4;
	block.z |= indices[2] << 8;
	block.z |= indices[3] << 12;
	block.z |= indices[4] << 16;
	block.z |= indices[5] << 20;
	block.z |= indices[6] << 24;
	block.z |= indices[7] << 28;
	block.w |= indices[8] << 0;
	block.w |= indices[9] << 4;
	block.w |= indices[10] << 8;
	block.w |= indices[11] << 12;
	block.w |= indices[12] << 16;
	block.w |= indices[13] << 20;
	block.w |= indices[14] << 24;
	block.w |= indices[15] << 28;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	uint2 blockCoordinate = dispatchThreadID.xy;
	uint faceIndex = dispatchThreadID.z;
	if (all(blockCoordinate < TextureSizeInBlocks)) {
		int2 texelBase = int2(blockCoordinate) * 4;
		float3 texels[16];
		[unroll]
		for (int i = 0; i < 16; ++i) {
			int x = i % 4;
			int y = i / 4;
			texels[i] = SrcTexture.Load(int4(
				texelBase.x + x,
				texelBase.y + y,
				int(faceIndex),
				int(MipLevel))).rgb;
		}

		uint4 block = 0;
		float blockMSLE = 0.0f;
		EncodeP1(block, blockMSLE, texels);
		OutputTexture[uint3(blockCoordinate, faceIndex)] = block;
	}
}

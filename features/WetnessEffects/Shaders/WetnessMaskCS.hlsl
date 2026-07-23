cbuffer WetnessCB : register(b0)
{
	uint2 gExtent;
	float gWetnessScalar;
	float _padding;
	float4 gViewToWorld[3];
};

Texture2D<float2> gNormal : register(t0);
RWTexture2D<float> gWetnessMask : register(u0);

float3 DecodeViewNormal(float2 enc)
{
	float2 e = enc * 4.0 - 2.0;
	float e2 = dot(e, e);
	float2 xy = e * sqrt(max(0.0, 1.0 - e2 * 0.25));
	float z = -(1.0 - e2 * 0.5);
	return normalize(float3(xy, z));
}

float3 ViewToWorldDirection(float3 direction)
{
	return normalize(float3(
		dot(gViewToWorld[0].xyz, direction),
		dot(gViewToWorld[1].xyz, direction),
		dot(gViewToWorld[2].xyz, direction)));
}

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= gExtent)) {
		return;
	}

	float3 normalWS = ViewToWorldDirection(DecodeViewNormal(gNormal.Load(int3(id, 0))));
	float upNormalGate = smoothstep(0.35, 0.75, normalWS.z);
	gWetnessMask[id] = saturate(gWetnessScalar) * upNormalGate;
}

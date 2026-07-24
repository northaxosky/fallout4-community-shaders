cbuffer WetnessCB : register(b0)
{
	uint2 gExtent;
	float gWetnessScalar;
	float _padding;
	float4 gViewToWorld[3];
};

Texture2D<float2> gNormal : register(t0);
Texture2D<float> gDepth : register(t1);
RWTexture2D<float> gWetnessMask : register(u0);

float3 DecodeViewNormal(float2 enc)
{
	float2 e = enc * 4.0 - 2.0;
	float e2 = dot(e, e);
	float2 xy = e * sqrt(max(0.0, 1.0 - e2 * 0.25));
	float z = -(1.0 - e2 * 0.5);
	return normalize(float3(xy, z));
}

// True world-up component of a view-space (RT20) normal.
//
// world.rotate is the camera world->view rotation, and CommonLibF4 packs its ROWS as
// the camera basis (forward,up,right) in world space - verified from logged matrices:
// row1 tracks world +Z (camera up) across a full yaw+pitch. view->world is therefore
// the TRANSPOSE, so a normal's world height (.z) is dot(thirdColumn, n_ni), where
// thirdColumn = (row0.z,row1.z,row2.z) = (gViewToWorld[0].z, [1].z, [2].z).
//
// The RT20 normal is D3D view order (right,up,forward); the camera frame is Ni order
// (forward,up,right), so the normal is swizzled to n.zyx before the dot. The result
// is invariant to both camera yaw and pitch (it is the real world height of the
// surface normal).
float WorldUpNormal(float3 n)
{
	float3 thirdColumn = float3(gViewToWorld[0].z, gViewToWorld[1].z, gViewToWorld[2].z);
	return dot(thirdColumn, n.zyx);
}

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= gExtent)) {
		return;
	}

	// Sky / far plane carries no wetness.
	if (gDepth.Load(int3(id, 0)) >= 1.0) {
		gWetnessMask[id] = 0.0;
		return;
	}

	float3 n = DecodeViewNormal(gNormal.Load(int3(id, 0)));
	float upness = WorldUpNormal(n);

	// Up-facing surfaces collect rain; scale by the weather-driven wetness amount.
	// wetness=0 (interior / clear) is exact identity - mask is 0 everywhere.
	gWetnessMask[id] = saturate(gWetnessScalar) * smoothstep(0.35, 0.75, upness);
}

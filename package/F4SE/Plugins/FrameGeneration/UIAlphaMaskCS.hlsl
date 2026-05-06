// Per-pixel UI alpha mask from composite-vs-HUDless diff; tagged as kBufferTypeUIAlpha for DLSS-G recomposition.

Texture2D<float4> Hudless    : register(t0);
Texture2D<float4> Backbuffer : register(t1);
RWTexture2D<float> UIAlpha   : register(u0);

// Threshold assumes 8-bit-per-channel sources; HUDless is forced to R8G8B8A8_UNORM upstream.
static const float kEpsilon = 0.5f / 255.0f;

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	uint2 px = dispatchThreadID.xy;

	float3 hudless    = Hudless.Load(int3(px, 0)).rgb;
	float3 backbuffer = Backbuffer.Load(int3(px, 0)).rgb;

	float3 diff = abs(backbuffer - hudless);
	float maxDiff = max(diff.r, max(diff.g, diff.b));

	// OR with reticle alpha already seeded by GenerateSharedBuffersCS at PostAlpha time.
	float existing = UIAlpha[px];
	float fromDiff = maxDiff > kEpsilon ? 1.0f : 0.0f;
	UIAlpha[px] = max(existing, fromDiff);
}

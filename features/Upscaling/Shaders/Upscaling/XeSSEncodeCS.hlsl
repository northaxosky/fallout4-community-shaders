Texture2D<float4> LinearColor : register(t0);
RWTexture2D<float4> OutputColor : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	uint width;
	uint height;
	OutputColor.GetDimensions(width, height);
	if (id.x >= width || id.y >= height) {
		return;
	}
	float4 color = LinearColor.Load(uint3(id.xy, 0));
	OutputColor[id.xy] = float4(pow(max(color.rgb, 0.0), 1.0 / 2.2), color.a);
}

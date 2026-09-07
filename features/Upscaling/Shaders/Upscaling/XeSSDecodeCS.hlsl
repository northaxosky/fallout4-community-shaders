Texture2D<float4> InputColor : register(t0);
RWTexture2D<float4> LinearColor : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	uint width;
	uint height;
	LinearColor.GetDimensions(width, height);
	if (id.x >= width || id.y >= height) {
		return;
	}
	float4 color = InputColor.Load(uint3(id.xy, 0));
	LinearColor[id.xy] = float4(pow(max(color.rgb, 0.0), 2.2), color.a);
}

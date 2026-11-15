// Clamps the float emissive target to the range of the 8-bit target it replaced.
// Blended with MIN on RGB, so the output of 1 is the clamp; PBR pixels keep their range.
Texture2D<float4> Material : register(t0);

float4 main(float4 a_position : SV_POSITION) : SV_Target0
{
	// Class tag 128 (TruePBR) in MRT3 alpha.
	if (abs(Material.Load(int3(int2(a_position.xy), 0)).w * 255.0 - 128.0) < 0.25)
		discard;
	return 1.0;
}

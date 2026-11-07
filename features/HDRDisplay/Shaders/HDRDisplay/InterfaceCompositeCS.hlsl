// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
// Blends the premultiplied interface over the HUD-less frame, output-encoded.
Texture2D<float4> Hudless : register(t0);
Texture2D<float4> Interface : register(t1);
RWTexture2D<float4> Output : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	uint width, height;
	Output.GetDimensions(width, height);
	if (dispatchID.x >= width || dispatchID.y >= height)
		return;

	float4 ui = Interface[dispatchID.xy];
	Output[dispatchID.xy] = float4(ui.rgb + (1.0 - ui.a) * Hudless[dispatchID.xy].rgb, 1.0);
}

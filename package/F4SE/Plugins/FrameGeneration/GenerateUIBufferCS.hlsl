// GenerateUIBufferCS.hlsl
// Extracts UI-only pixels with premultiplied alpha from the difference between
// the HUDLess scene (pre-UI) and the final backbuffer (post-UI).
//
// DLSS-G compositing formula:
//   Final_Color.RGB = UI.RGB + (1 - UI.Alpha) * Hudless.RGB
//
// Two UI rendering modes must be handled:
//   1. Additive/bright UI (crosshair, text): Backbuffer > HUDLess
//      Alpha estimated from max color channel difference.
//   2. Darkening UI (menu backgrounds, overlays): Backbuffer < HUDLess
//      Alpha estimated from scene attenuation ratio.

Texture2D<float4> HUDLessColor : register(t0);   // Clean scene without UI
Texture2D<float4> BackbufferColor : register(t1); // Final frame with UI composited

RWTexture2D<float4> OutputUIColorAlpha : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	float3 hudless = HUDLessColor[DTid.xy].rgb;
	float3 finalColor = BackbufferColor[DTid.xy].rgb;

	// Per-channel absolute difference
	float3 diff = abs(finalColor - hudless);
	float maxDiff = max(diff.r, max(diff.g, diff.b));

	// Threshold: ignore sub-pixel noise from compression/rounding
	if (maxDiff < 0.01) {
		OutputUIColorAlpha[DTid.xy] = float4(0.0, 0.0, 0.0, 0.0);
		return;
	}

	// Brightening UI (crosshair, text, icons): color was added to the scene
	float brighteningAlpha = saturate(maxDiff);

	// Darkening UI (menu backgrounds, overlays): scene was attenuated
	// Ratio of how much scene color remains: low ratio = high opacity overlay
	float3 safeHudless = max(hudless, 0.004);
	float3 ratio = finalColor / safeHudless;
	float minRatio = min(ratio.r, min(ratio.g, ratio.b));
	float darkeningAlpha = saturate(1.0 - minRatio);

	// Use whichever mode produced higher alpha
	float alpha = max(brighteningAlpha, darkeningAlpha);

	// Premultiplied RGB: the UI contribution
	// From: Final = UI_premul + (1 - alpha) * Hudless
	// So:   UI_premul = Final - Hudless * (1 - alpha)
	float3 uiPremul = finalColor - hudless * (1.0 - alpha);
	uiPremul = max(uiPremul, float3(0.0, 0.0, 0.0));
	uiPremul = min(uiPremul, float3(1.0, 1.0, 1.0));

	OutputUIColorAlpha[DTid.xy] = float4(uiPremul, alpha);
}

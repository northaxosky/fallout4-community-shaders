namespace ScreenSpaceShadows
{
	Texture2D<float> ScreenSpaceShadowsTexture : register(t24);

	// SV_POSITION is pixel-centered; no +0.5 unlike upstream
	float GetScreenSpaceShadow(float2 screenPosition)
	{
		return ScreenSpaceShadowsTexture.Load(int3(int2(screenPosition), 0)).x;
	}
}

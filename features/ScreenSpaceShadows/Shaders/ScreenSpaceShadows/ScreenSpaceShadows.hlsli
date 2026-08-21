namespace ScreenSpaceShadows
{
	Texture2D<float> ScreenSpaceShadowsTexture : register(t24);

	// screenPosition is SV_POSITION, which already arrives at pixel centres.
	float GetScreenSpaceShadow(float2 screenPosition)
	{
		return ScreenSpaceShadowsTexture.Load(int3(int2(screenPosition), 0)).x;
	}
}

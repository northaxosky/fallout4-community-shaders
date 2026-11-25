#ifndef FO4_SPHERE_MAP_NORMAL_HLSLI
#define FO4_SPHERE_MAP_NORMAL_HLSLI

namespace FO4GBuffer
{
	// Native sphere-map XY in [-2, 2]; valid encodings satisfy dot(f, f) <= 4.
	float2 UnpackSphereMapXY(float2 encoded)
	{
		float2 f = encoded * 4.0 - 2.0;
#ifdef FO4CS_SUBSTRATE
		// FO4: NaN geometry stores (0,0); decode it as the view-facing normal, not NaN.
		return (dot(f, f) <= 4.0) ? f : float2(0.0, 0.0);
#else
		return f;
#endif
	}
}
#endif

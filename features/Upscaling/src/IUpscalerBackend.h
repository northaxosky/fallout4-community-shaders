#pragma once

#include "Buffer.h"

namespace cs::features::upscaling
{

class IUpscalerBackend
{
public:
	virtual ~IUpscalerBackend() = default;

	virtual bool IsAvailable() const = 0;
	virtual void CreateResources() = 0;
	virtual void DestroyResources() = 0;
	// Reactive-mask generation remains backend-specific.
	virtual void PrepareReactiveMask() {}
	virtual bool NeedsDilatedMotionVectors() const { return false; }
	// Null or stale masks mean no hint.
	virtual void Upscale(Texture2D* a_color, Texture2D* a_dilatedMotionVectors,
		Texture2D* a_reactiveMask, Texture2D* a_transparencyMask,
		float2 a_jitter, float2 a_renderSize, uint a_quality) = 0;
};

}

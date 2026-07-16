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
	virtual void PrepareOpaqueColor() {}
	virtual void PrepareReactiveMask() {}
	virtual bool NeedsDilatedMotionVectors() const { return false; }
	virtual void Upscale(Texture2D* a_color, Texture2D* a_dilatedMotionVectors,
		float2 a_jitter, float2 a_renderSize, uint a_quality) = 0;
};

}

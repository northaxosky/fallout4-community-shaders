#pragma once

#define NV_WINDOWS

#pragma warning(push)
#pragma warning(disable: 4471)
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_matrix_helpers.h>
#include <sl_version.h>
#pragma warning(pop)
#include "Buffer.h"

namespace cs::features::Upscaling
{

// DLSS dispatch helper. SDK plumbing lives in cs::Streamline; this owns the DLSS entry points and dispatch path.
class Streamline
{
public:
	static Streamline* GetSingleton()
	{
		static Streamline singleton;
		return &singleton;
	}

	// Called from Upscaling::OnD3D11Ready via cs::Streamline's feature fan-out.
	void CacheDLSSFunctions();

	void Upscale(Texture2D* a_color, Texture2D* a_dilatedMotionVectorTexture, float2 a_jitter, float2 a_renderSize, uint a_qualityMode);
	void UpdateConstants(float2 a_jitter);
	void DestroyDLSSResources();

	sl::ViewportHandle viewport{ 0 };
	sl::FrameToken*    frameToken = nullptr;

	PFun_slDLSSSetOptions*         slDLSSSetOptions{};
};

}

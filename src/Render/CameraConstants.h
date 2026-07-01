#pragma once

#include "Render/StreamlineCore.h"

#include <cfloat>
#include <cmath>
#include <xmmintrin.h>

namespace cs::engine
{
	// Engine camera snapshot for the Streamline derivation; matrices/vectors are borrowed pointers, pos is a value.
	struct CameraConstants
	{
		const __m128* viewMat;
		const __m128* viewProjUnjittered;
		const __m128* currentViewProjUnjittered;
		const __m128* previousViewProjUnjittered;
		const __m128* viewUp;
		const __m128* viewRight;
		const __m128* viewDir;
		float posX, posY, posZ;
	};

	inline sl::float4x4 ToSLMatrix(const __m128* a_mat)
	{
		sl::float4x4 result;
		for (int i = 0; i < 4; ++i) {
			alignas(16) float row[4];
			_mm_store_ps(row, a_mat[i]);
			result[i] = sl::float4(row[0], row[1], row[2], row[3]);
		}
		return result;
	}

	inline sl::float3 ToSLFloat3(const __m128* a_v)
	{
		alignas(16) float vals[4];
		_mm_store_ps(vals, *a_v);
		return sl::float3(vals[0], vals[1], vals[2]);
	}

	// Shared DLSS/DLSS-G/FSR unjittered-matrix derivation for Upscaling + FrameGeneration, so they can't drift (e.g. the FOV=1.0 bug).
	inline sl::Constants BuildSLConstants(const CameraConstants& a_cam, uint a_width, uint a_height,
		float a_nearZ, float a_farZ, float a_jitterX, float a_jitterY, sl::Boolean a_reset)
	{
		sl::Constants c{};

		// Unjittered view-to-clip: inv(viewMat) * viewProjUnjittered.
		sl::float4x4 viewMatrix = ToSLMatrix(a_cam.viewMat);
		sl::float4x4 invView;
		sl::matrixFullInvert(invView, viewMatrix);
		sl::float4x4 vpUnjittered = ToSLMatrix(a_cam.viewProjUnjittered);
		sl::matrixMul(c.cameraViewToClip, invView, vpUnjittered);
		sl::matrixFullInvert(c.clipToCameraView, c.cameraViewToClip);

		// Reprojection: inv(currentViewProj) * previousViewProj.
		sl::float4x4 currentVP = ToSLMatrix(a_cam.currentViewProjUnjittered);
		sl::float4x4 previousVP = ToSLMatrix(a_cam.previousViewProjUnjittered);
		sl::float4x4 invCurrentVP;
		sl::matrixFullInvert(invCurrentVP, currentVP);
		sl::matrixMul(c.clipToPrevClip, invCurrentVP, previousVP);
		sl::matrixFullInvert(c.prevClipToClip, c.clipToPrevClip);

		c.cameraPos = sl::float3(a_cam.posX, a_cam.posY, a_cam.posZ);
		c.cameraUp = ToSLFloat3(a_cam.viewUp);
		c.cameraRight = ToSLFloat3(a_cam.viewRight);
		c.cameraFwd = ToSLFloat3(a_cam.viewDir);
		c.cameraNear = a_nearZ;
		c.cameraFar = a_farZ;
		c.cameraAspectRatio = (a_height != 0) ? static_cast<float>(a_width) / static_cast<float>(a_height) : 1.0f;
		c.cameraFOV = 2.0f * std::atan(1.0f / c.cameraViewToClip[1].y);
		c.cameraMotionIncluded = sl::Boolean::eTrue;
		c.cameraPinholeOffset = { 0.f, 0.f };
		c.depthInverted = sl::Boolean::eTrue;
		c.jitterOffset = { -a_jitterX, -a_jitterY };
		c.mvecScale = { 1, 1 };
		c.reset = a_reset;
		c.motionVectors3D = sl::Boolean::eFalse;
		c.motionVectorsInvalidValue = FLT_MIN;
		c.orthographicProjection = sl::Boolean::eFalse;
		c.motionVectorsDilated = sl::Boolean::eFalse;
		c.motionVectorsJittered = sl::Boolean::eFalse;
		return c;
	}
}

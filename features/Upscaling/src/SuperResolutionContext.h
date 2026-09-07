#pragma once

#include "Render/TemporalProvider.h"

namespace cs::features
{
	using ColorRange = render::temporal::ColorRange;
	using TransferFunction = render::temporal::TransferFunction;
	using ColorPrimaries = render::temporal::ColorPrimaries;
	using ColorStage = render::temporal::ColorStage;
	using AlphaMode = render::temporal::AlphaMode;
	using ExposureMode = render::temporal::ExposureMode;
	using ColorMetadata = render::temporal::ColorContract;
	using render::temporal::IsFo4PostTonemapSdr;

	struct SuperResolutionInitContext
	{
		ID3D11Device* device = nullptr;
		std::uint32_t maxRenderWidth = 0;
		std::uint32_t maxRenderHeight = 0;
		std::uint32_t outputWidth = 0;
		std::uint32_t outputHeight = 0;
	};

	struct SuperResolutionExecutionContext
	{
		ID3D11DeviceContext* commandContext = nullptr;
		ID3D11Resource* colorInput = nullptr;
		ID3D11Resource* privateOutput = nullptr;
		ID3D11Resource* depth = nullptr;
		ID3D11Resource* motionVectors = nullptr;
		ID3D11Resource* reactiveMask = nullptr;
		ID3D11Resource* transparencyCompositionMask = nullptr;
		std::uint32_t renderWidth = 0;
		std::uint32_t renderHeight = 0;
		std::uint32_t outputWidth = 0;
		std::uint32_t outputHeight = 0;
		std::uint32_t qualityMode = 0;
		std::uint32_t providerPreset = 0;
		std::uint64_t realFrame = 0;
		std::uint32_t engineFrame = 0;
		float jitterX = 0.0f;
		float jitterY = 0.0f;
		float sharpness = 0.0f;
		float frameTimeMilliseconds = 0.0f;
		float cameraNear = 0.0f;
		float cameraFar = 1.0f;
		float cameraVerticalFov = 0.0f;
		bool resetHistory = false;
		ColorMetadata color;
		render::temporal::FrameGenerationCamera camera;
	};
}

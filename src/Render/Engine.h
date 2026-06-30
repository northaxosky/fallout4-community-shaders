#pragma once

namespace cs::engine
{
	// Engine singleton accessors - canonical home for cross-feature renderer-state lookups.
	[[nodiscard]] inline RE::BSGraphics::State* GetGraphicsState()
	{
		static REL::Relocation<RE::BSGraphics::State*> singleton{ REL::ID({ 600795, 2704621, 2704621 }) };
		return singleton.get();
	}

	[[nodiscard]] inline RE::BSGraphics::RenderTargetManager* GetRenderTargetManager()
	{
		static REL::Relocation<RE::BSGraphics::RenderTargetManager*> singleton{ REL::ID({ 1508457, 2666735, 2666735 }) };
		return singleton.get();
	}

	// DrawWorld near/far globals, per Fallout4RE cs-camera-near-far-globals.json @ 2b79e7c.
	// Prefer NiCamera::viewFrustum for live per-camera values; use these only to mirror engine frame setup.
	[[nodiscard]] inline float GetCameraNear()
	{
		static REL::Relocation<float*> near_{ REL::ID({ 57985, 2712882, 2712882 }) };
		return *near_.get();
	}

	[[nodiscard]] inline float GetCameraFar()
	{
		static REL::Relocation<float*> far_{ REL::ID({ 958877, 2712883, 2712883 }) };
		return *far_.get();
	}

	// Dynres offsets, per Fallout4RE cs-rtm-dynamic-res-offsets.json @ a124812; OG lacks NG/AE padding.
	// CommonLibF4's unified layout is wrong for OG and isActivated; always use these accessors.
	namespace dynres
	{
		struct Offsets
		{
			std::ptrdiff_t widthRatio;
			std::ptrdiff_t heightRatio;
			std::ptrdiff_t isActivated;
		};

		inline constexpr Offsets kOG{ 0xF88, 0xF8C, 0xFA8 };
		inline constexpr Offsets kNGAE{ 0xFB8, 0xFBC, 0xFE5 };

		[[nodiscard]] inline Offsets Get()
		{
			return REX::FModule::IsRuntimeOG() ? kOG : kNGAE;
		}

		[[nodiscard]] inline float GetWidthRatio(RE::BSGraphics::RenderTargetManager* a_rtm)
		{
			const auto off = Get();
			return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(a_rtm) + off.widthRatio);
		}

		[[nodiscard]] inline float GetHeightRatio(RE::BSGraphics::RenderTargetManager* a_rtm)
		{
			const auto off = Get();
			return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(a_rtm) + off.heightRatio);
		}

		[[nodiscard]] inline bool IsActivated(RE::BSGraphics::RenderTargetManager* a_rtm)
		{
			const auto off = Get();
			return *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(a_rtm) + off.isActivated);
		}

		inline void Set(RE::BSGraphics::RenderTargetManager* a_rtm, float a_width, float a_height, bool a_activated)
		{
			const auto off = Get();
			auto base = reinterpret_cast<uintptr_t>(a_rtm);
			*reinterpret_cast<float*>(base + off.widthRatio)  = a_width;
			*reinterpret_cast<float*>(base + off.heightRatio) = a_height;
			*reinterpret_cast<bool*>(base + off.isActivated)  = a_activated;

			// Sync CommonLibF4 fields on NG/AE only; on OG they overlap `create` and crash on resize.
			// Source: Fallout4RE cs-rtm-create-field-og.json @ 8256239; OG callers must use dynres accessors.
			if (!REX::FModule::IsRuntimeOG()) {
				a_rtm->dynamicWidthRatio = a_width;
				a_rtm->dynamicHeightRatio = a_height;
				a_rtm->isDynamicResolutionCurrentlyActivated = a_activated;
			}
		}
	}

	// FO4's render-target indices; canonical source for cross-feature engine-RE constants.
	enum class RenderTarget
	{
		kFrameBuffer = 0,

		kRefractionNormal = 1,

		kMainPreAlpha = 2,
		kMain = 3,
		kMainTemp = 4,

		kSSRRaw = 7,
		kSSRBlurred = 8,
		kSSRBlurredExtra = 9,

		kSSRDirection = 10,
		kSSRMask = 11,

		kMainVerticalBlur = 14,
		kMainHorizontalBlur = 15,

		kUI = 17,
		kUITemp = 18,

		kGbufferNormal = 20,
		kGbufferNormalSwap = 21,
		kGbufferAlbedo = 22,
		kGbufferEmissive = 23,
		kGbufferMaterial = 24,  // Glossiness, Specular, Backlighting, SSS

		kTAAAccumulation = 26,
		kTAAAccumulationSwap = 27,

		kSSAO = 28,

		kMotionVectors = 29,

		kUIDownscaled = 36,
		kUIDownscaledComposite = 37,

		kMainDepthMips = 39,
		kSSLRRaytracing = 40,  // Fallout4RE cs-engine-h-rt-enum-extension.json @ 2d73ccf.

		// Full-res SAO buffers sampled by deferred ambient pass (t9), verified by runtime probe + RenderDoc.
		kSSAOFinal = 45,
		kSSAOFinalSwap = 46,
		kSSAOFinalSwap2 = 47,

		kSSAOTemp = 48,
		kSSAOTemp2 = 49,
		kSSAOTemp3 = 50,

		kUnkMask = 57,

		kDiffuseBuffer = 58,
		kSpecularBuffer = 59,

		kDownscaledHDR = 64,
		kDownscaledHDRLuminance2 = 65,
		kDownscaledHDRLuminance3 = 66,
		kDownscaledHDRLuminance4 = 67,
		kDownscaledHDRLuminance5Adaptation = 68,
		kDownscaledHDRLuminance6AdaptationSwap = 69,
		kDownscaledHDRLuminance6 = 70,

		kCount = 101
	};

	enum class DepthStencilTarget
	{
		kMainOtherOther = 0,
		kMainOther = 1,
		kMain = 2,
		kMainCopy = 3,
		kMainCopyCopy = 4,

		kShadowMap = 8,
		kGodraysDepth = 10,  // Fallout4RE cs-engine-h-rt-enum-extension.json @ 2d73ccf.

		kCount = 13
	};
}

#pragma once

#include "Render/ProjectionMath.h"

#include <DirectXMath.h>
#include <d3d11.h>

#include <cmath>
#include <xmmintrin.h>

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

	// DrawWorld near/far globals from Fallout4RE cs-camera-near-far-globals.json @ 2b79e7c; prefer NiCamera::viewFrustum except when mirroring engine frame setup.
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

	// Vertical FOV (radians) from projMat[1][1] = cot(fovY/2) (jitter doesn't affect it); 0 if unavailable.
	[[nodiscard]] inline float GetVerticalFOV()
	{
		auto* state = GetGraphicsState();
		if (!state) {
			return 0.0f;
		}
		alignas(16) float row1[4];
		_mm_store_ps(row1, state->cameraState.camViewData.projMat[1]);
		return (row1[1] != 0.0f) ? 2.0f * std::atan(1.0f / row1[1]) : 0.0f;
	}

	struct CameraMatrices
	{
		DirectX::XMFLOAT4X4 view;
		DirectX::XMFLOAT4X4 proj;
		DirectX::XMFLOAT4X4 viewProj;
		DirectX::XMFLOAT4X4 invView;
		DirectX::XMFLOAT4X4 invProj;
		DirectX::XMFLOAT4X4 invViewProj;
		DirectX::XMFLOAT4   ndcToViewMul;
		DirectX::XMFLOAT4   ndcToViewAdd;
	};

	[[nodiscard]] inline bool TryGetCameraMatrices(CameraMatrices& a_out)
	{
		auto* state = GetGraphicsState();
		if (!state) {
			return false;
		}

		const auto& camera = state->cameraState.camViewData;
		const auto view = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(camera.viewMat));
		const auto proj = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(camera.projMat));
		const auto viewProj = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(camera.viewProjMat));
		const auto invView = DirectX::XMMatrixInverse(nullptr, view);
		const auto invProj = DirectX::XMMatrixInverse(nullptr, proj);
		const auto invViewProj = DirectX::XMMatrixInverse(nullptr, viewProj);

		// NDC->view ray reconstruction: FO4 standard depth is near->0, far->1 (RE-confirmed: the game's
		// own deferred_composite branches depth<=0.01 as the near path, and the DOF Linearize maps 0->near).
		// Note the Streamline depthInverted=eTrue in CameraConstants.h is a DLSS/FSR-contract flag, not the
		// sampled-buffer convention; don't read it as reversed-Z.
		// The divide-by-z makes mul/add independent of NDC z, so z=1 yields the same camera ray.
		const auto viewTopLeft = DirectX::XMVector4Transform(DirectX::XMVectorSet(-1.0f, 1.0f, 1.0f, 1.0f), invProj);
		const auto viewBottomRight = DirectX::XMVector4Transform(DirectX::XMVectorSet(1.0f, -1.0f, 1.0f, 1.0f), invProj);
		const float topLeftZ = DirectX::XMVectorGetZ(viewTopLeft);
		const float bottomRightZ = DirectX::XMVectorGetZ(viewBottomRight);
		if (topLeftZ == 0.0f || bottomRightZ == 0.0f) {
			return false;
		}

		DirectX::XMFLOAT4 topLeft;
		DirectX::XMFLOAT4 bottomRight;
		DirectX::XMStoreFloat4(&topLeft, DirectX::XMVectorScale(viewTopLeft, 1.0f / topLeftZ));
		DirectX::XMStoreFloat4(&bottomRight, DirectX::XMVectorScale(viewBottomRight, 1.0f / bottomRightZ));

		CameraMatrices result{};
		DirectX::XMStoreFloat4x4(&result.view, view);
		DirectX::XMStoreFloat4x4(&result.proj, proj);
		DirectX::XMStoreFloat4x4(&result.viewProj, viewProj);
		DirectX::XMStoreFloat4x4(&result.invView, invView);
		DirectX::XMStoreFloat4x4(&result.invProj, invProj);
		DirectX::XMStoreFloat4x4(&result.invViewProj, invViewProj);
		result.ndcToViewMul = { bottomRight.x - topLeft.x, bottomRight.y - topLeft.y, 0.0f, 0.0f };
		result.ndcToViewAdd = { topLeft.x, topLeft.y, 0.0f, 0.0f };
		a_out = result;
		return true;
	}

	[[nodiscard]] inline bool TryGetWorldSceneProjection(
		DirectX::XMFLOAT4X4& a_outProj,
		DirectX::XMFLOAT4X4& a_outInvProj,
		DirectX::XMFLOAT4&   a_outNdcToViewMul,
		DirectX::XMFLOAT4&   a_outNdcToViewAdd)
	{
		auto* sceneCamera = RE::Main::WorldRootCamera();
		if (!sceneCamera) {
			return false;
		}

		// NiCamera::viewFrustum (+0x160) retains the world/far projection after the near/first-person (~41deg) pass clobbers camViewData.projMat; decode needs this view-space basis.
		const auto& frustum = sceneCamera->viewFrustum;
		const auto& [left, right, top, bottom, nearPlane, farPlane, ortho] = frustum;
		if (ortho) {
			return false;
		}

		return cs::engine::BuildPerspectiveFromFrustum(
			left,
			right,
			top,
			bottom,
			nearPlane,
			farPlane,
			a_outProj,
			a_outInvProj,
			a_outNdcToViewMul,
			a_outNdcToViewAdd);
	}

	// Dynres offsets from Fallout4RE cs-rtm-dynamic-res-offsets.json @ a124812 account for missing OG padding; CommonLibF4's unified layout is wrong for OG and isActivated, so use these accessors.
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

			// Sync CommonLibF4 fields only on NG/AE; on OG they overlap `create` and crash on resize per Fallout4RE cs-rtm-create-field-og.json @ 8256239, so OG callers must use dynres accessors.
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

		// Confirmed on AE 1.11.221; OG 1.10.163 and NG 1.10.984 require validation before rollout.
		kSSAOFinal = 25,

		kTAAAccumulation = 26,
		kTAAAccumulationSwap = 27,

		kSSAO = 28,

		kMotionVectors = 29,

		kUIDownscaled = 36,
		kUIDownscaledComposite = 37,

		kMainDepthMips = 39,
		kSSLRRaytracing = 40,  // Fallout4RE cs-engine-h-rt-enum-extension.json @ 2d73ccf.

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

	[[nodiscard]] inline ID3D11ShaderResourceView* GetSceneDepthSRV()
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return nullptr;
		}
		return reinterpret_cast<ID3D11ShaderResourceView*>(
			rendererData->depthStencilTargets[static_cast<uint>(DepthStencilTarget::kMain)].srViewDepth);
	}

	[[nodiscard]] inline ID3D11ShaderResourceView* GetRenderTargetSRV(RenderTarget a_renderTarget)
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return nullptr;
		}
		return reinterpret_cast<ID3D11ShaderResourceView*>(
			rendererData->renderTargets[static_cast<uint>(a_renderTarget)].srView);
	}

	[[nodiscard]] inline ID3D11RenderTargetView* GetRenderTargetRTV(RenderTarget a_renderTarget)
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return nullptr;
		}
		return reinterpret_cast<ID3D11RenderTargetView*>(
			rendererData->renderTargets[static_cast<uint>(a_renderTarget)].rtView);
	}

	[[nodiscard]] inline ID3D11UnorderedAccessView* GetRenderTargetUAV(RenderTarget a_renderTarget)
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return nullptr;
		}
		return reinterpret_cast<ID3D11UnorderedAccessView*>(
			rendererData->renderTargets[static_cast<uint>(a_renderTarget)].uaView);
	}
}

#pragma once

#include "RE/S/SceneGraph.h"

#include <DirectXMath.h>
#include <d3d11.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace cs::engine
{
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

	[[nodiscard]] inline RE::NiCamera* GetWorldRootCamera()
	{
		auto* worldRoot = RE::Main::GetWorldRootNode();
		return worldRoot ? worldRoot->camera.get() : nullptr;
	}

	inline void SetDynamicResolutionRatios(float a_widthRatio, float a_heightRatio)
	{
		if (auto* renderTargetManager = GetRenderTargetManager()) {
			renderTargetManager->SetDynamicResolutionState(
				a_widthRatio,
				a_heightRatio,
				renderTargetManager->IsDynamicResolutionCurrentlyActivated());
		}
	}

	inline void SetDynamicResolution(float a_widthRatio, float a_heightRatio, bool a_activated)
	{
		if (auto* renderTargetManager = GetRenderTargetManager()) {
			renderTargetManager->SetDynamicResolutionState(a_widthRatio, a_heightRatio, a_activated);
		}
	}

	// Master enable read by ImageSpaceEffectTemporalAA::IsActive; FO4 has no bUseTAA INI literal.
	[[nodiscard]] inline std::uint32_t* GetTemporalAAEnableGlobal()
	{
		static REL::Relocation<std::uint32_t*> global{ REL::ID({ 0, 2704658, 2704658 }) };
		return global.get();
	}

	// Prefer viewFrustum; setup mirrors use these globals.
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

	[[nodiscard]] inline bool TryGetWorldSceneProjection(
		DirectX::XMFLOAT4X4& a_outProj,
		DirectX::XMFLOAT4X4& a_outInvProj,
		DirectX::XMFLOAT4&   a_outNdcToViewMul,
		DirectX::XMFLOAT4&   a_outNdcToViewAdd)
	{
		auto* sceneCamera = GetWorldRootCamera();
		if (!sceneCamera) {
			return false;
		}

		// viewFrustum survives first-person projection overrides.
		const auto& frustum = sceneCamera->viewFrustum;
		return RE::BuildPerspectiveFromFrustum(
			frustum,
			a_outProj,
			a_outInvProj,
			a_outNdcToViewMul,
			a_outNdcToViewAdd);
	}

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
		kGbufferMaterial = 24,  // Glossiness, specular, backlighting, SSS.

		// Deferred ambient composite samples this AO target.
		kSSAOFinal = 25,

		kTAAAccumulation = 26,
		kTAAAccumulationSwap = 27,

		kSSAO = 28,

		// RT29 is full-resolution R16G16_FLOAT motion; half-resolution RT32 contains none.
		kMotionVectors = 29,

		kUIDownscaled = 36,
		kUIDownscaledComposite = 37,

		kMainDepthMips = 39,
		kSSLRRaytracing = 40,

		kSSAOTemp = 48,
		kSSAOTemp2 = 49,
		kSSAOTemp3 = 50,

		// Scalable Ambient Obscurance working buffer, half-res R8G8B8A8; not a mask.
		kSAOWorkBuffer = 57,

		// B slots are repointed from Pip-Boy allocations only while tiled lighting is active.
		kDiffuseBufferA = 58,
		kProbeBufferA = 59,
		kDiffuseBufferB = 60,
		kProbeBufferB = 61,

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

		// DS5 is the configurable sun shadow depth, sized from iShadowMapResolution:Display.
		kShadowMap = 5,

		// DS8 is the fixed 512x512 precipitation occlusion depth, pair-mate of RT86.
		kPrecipitationOcclusion = 8,

		kGodraysDepth = 10,

		kCount = 13
	};

	[[nodiscard]] inline ID3D11Device* GetDevice()
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		return rendererData ? reinterpret_cast<ID3D11Device*>(rendererData->device) : nullptr;
	}

	[[nodiscard]] inline ID3D11DeviceContext* GetImmediateContext()
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		return rendererData ? reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) : nullptr;
	}

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

	[[nodiscard]] inline ID3D11Texture2D* GetRenderTargetTexture(RenderTarget a_renderTarget)
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return nullptr;
		}
		return reinterpret_cast<ID3D11Texture2D*>(
			rendererData->renderTargets[static_cast<uint>(a_renderTarget)].texture);
	}

	[[nodiscard]] inline ID3D11Texture2D* GetRenderTargetCopyTexture(RenderTarget a_renderTarget)
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return nullptr;
		}
		return reinterpret_cast<ID3D11Texture2D*>(
			rendererData->renderTargets[static_cast<uint>(a_renderTarget)].copyTexture);
	}

	[[nodiscard]] inline ID3D11ShaderResourceView* GetRenderTargetCopySRV(RenderTarget a_renderTarget)
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return nullptr;
		}
		return reinterpret_cast<ID3D11ShaderResourceView*>(
			rendererData->renderTargets[static_cast<uint>(a_renderTarget)].copySRView);
	}

	[[nodiscard]] inline ID3D11Texture2D* GetDepthStencilTexture(DepthStencilTarget a_target)
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return nullptr;
		}
		return reinterpret_cast<ID3D11Texture2D*>(
			rendererData->depthStencilTargets[static_cast<uint>(a_target)].texture);
	}

	[[nodiscard]] inline ID3D11DepthStencilView* GetDepthStencilDSV(DepthStencilTarget a_target)
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return nullptr;
		}
		return reinterpret_cast<ID3D11DepthStencilView*>(
			rendererData->depthStencilTargets[static_cast<uint>(a_target)].dsView[0]);
	}

	[[nodiscard]] inline ID3D11ShaderResourceView* GetDepthStencilDepthSRV(DepthStencilTarget a_target)
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return nullptr;
		}
		return reinterpret_cast<ID3D11ShaderResourceView*>(
			rendererData->depthStencilTargets[static_cast<uint>(a_target)].srViewDepth);
	}

	[[nodiscard]] inline ID3D11ShaderResourceView* GetDepthStencilStencilSRV(DepthStencilTarget a_target)
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return nullptr;
		}
		return reinterpret_cast<ID3D11ShaderResourceView*>(
			rendererData->depthStencilTargets[static_cast<uint>(a_target)].srViewStencil);
	}
}

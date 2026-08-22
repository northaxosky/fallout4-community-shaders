#pragma once

#include "RE/S/SceneGraph.h"

#include <DirectXMath.h>
#include <d3d11.h>

#include <cmath>
#include <xmmintrin.h>

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
		auto* worldRoot = RE::Main::WorldRootNode();
		return worldRoot ? worldRoot->camera.get() : nullptr;
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

	// Returns vertical FOV radians, or zero when unavailable.
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

		// near=0, far=1; not reversed-Z.
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

		kMotionVectors = 29,

		kUIDownscaled = 36,
		kUIDownscaledComposite = 37,

		kMainDepthMips = 39,
		kSSLRRaytracing = 40,

		kSSAOTemp = 48,
		kSSAOTemp2 = 49,
		kSSAOTemp3 = 50,

		kUnkMask = 57,

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

		kShadowMap = 8,
		kGodraysDepth = 10,

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

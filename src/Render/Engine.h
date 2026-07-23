#pragma once

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

		// NDC->view ray reconstruction: FO4 standard depth is near->0, far->1 (RE: deferred_composite depth<=0.01 near path; DOF Linearize maps 0->near); Streamline depthInverted=eTrue is a DLSS/FSR contract flag, not reversed-Z.
		// Divide-by-z makes mul/add independent of NDC z, so z=1 yields the same camera ray.
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

		// viewFrustum retains the world projection after the first-person pass overwrites projMat.
		const auto& frustum = sceneCamera->viewFrustum;
		return RE::BuildPerspectiveFromFrustum(
			frustum,
			a_outProj,
			a_outInvProj,
			a_outNdcToViewMul,
			a_outNdcToViewAdd);
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

		// SAO/AO-final the deferred ambient composite samples. Confirmed OG·NG·AE (fallout4-re BSShaderRenderTargets::Create, REL::ID 2318909).
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

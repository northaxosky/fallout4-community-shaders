#pragma once

#include "RE/B/BSShader.h"
#include "RE/S/SceneGraph.h"

#include <DirectXMath.h>
#include <d3d11.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

namespace cs::engine
{
	namespace native
	{
		namespace detail
		{
			enum class ShaderRuntimeLayout
			{
				kOriginal,
				kModern
			};

			[[nodiscard]] inline ShaderRuntimeLayout CurrentShaderRuntimeLayout()
				noexcept
			{
				return REX::FModule::IsRuntimeOG() ?
					ShaderRuntimeLayout::kOriginal :
					ShaderRuntimeLayout::kModern;
			}

			template <class T>
			[[nodiscard]] inline T& RuntimeMember(
				RE::BSShader* a_shader,
				std::ptrdiff_t a_ogOffset,
				std::ptrdiff_t a_modernOffset,
				ShaderRuntimeLayout a_layout =
					CurrentShaderRuntimeLayout()) noexcept
			{
				const auto offset =
					a_layout == ShaderRuntimeLayout::kOriginal ?
					a_ogOffset :
					a_modernOffset;
				return *reinterpret_cast<T*>(
					reinterpret_cast<std::byte*>(a_shader) + offset);
			}

			template <class T>
			[[nodiscard]] inline const T& RuntimeMember(
				const RE::BSShader* a_shader,
				std::ptrdiff_t a_ogOffset,
				std::ptrdiff_t a_modernOffset,
				ShaderRuntimeLayout a_layout =
					CurrentShaderRuntimeLayout()) noexcept
			{
				return RuntimeMember<T>(
					const_cast<RE::BSShader*>(a_shader),
					a_ogOffset,
					a_modernOffset,
					a_layout);
			}
		}

		using VertexShaderMap =
			RE::BSShaderTechniqueIDMap::MapType<
				RE::BSGraphics::VertexShader*>;
		using PixelShaderMap =
			RE::BSShaderTechniqueIDMap::MapType<
				RE::BSGraphics::PixelShader*>;
		using ComputeShaderMap =
			RE::BSShaderTechniqueIDMap::MapType<
				RE::BSGraphics::ComputeShader*>;

		[[nodiscard]] inline const VertexShaderMap& VertexShaders(
			const RE::BSShader* a_shader) noexcept
		{
			return detail::RuntimeMember<VertexShaderMap>(
				a_shader, 0x20, 0x98);
		}

		[[nodiscard]] inline const PixelShaderMap& PixelShaders(
			const RE::BSShader* a_shader) noexcept
		{
			return detail::RuntimeMember<PixelShaderMap>(
				a_shader, 0xB0, 0x128);
		}

		[[nodiscard]] inline const ComputeShaderMap& ComputeShaders(
			const RE::BSShader* a_shader) noexcept
		{
			return detail::RuntimeMember<ComputeShaderMap>(
				a_shader, 0xE0, 0x158);
		}

		[[nodiscard]] inline const ComputeShaderMap&
			StandaloneComputeShaders(const void* a_owner) noexcept
		{
			return *reinterpret_cast<const ComputeShaderMap*>(
				static_cast<const std::byte*>(a_owner) + 0x20);
		}

		[[nodiscard]] inline const char*
			StandaloneComputeOwnerName(const void* a_owner) noexcept
		{
			return *reinterpret_cast<const char* const*>(
				static_cast<const std::byte*>(a_owner) + 0x18);
		}

		[[nodiscard]] inline const char* FxpFilename(
			const RE::BSShader* a_shader) noexcept
		{
			return detail::RuntimeMember<const char*>(
				a_shader, 0x110, 0x188);
		}

		[[nodiscard]] inline std::int32_t ShaderType(
			const RE::BSShader* a_shader) noexcept
		{
			return detail::RuntimeMember<std::int32_t>(
				a_shader,
				0x18,
				0x18,
				detail::ShaderRuntimeLayout::kOriginal);
		}

		[[nodiscard]] inline const char* ImageSpaceShaderPrefix(
			const RE::BSShader* a_shader) noexcept
		{
			if (!a_shader
				|| !REX::FModule::IsRuntimeAE()
				|| ShaderType(a_shader) != 0xC) {
				return nullptr;
			}
			return detail::RuntimeMember<const char*>(
				a_shader,
				0x248,
				0x248,
				detail::ShaderRuntimeLayout::kModern);
		}

		[[nodiscard]] inline const char* ImageSpaceShaderClassName(
			const RE::BSShader* a_shader) noexcept
		{
			if (!a_shader
				|| !REX::FModule::IsRuntimeAE()
				|| ShaderType(a_shader) != 0xC) {
				return nullptr;
			}
			return detail::RuntimeMember<const char*>(
				a_shader,
				0x240,
				0x240,
				detail::ShaderRuntimeLayout::kModern);
		}

		struct ImageSpaceMacroSet
		{
			std::array<std::pair<std::string, std::string>, 7> values;
			std::size_t count = 0;
		};

		namespace detail
		{
			template <bool RequireAeRuntime>
			[[nodiscard]] inline std::optional<ImageSpaceMacroSet>
				GetImageSpaceMacrosImpl(RE::BSShader* a_shader) noexcept
			{
				if (!a_shader || ShaderType(a_shader) != 0xC)
					return std::nullopt;
				if constexpr (RequireAeRuntime) {
					if (!REX::FModule::IsRuntimeAE())
						return std::nullopt;
				}

				struct NativeMacro
				{
					const char* name = nullptr;
					const char* value = nullptr;
				};
				using GetMacros = NativeMacro* (*)(
					RE::BSShader*, NativeMacro*);
				const auto vtable =
					*reinterpret_cast<std::uintptr_t**>(a_shader);
				if (!vtable || !vtable[17])
					return std::nullopt;

				std::array<NativeMacro, 8> nativeMacros{};
				const auto emitter =
					reinterpret_cast<GetMacros>(vtable[17]);
				std::ignore = emitter(a_shader, nativeMacros.data());

				ImageSpaceMacroSet result;
				for (const auto& macro : nativeMacros) {
					if (!macro.name) {
						if (macro.value)
							return std::nullopt;
						return result;
					}
					if (!macro.value
						|| result.count >= result.values.size()) {
						return std::nullopt;
					}
					result.values[result.count++] = {
						macro.name, macro.value
					};
				}
				return std::nullopt;
			}
		}

		[[nodiscard]] inline std::optional<ImageSpaceMacroSet>
			GetImageSpaceMacros(RE::BSShader* a_shader) noexcept
		{
			return detail::GetImageSpaceMacrosImpl<true>(a_shader);
		}

#ifdef FO4CS_SHADER_INJECTION_TESTING
		[[nodiscard]] inline const VertexShaderMap&
			VertexShadersForTesting(
				const RE::BSShader* a_shader,
				bool a_modern) noexcept
		{
			return detail::RuntimeMember<VertexShaderMap>(
				a_shader,
				0x20,
				0x98,
				a_modern ?
					detail::ShaderRuntimeLayout::kModern :
					detail::ShaderRuntimeLayout::kOriginal);
		}

		[[nodiscard]] inline const PixelShaderMap&
			PixelShadersForTesting(
				const RE::BSShader* a_shader,
				bool a_modern) noexcept
		{
			return detail::RuntimeMember<PixelShaderMap>(
				a_shader,
				0xB0,
				0x128,
				a_modern ?
					detail::ShaderRuntimeLayout::kModern :
					detail::ShaderRuntimeLayout::kOriginal);
		}

		[[nodiscard]] inline const ComputeShaderMap&
			ComputeShadersForTesting(
				const RE::BSShader* a_shader,
				bool a_modern) noexcept
		{
			return detail::RuntimeMember<ComputeShaderMap>(
				a_shader,
				0xE0,
				0x158,
				a_modern ?
					detail::ShaderRuntimeLayout::kModern :
					detail::ShaderRuntimeLayout::kOriginal);
		}

		[[nodiscard]] inline const char* FxpFilenameForTesting(
			const RE::BSShader* a_shader,
			bool a_modern) noexcept
		{
			return detail::RuntimeMember<const char*>(
				a_shader,
				0x110,
				0x188,
				a_modern ?
					detail::ShaderRuntimeLayout::kModern :
					detail::ShaderRuntimeLayout::kOriginal);
		}

		[[nodiscard]] inline std::optional<ImageSpaceMacroSet>
			GetImageSpaceMacrosForTesting(RE::BSShader* a_shader) noexcept
		{
			return detail::GetImageSpaceMacrosImpl<false>(a_shader);
		}

		[[nodiscard]] inline const char*
			ImageSpaceShaderPrefixForTesting(
				const RE::BSShader* a_shader) noexcept
		{
			return detail::RuntimeMember<const char*>(
				a_shader,
				0x248,
				0x248,
				detail::ShaderRuntimeLayout::kModern);
		}

		[[nodiscard]] inline const char*
			ImageSpaceShaderClassNameForTesting(
				const RE::BSShader* a_shader) noexcept
		{
			return detail::RuntimeMember<const char*>(
				a_shader,
				0x240,
				0x240,
				detail::ShaderRuntimeLayout::kModern);
		}
#endif
	}

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
		static REL::Relocation<std::uint32_t*> global{ REL::ID({ 460417, 2704658, 2704658 }) };
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

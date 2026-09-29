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
			template <class T>
			[[nodiscard]] inline const T& RuntimeMember(
				const RE::BSShader* a_shader,
				std::ptrdiff_t a_ogOffset,
				std::ptrdiff_t a_modernOffset) noexcept
			{
				const auto offset = REX::FModule::IsRuntimeOG() ? a_ogOffset : a_modernOffset;
				return *reinterpret_cast<const T*>(
					reinterpret_cast<const std::byte*>(a_shader) + offset);
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

		// Shader loaders gate map publication on this byte.
		[[nodiscard]] inline bool ShaderArchiveStreamHasPayload(
			const RE::BSIStream* a_stream) noexcept
		{
			if (!a_stream)
				return false;
			return *(reinterpret_cast<const std::byte*>(a_stream) + 0x10)
				!= std::byte{ 0 };
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
			return *reinterpret_cast<const std::int32_t*>(
				reinterpret_cast<const std::byte*>(a_shader) + 0x18);
		}

		// Class name and source prefix follow the BSShader base.
		[[nodiscard]] inline const char* ImageSpaceShaderPrefix(
			const RE::BSShader* a_shader) noexcept
		{
			if (!a_shader || ShaderType(a_shader) != 0xC) {
				return nullptr;
			}
			return detail::RuntimeMember<const char*>(a_shader, 0x1D0, 0x248);
		}

		[[nodiscard]] inline const char* ImageSpaceShaderClassName(
			const RE::BSShader* a_shader) noexcept
		{
			if (!a_shader || ShaderType(a_shader) != 0xC) {
				return nullptr;
			}
			return detail::RuntimeMember<const char*>(a_shader, 0x1C8, 0x240);
		}

		struct ImageSpaceMacroSet
		{
			std::array<std::pair<std::string, std::string>, 7> values;
			std::size_t count = 0;
		};

		// Vtable slot 17 emits null-terminated macro pairs.
		[[nodiscard]] inline std::optional<ImageSpaceMacroSet>
			GetImageSpaceMacros(RE::BSShader* a_shader) noexcept
		{
			if (!a_shader || ShaderType(a_shader) != 0xC)
				return std::nullopt;

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

	namespace native
	{
		// CommonLibF4 declares BSShaderManager::State members private and exposes no singleton.
		[[nodiscard]] inline const RE::NiTransform* DirectionalAmbientTransform() noexcept
		{
			static REL::Relocation<std::byte*> state{ REL::ID({ 1327069, 2712479, 2712479 }) };
			auto* base = state.get();
			const auto offset = REX::FModule::IsRuntimeOG() ? 0xB8 : 0xC0;
			return base ? reinterpret_cast<const RE::NiTransform*>(base + offset) : nullptr;
		}
	}

	// World-space directional ambient: channel c is pow(max(0, dot(rows[c], float4(N, 1))), 2.2).
	[[nodiscard]] inline bool TryGetDirectionalAmbientRows(DirectX::XMFLOAT4 (&a_rows)[3]) noexcept
	{
		const auto* transform = native::DirectionalAmbientTransform();
		if (!transform)
			return false;
		// BSDFLightShader::SetupGeometry scales rotation by scale, appends (translate, 1), and evaluates (N, 1) * M.
		const auto& rotate = transform->rotate.entry;
		const float scale = transform->scale;
		const float translate[3]{ transform->translate.x, transform->translate.y, transform->translate.z };
		const auto column = [&](const RE::NiPoint4& a_row, std::size_t a_channel) {
			return (a_channel == 0 ? a_row.x : a_channel == 1 ? a_row.y : a_row.z) * scale;
		};
		for (std::size_t channel = 0; channel < 3; ++channel) {
			a_rows[channel] = {
				column(rotate[0], channel),
				column(rotate[1], channel),
				column(rotate[2], channel),
				translate[channel]
			};
		}
		return std::isfinite(scale);
	}

	// Engine reflection cube (logical cube 0); rendered only when bUseCubeMapReflections:Display is set.
	[[nodiscard]] inline ID3D11ShaderResourceView* GetActiveReflectionCubeSRV() noexcept
	{
		const auto* setting = RE::GetINISetting("bUseCubeMapReflections:Display");
		if (!setting || !setting->GetBinary())
			return nullptr;
		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* manager = RE::BSGraphics::RenderTargetManager::GetSingleton();
		if (!rendererData || !manager)
			return nullptr;
		const auto platformID = manager->GetCubeMapRenderTargetPlatformID(0);
		return platformID < std::size(rendererData->cubeMapRenderTargets) ?
			reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->cubeMapRenderTargets[platformID].srView) :
			nullptr;
	}

	[[nodiscard]] inline RE::BSGraphics::State* GetGraphicsState()
	{
		static REL::Relocation<RE::BSGraphics::State*> singleton{ REL::ID({ 600795, 2704621, 2704621 }) };
		return singleton.get();
	}

	// CommonLibF4 uses OG's taaState offset; NG/AE read +0xAC.
	inline void SetGraphicsStateTemporalAA(RE::BSGraphics::State& a_state, bool a_enabled) noexcept
	{
		const auto offset = REX::FModule::IsRuntimeOG() ? 0xA8 : 0xAC;
		*reinterpret_cast<RE::BSGraphics::TAA_STATE*>(reinterpret_cast<std::byte*>(&a_state) + offset) =
			a_enabled ? RE::BSGraphics::TAA_STATE::kEnabled : RE::BSGraphics::TAA_STATE::kDisabled;
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

	// Logical RenderTargetManager IDs; recreation reassigns their physical pool slots.
	enum class RenderTarget : std::uint32_t
	{
		kFrameBuffer = 0,

		kMainTemp = 1,
		kMain = 2,

		kSSLRRayStart = 4,
		kSSLRRayResult = 5,
		kSSLRBlurH = 6,
		kSSLRBlurV = 7,
		kSSLRSurfaceDepth = 8,

		kRefractionNormal = 14,
		kHdrImagespaceAux = 15,

		kUIDownscaled = 24,
		kUIDownscaledComposite = 25,

		kGbufferAlbedo = 26,
		kGbufferNormal = 27,
		// Prepass MRT2: material flag, cubemap index, environment strength.
		kGbufferMetadata = 29,
		kGbufferMaterial = 30,  // Glossiness, specular, backlighting, SSS.
		kGbufferEmissive = 31,

		// Full-resolution R16G16_FLOAT motion.
		kMotionVectors = 32,

		// RGBSPEC gates the specular side; tiled lighting gates the B pair.
		kDiffuseBufferA = 33,
		kSpecularBufferA = 34,
		kDiffuseBufferB = 35,
		kSpecularBufferB = 36,

		// Composite t9 reads the half-resolution target unless NVHBAO or full-resolution AO is active.
		kAmbientOcclusion = 37,
		kAmbientOcclusionHalf = 39,

		// Depth pyramid; logical 41-45 are its mip views.
		kMainDepthMips = 40,

		kMainVerticalBlur = 68,
		kLuminanceDownscale = 70,

		kCount = 100
	};

	// Logical RenderTargetManager depth IDs; recreation reassigns their physical pool slots.
	enum class DepthStencilTarget : std::uint32_t
	{
		kMain = 1,

		// Fixed 512x512 precipitation occlusion depth.
		kPrecipitationOcclusion = 8,

		kCount = 12
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

	[[nodiscard]] inline RE::BSGraphics::DepthStencilTarget* ResolveDepthStencilTarget(DepthStencilTarget a_target)
	{
		const auto logicalID = static_cast<std::uint32_t>(a_target);
		auto*      rendererData = RE::BSGraphics::GetRendererData();
		auto*      renderTargetManager = GetRenderTargetManager();
		if (!rendererData || !renderTargetManager || logicalID >= static_cast<std::uint32_t>(DepthStencilTarget::kCount)) {
			return nullptr;
		}
		const auto slot = renderTargetManager->GetDepthStencilTargetPlatformID(logicalID);
		return slot < std::size(rendererData->depthStencilTargets) ? &rendererData->depthStencilTargets[slot] : nullptr;
	}

	[[nodiscard]] inline ID3D11ShaderResourceView* GetSceneDepthSRV()
	{
		auto* target = ResolveDepthStencilTarget(DepthStencilTarget::kMain);
		return target ? reinterpret_cast<ID3D11ShaderResourceView*>(target->srViewDepth) : nullptr;
	}

	// Resolve at each use; a cached slot goes stale when targets are recreated.
	[[nodiscard]] inline std::optional<std::uint32_t> ResolveRenderTargetSlot(RenderTarget a_renderTarget)
	{
		const auto logicalID = static_cast<std::uint32_t>(a_renderTarget);
		auto*      rendererData = RE::BSGraphics::GetRendererData();
		auto*      renderTargetManager = GetRenderTargetManager();
		if (!rendererData || !renderTargetManager || logicalID >= static_cast<std::uint32_t>(RenderTarget::kCount)) {
			return std::nullopt;
		}
		const auto slot = renderTargetManager->GetRenderTargetPlatformID(logicalID);
		if (slot >= std::size(rendererData->renderTargets)) {
			return std::nullopt;
		}
		return slot;
	}

	[[nodiscard]] inline RE::BSGraphics::RenderTarget* ResolveRenderTarget(RenderTarget a_renderTarget)
	{
		const auto slot = ResolveRenderTargetSlot(a_renderTarget);
		return slot ? &RE::BSGraphics::GetRendererData()->renderTargets[*slot] : nullptr;
	}

	[[nodiscard]] inline ID3D11ShaderResourceView* GetRenderTargetSRV(RenderTarget a_renderTarget)
	{
		auto* target = ResolveRenderTarget(a_renderTarget);
		return target ? reinterpret_cast<ID3D11ShaderResourceView*>(target->srView) : nullptr;
	}

	[[nodiscard]] inline ID3D11RenderTargetView* GetRenderTargetRTV(RenderTarget a_renderTarget)
	{
		auto* target = ResolveRenderTarget(a_renderTarget);
		return target ? reinterpret_cast<ID3D11RenderTargetView*>(target->rtView) : nullptr;
	}

	[[nodiscard]] inline ID3D11UnorderedAccessView* GetRenderTargetUAV(RenderTarget a_renderTarget)
	{
		auto* target = ResolveRenderTarget(a_renderTarget);
		return target ? reinterpret_cast<ID3D11UnorderedAccessView*>(target->uaView) : nullptr;
	}

	[[nodiscard]] inline ID3D11Texture2D* GetRenderTargetTexture(RenderTarget a_renderTarget)
	{
		auto* target = ResolveRenderTarget(a_renderTarget);
		return target ? reinterpret_cast<ID3D11Texture2D*>(target->texture) : nullptr;
	}

	[[nodiscard]] inline ID3D11Texture2D* GetDepthStencilTexture(DepthStencilTarget a_target)
	{
		auto* target = ResolveDepthStencilTarget(a_target);
		return target ? reinterpret_cast<ID3D11Texture2D*>(target->texture) : nullptr;
	}

	[[nodiscard]] inline ID3D11DepthStencilView* GetDepthStencilDSV(DepthStencilTarget a_target)
	{
		auto* target = ResolveDepthStencilTarget(a_target);
		return target ? reinterpret_cast<ID3D11DepthStencilView*>(target->dsView[0]) : nullptr;
	}

	[[nodiscard]] inline ID3D11ShaderResourceView* GetDepthStencilDepthSRV(DepthStencilTarget a_target)
	{
		auto* target = ResolveDepthStencilTarget(a_target);
		return target ? reinterpret_cast<ID3D11ShaderResourceView*>(target->srViewDepth) : nullptr;
	}

	[[nodiscard]] inline ID3D11ShaderResourceView* GetDepthStencilStencilSRV(DepthStencilTarget a_target)
	{
		auto* target = ResolveDepthStencilTarget(a_target);
		return target ? reinterpret_cast<ID3D11ShaderResourceView*>(target->srViewStencil) : nullptr;
	}
}

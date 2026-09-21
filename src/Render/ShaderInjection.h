#pragma once

#include "Render/PixelShaderSwapBroker.h"
#include "Render/ShaderInjectionTargets.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceChild;
struct ID3D11DeviceContext;
struct ID3D11ComputeShader;
struct ID3D11PixelShader;
namespace RE
{
	class BSIStream;
	class BSShader;
	namespace BSGraphics
	{
		class ComputeShader;
		class PixelShader;
		class VertexShader;
	}
}

namespace cs::engine
{
	struct ShaderFamilyDescriptor;
	enum class ShaderResourceType : std::uint8_t
	{
		kConstantBuffer,
		kSampler,
		kShaderResource,
		kUnorderedAccess
	};

	struct ShaderSlotClaim
	{
		ShaderStage        stage = ShaderStage::kPixel;
		ShaderResourceType resourceType = ShaderResourceType::kShaderResource;
		std::uint32_t      slot = 0;

		auto operator<=>(const ShaderSlotClaim&) const = default;
	};

	using ShaderInjectionDefines = std::map<std::string, std::string, std::less<>>;
	using ShaderInjectionReadyPredicate = std::function<bool()>;
	using ShaderInjectionBindCallback = std::function<void(ID3D11DeviceContext*)>;

	struct ShaderReplacementRegistration
	{
		ShaderInjectionTarget         targetId = ShaderInjectionTarget::kCount;
		ShaderStageMask               stages = ShaderStageBit(ShaderStage::kPixel);
		std::string                   contributor;
		ShaderInjectionDefines        defines;
		ShaderInjectionReadyPredicate isReady;
		ShaderInjectionBindCallback   bind;
		std::vector<ShaderSlotClaim>  slotClaims;
	};

	struct ShaderVariantCompilationDescriptor
	{
		std::wstring            sourcePath;
		std::string             entryPoint;
		std::string             profile;
		ShaderInjectionDefines  defines;
	};

	enum class DeveloperShaderOverride : std::uint8_t
	{
		kAuto,
		kForceOn,
		kForceOff
	};

	enum class ShaderInjectionRequestReason : std::uint8_t
	{
		kNone = 0,
		kFeatureContributor = 1U << 0,
		kBaselineOwnership = 1U << 1,
		kDeveloperForceOn = 1U << 2
	};

	constexpr ShaderInjectionRequestReason operator|(
		ShaderInjectionRequestReason a_left,
		ShaderInjectionRequestReason a_right) noexcept
	{
		return static_cast<ShaderInjectionRequestReason>(
			static_cast<std::uint8_t>(a_left)
			| static_cast<std::uint8_t>(a_right));
	}

	constexpr ShaderInjectionRequestReason& operator|=(
		ShaderInjectionRequestReason& a_left,
		ShaderInjectionRequestReason a_right) noexcept
	{
		a_left = a_left | a_right;
		return a_left;
	}

	constexpr bool HasShaderInjectionRequestReason(
		ShaderInjectionRequestReason a_reasons,
		ShaderInjectionRequestReason a_reason) noexcept
	{
		return (
			static_cast<std::uint8_t>(a_reasons)
			& static_cast<std::uint8_t>(a_reason))
			!= 0;
	}

	struct ShaderInjectionTargetSnapshot
	{
		ShaderInjectionTarget  id = ShaderInjectionTarget::kCount;
		std::string            name;
		bool                   requested = false;
		bool                   published = false;
		bool                   slotCollision = false;
		DeveloperShaderOverride developerOverride = DeveloperShaderOverride::kAuto;
		ShaderInjectionRequestReason requestReasons =
			ShaderInjectionRequestReason::kNone;
		std::size_t            contributors = 0;
		ShaderInjectionDefines defines;
		std::string            publicationError;
		std::size_t            variantsObserved = 0;
		std::size_t            variantsPending = 0;
		std::size_t            variantsReady = 0;
		std::size_t            variantsFailed = 0;
		std::size_t            variantsUnsupported = 0;
		std::uint64_t          matches = 0;
		std::uint64_t          substitutions = 0;
		std::uint64_t          compileFailures = 0;
		std::uint64_t          passthroughNotReady = 0;
		std::uint64_t          passthroughDisabled = 0;
		std::uint64_t          dispatches = 0;
	};

	struct ShaderInjectionOutcomeSnapshot
	{
		std::uint64_t matches = 0;
		std::uint64_t substitutions = 0;
	};

	struct ComputeDispatchBridgeStatus
	{
		bool installed = false;
		std::uint64_t bridgeCalls = 0;
		std::uint64_t matchingDispatches = 0;
		std::uint64_t contextRejections = 0;
		std::uint64_t phaseRejections = 0;
		std::uint64_t shaderRejections = 0;
	};

	struct ShaderInjectionSummary
	{
		std::size_t requested = 0;
		std::size_t published = 0;
		std::size_t variantsObserved = 0;
		std::size_t variantsPending = 0;
		std::size_t variantsReady = 0;
		std::size_t variantsFailed = 0;
		std::size_t variantsUnsupported = 0;
		std::size_t requestedByFeatureContributor = 0;
		std::size_t requestedByBaselineOwnership = 0;
		std::size_t requestedByDeveloperForceOn = 0;
		std::uint64_t matches = 0;
		std::uint64_t substitutions = 0;
		std::uint64_t compileFailures = 0;
		std::uint64_t passthroughNotReady = 0;
		std::uint64_t passthroughDisabled = 0;
		std::uint64_t dispatches = 0;
		ComputeDispatchBridgeStatus computeBridge;
	};

	std::optional<ShaderVariantCompilationDescriptor>
		BuildEffectiveShaderCompileRequest(
			const ShaderInjectionTargetMetadata& a_target,
			ShaderStage a_stage,
			const ShaderVariantCompilationDescriptor& a_family,
			std::span<const ShaderReplacementRegistration> a_contributions,
			std::string* a_error = nullptr);

	bool RegisterReplacement(ShaderReplacementRegistration a_registration);
	bool RegisterReplacementIfEnabled(
		bool a_enabled,
		ShaderReplacementRegistration a_registration);
	bool SetBaselineShaderOwnership(
		ShaderInjectionTarget a_target,
		bool a_enabled);
	bool SetDeveloperShaderForceOffEnabled(bool a_enabled);
	bool SetDeveloperShaderOverride(ShaderInjectionTarget a_target, DeveloperShaderOverride a_override);
	bool SetDeveloperShaderSourceRoot(std::wstring a_sourceRoot);
	bool SetShaderInjectionEnabled(bool a_enabled);
	bool ValidateShaderInjectionRoutes(
		std::string_view a_contributor,
		std::string& a_error);

	void FreezeAndCompileShaderInjections(ID3D11Device* a_device);
	bool EnsureComputeDispatchBridgeInstalled(
		ID3D11DeviceContext* a_immediateContext) noexcept;
	[[nodiscard]] bool ComputeDispatchBridgeInstalled() noexcept;
	[[nodiscard]] ComputeDispatchBridgeStatus
		GetComputeDispatchBridgeStatus() noexcept;
	struct NativeGraphicsShaderBinding
	{
		RE::BSGraphics::VertexShader* vertex = nullptr;
		RE::BSGraphics::PixelShader* pixel = nullptr;
	};
#ifdef FO4CS_SHADER_INJECTION_TESTING
	bool InstallComputeDispatchBridgeForTesting(
		ID3D11DeviceContext* a_context,
		std::uintptr_t a_validatedTail) noexcept;
	struct NativeShaderMetadataForTesting
	{
		bool forceEarlyDepthStencil = false;
	};
	std::optional<NativeShaderMetadataForTesting>
		GetObservedNativeShaderMetadataForTesting(
			ID3D11DeviceChild* a_shader) noexcept;
	ID3D11DeviceChild* PrepareNativeShaderVariantForTesting(
		const ShaderFamilyDescriptor& a_descriptor) noexcept;
	bool QueueNativeShaderVariantForTesting(
		const ShaderFamilyDescriptor& a_descriptor) noexcept;
	NativeGraphicsShaderBinding
		ResolveNativeGraphicsShaderBindingForTesting(
			ShaderInjectionTarget a_target,
			std::string_view a_nativeName,
			std::uint32_t a_vertexShaderId,
			std::uint32_t a_pixelShaderId,
			RE::BSGraphics::VertexShader* a_nativeVertex,
			RE::BSGraphics::PixelShader* a_nativePixel) noexcept;
	NativeGraphicsShaderBinding
		ResolveNativeGraphicsShaderBindingForDescriptorTesting(
			const ShaderFamilyDescriptor& a_descriptor,
			std::uint32_t a_vertexShaderId,
			std::uint32_t a_pixelShaderId,
			RE::BSGraphics::VertexShader* a_nativeVertex,
			RE::BSGraphics::PixelShader* a_nativePixel) noexcept;
	RE::BSGraphics::VertexShader*
		CacheNativeVertexReplacementWrapperForTesting(
			RE::BSGraphics::VertexShader* a_nativeVertex,
			ID3D11VertexShader* a_replacement) noexcept;
	struct NativeVariantCacheStatsForTesting
	{
		std::size_t entries = 0;
		std::size_t unsupported = 0;
		std::size_t compilation = 0;
	};
	NativeVariantCacheStatsForTesting
		GetNativeVariantCacheStatsForTesting() noexcept;
	void ObserveNativeComputeShaderForTesting(
		ShaderInjectionTarget a_target,
		std::uint32_t a_descriptor,
		std::string_view a_nativeName,
		ID3D11ComputeShader* a_shader) noexcept;
	void ObserveNativeShaderForTesting(
		RE::BSShader* a_shader,
		const RE::BSIStream* a_stream,
		bool a_modernLayout) noexcept;
#endif
	void DispatchShaderInjections(
		ShaderInjectionTarget a_target,
		ID3D11DeviceContext* a_context) noexcept;
	void DispatchInjectionsForBoundPixelShader(
		ID3D11DeviceContext* a_context) noexcept;
	NativeGraphicsShaderBinding ResolveNativeGraphicsShaderBinding(
		RE::BSShader* a_shader,
		std::uint32_t a_vertexShaderId,
		std::uint32_t a_pixelShaderId,
		RE::BSGraphics::VertexShader* a_nativeVertex,
		RE::BSGraphics::PixelShader* a_nativePixel) noexcept;
	RE::BSGraphics::ComputeShader* ResolveNativeComputeShaderBinding(
		RE::BSGraphics::ComputeShader* a_nativeCompute) noexcept;
	void ObserveNativeShader(
		RE::BSShader* a_shader,
		const RE::BSIStream* a_stream) noexcept;
	void ObserveNativeComputeOwner(
		const void* a_owner,
		ShaderInjectionTarget a_target,
		std::string_view a_nativeName) noexcept;
	void ObserveNativeShaderBytecode(
		ShaderStage a_stage,
		const void* a_bytecode,
		std::size_t a_bytecodeLength,
		ID3D11DeviceChild* a_shader) noexcept;
	void InvalidateNativeShaderVariantCompilations() noexcept;

	const ShaderInjectionDefines* GetActiveShaderInjectionVariantDefines(
		ShaderInjectionTarget a_target) noexcept;
	bool ActiveShaderInjectionVariantHasDefine(
		ShaderInjectionTarget a_target,
		std::string_view a_define) noexcept;
	ShaderInjectionOutcomeSnapshot GetShaderInjectionOutcomeSnapshot(
		ShaderInjectionTarget a_target) noexcept;
	ShaderInjectionTargetSnapshot GetShaderInjectionTargetSnapshot(ShaderInjectionTarget a_target);
	ShaderInjectionSummary GetShaderInjectionSummary() noexcept;
}

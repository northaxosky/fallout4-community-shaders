#pragma once

#include "Render/PixelShaderSwapBroker.h"

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
struct ID3D11DeviceContext;
struct ID3D11PixelShader;

namespace cs::engine
{
	enum class ShaderInjectionTarget : std::uint8_t
	{
		kDeferredComposite,
		kDeferredPrepass,
		kBsdfLightDeferredPoint,
		kAmbientIblPass,
		kBsdfLightDeferredDirectional,
		kBsdfLightDeferredDirectionalIbl,
		kVlsSliceScatter,
		kCount
	};

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

	struct ShaderReplacementVariantRegistration
	{
		ShaderInjectionTarget                targetId =
			ShaderInjectionTarget::kCount;
		std::string                          name;
		std::vector<ShaderVariantKey>        variantKeys;
		std::string                          expectedStockSha1;
		ShaderVariantCompilationDescriptor   compilation;
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

	struct ShaderInjectionDefineMetadata
	{
		std::string_view name;
		std::string_view value;
	};

	struct ShaderInjectionTargetMetadata
	{
		ShaderInjectionTarget                         id = ShaderInjectionTarget::kCount;
		std::string_view                              name;
		std::wstring_view                             sourcePath;
		std::string_view                              entryPoint;
		std::string_view                              profile;
		std::span<const ShaderInjectionDefineMetadata> baseDefines;
	};

	struct ShaderInjectionTargetSnapshot
	{
		ShaderInjectionTarget  id = ShaderInjectionTarget::kCount;
		std::string            name;
		bool                   requested = false;
		bool                   compileAttempted = false;
		bool                   compileOk = false;
		bool                   swappable = false;
		bool                   slotCollision = false;
		DeveloperShaderOverride developerOverride = DeveloperShaderOverride::kAuto;
		ShaderInjectionRequestReason requestReasons =
			ShaderInjectionRequestReason::kNone;
		std::size_t            contributors = 0;
		ShaderInjectionDefines defines;
		std::string            compiledSha1;
		std::string            compileError;
		std::uint64_t          matches = 0;
		std::uint64_t          substitutions = 0;
		std::uint64_t          passthroughCompileFail = 0;
		std::uint64_t          passthroughNotReady = 0;
		std::uint64_t          passthroughDisabled = 0;
		std::uint64_t          dispatches = 0;
	};

	struct ShaderInjectionSummary
	{
		std::size_t requested = 0;
		std::size_t compileAttempted = 0;
		std::size_t compiled = 0;
		std::size_t swappable = 0;
		std::size_t requestedByFeatureContributor = 0;
		std::size_t requestedByBaselineOwnership = 0;
		std::size_t requestedByDeveloperForceOn = 0;
		std::uint64_t matches = 0;
		std::uint64_t substitutions = 0;
		std::uint64_t passthroughCompileFail = 0;
		std::uint64_t passthroughNotReady = 0;
		std::uint64_t passthroughDisabled = 0;
		std::uint64_t dispatches = 0;
	};

	std::span<const ShaderInjectionTargetMetadata> GetShaderInjectionTargets() noexcept;
	const ShaderInjectionTargetMetadata* GetShaderInjectionTarget(ShaderInjectionTarget a_target) noexcept;
	const ShaderInjectionTargetMetadata* FindShaderInjectionTarget(std::string_view a_name) noexcept;

	bool RegisterReplacement(ShaderReplacementRegistration a_registration);
	bool RegisterReplacementIfEnabled(
		bool a_enabled,
		ShaderReplacementRegistration a_registration);
	bool RegisterReplacementVariant(
		ShaderReplacementVariantRegistration a_registration);

	bool SetBaselineShaderOwnership(
		ShaderInjectionTarget a_target,
		bool a_enabled);
	bool SetDeveloperShaderForceOffEnabled(bool a_enabled);
	bool SetDeveloperShaderOverride(ShaderInjectionTarget a_target, DeveloperShaderOverride a_override);
	bool SetDeveloperShaderSourceRoot(std::wstring a_sourceRoot);
	bool SetShaderInjectionEnabled(bool a_enabled);

	void FreezeAndCompileShaderInjections(ID3D11Device* a_device);
	void DispatchShaderInjections(
		ShaderInjectionTarget a_target,
		ID3D11DeviceContext* a_context) noexcept;
	void DispatchInjectionsForBoundPixelShader(
		ID3D11DeviceContext* a_context) noexcept;

	ID3D11PixelShader* GetInjectedPixelShader(ShaderInjectionTarget a_target) noexcept;
	bool IsInjectedPixelShader(
		ShaderInjectionTarget a_target,
		ID3D11PixelShader* a_shader) noexcept;
	ShaderInjectionTargetSnapshot GetShaderInjectionTargetSnapshot(ShaderInjectionTarget a_target);
	ShaderInjectionSummary GetShaderInjectionSummary() noexcept;
}

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
	using ShaderPatchedPixelShaderMatcher = bool (*)(
		ShaderInjectionTarget,
		ID3D11PixelShader*) noexcept;

	struct ShaderReplacementRegistration
	{
		ShaderInjectionTarget         targetId = ShaderInjectionTarget::kCount;
		std::string                   contributor;
		ShaderInjectionDefines        defines;
		ShaderInjectionReadyPredicate isReady;
		ShaderInjectionBindCallback   bind;
		std::vector<ShaderSlotClaim>  slotClaims;
	};

	struct ShaderPatchedDispatchRegistration
	{
		ShaderInjectionTarget targetId = ShaderInjectionTarget::kCount;
		std::string contributor;
		ShaderPatchedPixelShaderMatcher matches = nullptr;
		ShaderInjectionBindCallback bind;
		std::vector<ShaderSlotClaim> slotClaims;
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
		std::size_t            contributors = 0;
		std::size_t            suppressedContributors = 0;
		std::vector<std::string> suppressedContributorNames;
		ShaderInjectionDefines defines;
		std::string            compiledSha1;
		std::string            compileError;
		std::uint64_t          matches = 0;
		std::uint64_t          substitutions = 0;
		std::uint64_t          dispatches = 0;
	};

	struct ShaderInjectionSummary
	{
		std::size_t   requested = 0;
		std::size_t   compiled = 0;
		std::size_t   suppressedContributors = 0;
		std::uint64_t matches = 0;
		std::uint64_t substitutions = 0;
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
	bool RegisterPatchedShaderDispatch(
		ShaderPatchedDispatchRegistration a_registration);

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
	bool IsPatchedDispatchPixelShader(
		ShaderInjectionTarget a_target,
		ID3D11PixelShader* a_shader) noexcept;
	bool ArePatchedShaderDispatchesPublished(
		std::span<const ShaderInjectionTarget> a_targets) noexcept;
	ShaderInjectionTargetSnapshot GetShaderInjectionTargetSnapshot(ShaderInjectionTarget a_target);
	ShaderInjectionSummary GetShaderInjectionSummary() noexcept;

#ifdef FO4CS_SHADER_INJECTION_TESTING
	struct ShaderInjectionFreezePreview
	{
		bool published = false;
		bool hlslRequested = false;
		bool bytecodePatchExclusive = false;
		bool slotCollision = false;
		std::size_t variants = 0;
		std::size_t binds = 0;
		std::size_t patchedMatchers = 0;
		std::size_t suppressedContributors = 0;
		std::vector<std::string> suppressedContributorNames;
	};

	ShaderInjectionFreezePreview PreviewShaderInjectionFreeze(
		ShaderInjectionTarget a_target);
	bool PublishShaderInjectionFreezePreview();
	bool DispatchShaderInjectionForTesting(
		ShaderInjectionTarget a_target,
		ID3D11PixelShader* a_shader,
		ID3D11DeviceContext* a_context);
#endif
}

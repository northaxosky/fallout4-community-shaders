#pragma once

#include "Render/ShaderInjection.h"
#include "Utils/CSSha1.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cs::engine
{
	enum class OwnedShaderRuntime : std::uint8_t
	{
		kAny,
		kOg1_10_163,
		kNg1_10_980,
		kNg1_10_984,
		kAe1_11_221
	};

	enum class OwnedShaderBindAnchor : std::uint8_t
	{
		kNone,
		kPreSunLightDraw
	};

	struct OwnedShaderSourceDesc
	{
		std::string  name;
		std::wstring relativePath;
		std::string  entryPoint;
		std::string  profile;
	};

	struct OwnedShaderIdentityDesc
	{
		std::string            name;
		std::string            subclass;
		ShaderStage            stage = ShaderStage::kPixel;
		std::string            sourceName;
		ShaderInjectionDefines nativeDefines;
		std::string            expectedStockSha1;
		OwnedShaderRuntime     runtime = OwnedShaderRuntime::kAny;
	};

	struct OwnedShaderDeliveryDesc
	{
		std::string           identityName;
		std::string           group;
		ShaderVariantKey      variantKey;
		OwnedShaderBindAnchor bindAnchor = OwnedShaderBindAnchor::kNone;
	};

	struct OwnedShaderContributionDesc
	{
		std::string                  group;
		std::string                  contributor;
		ShaderInjectionDefines       defines;
		std::vector<ShaderSlotClaim> slotClaims;
	};

	struct OwnedShaderGroup
	{
		std::string                  name;
		std::vector<std::string>     identities;
		std::vector<std::string>     contributors;
		ShaderInjectionDefines       defines;
		std::vector<ShaderSlotClaim> slotClaims;
	};

	enum class OwnedShaderFreezeStatus : std::uint8_t
	{
		kOk,
		kAlreadyFrozen,
		kDuplicateSource,
		kDuplicateIdentity,
		kUnknownSource,
		kMalformedStockSha1,
		kDuplicateStockSha1,
		kLegacyStockSha1Overlap,
		kUnknownDeliveryIdentity,
		kDuplicateDeliveryIdentity,
		kConflictingDeliveryKey,
		kGroupWithoutDelivery,
		kDuplicateContributor,
		kConflictingDefine,
		kConflictingSlot
	};

	struct OwnedShaderFreezeResult
	{
		OwnedShaderFreezeStatus status = OwnedShaderFreezeStatus::kOk;
		std::string             subject;

		[[nodiscard]] bool Ok() const noexcept
		{
			return status == OwnedShaderFreezeStatus::kOk;
		}
	};

	enum class OwnedShaderResolutionKind : std::uint8_t
	{
		kNoMatch,
		kBaselineNotReady,
		kBaseline,
		kOverlay
	};

	struct OwnedShaderResolution
	{
		OwnedShaderResolutionKind kind = OwnedShaderResolutionKind::kNoMatch;
		std::string_view          identity;
		std::string_view          group;
		OwnedShaderBindAnchor     bindAnchor = OwnedShaderBindAnchor::kNone;
	};

	[[nodiscard]] bool OwnedShaderSubstitutes(
		OwnedShaderResolutionKind a_kind) noexcept;
	[[nodiscard]] std::string_view OwnedShaderFreezeStatusName(
		OwnedShaderFreezeStatus a_status) noexcept;
	[[nodiscard]] std::string_view OwnedShaderRuntimeName(
		OwnedShaderRuntime a_runtime) noexcept;

	class OwnedShaderRegistry
	{
	public:
		bool AddSource(OwnedShaderSourceDesc a_source);
		bool AddIdentity(OwnedShaderIdentityDesc a_identity);
		bool AddDelivery(OwnedShaderDeliveryDesc a_delivery);
		bool AddContribution(OwnedShaderContributionDesc a_contribution);

		OwnedShaderFreezeResult Freeze(
			std::span<const std::string> a_legacyStockSha1 = {});

		[[nodiscard]] bool Frozen() const noexcept;

		bool SetBaselineReady(std::string_view a_identity, bool a_ready);
		bool SetOverlayReady(std::string_view a_identity, bool a_ready);
		bool SetGroupReady(std::string_view a_group, bool a_ready);

		[[nodiscard]] OwnedShaderResolution Resolve(
			const sha1::Sha1Result& a_stockSha1,
			std::optional<ShaderVariantKeyView> a_variant) const noexcept;

		[[nodiscard]] std::span<const OwnedShaderIdentityDesc>
			Identities() const noexcept;
		[[nodiscard]] std::span<const OwnedShaderGroup> Groups() const noexcept;
		[[nodiscard]] const OwnedShaderSourceDesc* SourceFor(
			std::string_view a_identity) const;
		[[nodiscard]] const OwnedShaderGroup* Group(
			std::string_view a_group) const;

	private:
		enum class Lifecycle : std::uint8_t
		{
			kCollecting,
			kFrozen,
			kRejected
		};

		struct IdentityState
		{
			std::size_t                source = 0;
			std::optional<std::size_t> group;
			ShaderVariantKey           variantKey;
			OwnedShaderBindAnchor      bindAnchor = OwnedShaderBindAnchor::kNone;
			bool                       baselineReady = false;
			bool                       overlayReady = false;
		};

		struct GroupState
		{
			std::vector<std::size_t> members;
			bool                     ready = false;
		};

		struct StockShaOwner
		{
			std::array<std::uint8_t, 20> bytes{};
			std::size_t                  identity = 0;
		};

		using NameIndex = std::map<std::string, std::size_t, std::less<>>;

		OwnedShaderFreezeResult Build(
			std::span<const std::string> a_legacyStockSha1);
		OwnedShaderFreezeResult BuildSources();
		OwnedShaderFreezeResult BuildIdentities(
			std::span<const std::string> a_legacyStockSha1);
		OwnedShaderFreezeResult BuildDeliveries();
		OwnedShaderFreezeResult BuildContributions();

		[[nodiscard]] std::optional<std::size_t> FindIdentity(
			std::string_view a_name) const;
		[[nodiscard]] std::optional<std::size_t> FindGroup(
			std::string_view a_name) const;
		std::size_t EnsureGroup(const std::string& a_name);
		[[nodiscard]] bool GroupOverlaysReady(std::size_t a_group) const noexcept;

		Lifecycle _lifecycle = Lifecycle::kCollecting;

		std::vector<OwnedShaderSourceDesc>       _sources;
		std::vector<OwnedShaderIdentityDesc>     _identities;
		std::vector<OwnedShaderDeliveryDesc>     _deliveries;
		std::vector<OwnedShaderContributionDesc> _contributions;

		std::vector<OwnedShaderGroup> _groups;
		std::vector<IdentityState>    _identityState;
		std::vector<GroupState>       _groupState;
		std::vector<StockShaOwner>    _stockShaOwners;

		NameIndex _sourceIndex;
		NameIndex _identityIndex;
		NameIndex _groupIndex;
	};
}

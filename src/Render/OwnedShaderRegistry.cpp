#include "Render/OwnedShaderRegistry.h"

#include <algorithm>
#include <utility>

namespace cs::engine
{
	namespace
	{
		bool ParseStockSha1(
			const std::string& a_hex,
			std::array<std::uint8_t, 20>& a_bytes)
		{
			sha1::Sha1Result parsed;
			if (!sha1::Sha1FromHex(a_hex, parsed))
				return false;
			a_bytes = parsed.bytes;
			return true;
		}
	}

	bool OwnedShaderSubstitutes(OwnedShaderResolutionKind a_kind) noexcept
	{
		return a_kind == OwnedShaderResolutionKind::kBaseline
			|| a_kind == OwnedShaderResolutionKind::kOverlay;
	}

	std::string_view OwnedShaderFreezeStatusName(
		OwnedShaderFreezeStatus a_status) noexcept
	{
		switch (a_status) {
		case OwnedShaderFreezeStatus::kOk:
			return "ok";
		case OwnedShaderFreezeStatus::kAlreadyFrozen:
			return "already-frozen";
		case OwnedShaderFreezeStatus::kDuplicateSource:
			return "duplicate-source";
		case OwnedShaderFreezeStatus::kDuplicateIdentity:
			return "duplicate-identity";
		case OwnedShaderFreezeStatus::kUnknownSource:
			return "unknown-source";
		case OwnedShaderFreezeStatus::kMalformedStockSha1:
			return "malformed-stock-sha1";
		case OwnedShaderFreezeStatus::kDuplicateStockSha1:
			return "duplicate-stock-sha1";
		case OwnedShaderFreezeStatus::kLegacyStockSha1Overlap:
			return "legacy-stock-sha1-overlap";
		case OwnedShaderFreezeStatus::kUnknownDeliveryIdentity:
			return "unknown-delivery-identity";
		case OwnedShaderFreezeStatus::kDuplicateDeliveryIdentity:
			return "duplicate-delivery-identity";
		case OwnedShaderFreezeStatus::kConflictingDeliveryKey:
			return "conflicting-delivery-key";
		case OwnedShaderFreezeStatus::kGroupWithoutDelivery:
			return "group-without-delivery";
		case OwnedShaderFreezeStatus::kDuplicateContributor:
			return "duplicate-contributor";
		case OwnedShaderFreezeStatus::kConflictingDefine:
			return "conflicting-define";
		case OwnedShaderFreezeStatus::kConflictingSlot:
			return "conflicting-slot";
		}
		return "unknown";
	}

	std::string_view OwnedShaderRuntimeName(
		OwnedShaderRuntime a_runtime) noexcept
	{
		switch (a_runtime) {
		case OwnedShaderRuntime::kAny:
			return "any";
		case OwnedShaderRuntime::kOg1_10_163:
			return "1.10.163";
		case OwnedShaderRuntime::kNg1_10_980:
			return "1.10.980";
		case OwnedShaderRuntime::kNg1_10_984:
			return "1.10.984";
		case OwnedShaderRuntime::kAe1_11_221:
			return "1.11.221";
		}
		return "unknown";
	}

	bool OwnedShaderRegistry::AddSource(OwnedShaderSourceDesc a_source)
	{
		if (_lifecycle != Lifecycle::kCollecting)
			return false;
		_sources.push_back(std::move(a_source));
		return true;
	}

	bool OwnedShaderRegistry::AddIdentity(OwnedShaderIdentityDesc a_identity)
	{
		if (_lifecycle != Lifecycle::kCollecting)
			return false;
		_identities.push_back(std::move(a_identity));
		return true;
	}

	bool OwnedShaderRegistry::AddDelivery(OwnedShaderDeliveryDesc a_delivery)
	{
		if (_lifecycle != Lifecycle::kCollecting)
			return false;
		_deliveries.push_back(std::move(a_delivery));
		return true;
	}

	bool OwnedShaderRegistry::AddContribution(
		OwnedShaderContributionDesc a_contribution)
	{
		if (_lifecycle != Lifecycle::kCollecting)
			return false;
		_contributions.push_back(std::move(a_contribution));
		return true;
	}

	OwnedShaderFreezeResult OwnedShaderRegistry::Freeze(
		std::span<const std::string> a_legacyStockSha1)
	{
		if (_lifecycle != Lifecycle::kCollecting)
			return { OwnedShaderFreezeStatus::kAlreadyFrozen, {} };

		auto result = Build(a_legacyStockSha1);
		_lifecycle = result.Ok() ? Lifecycle::kFrozen : Lifecycle::kRejected;
		return result;
	}

	bool OwnedShaderRegistry::Frozen() const noexcept
	{
		return _lifecycle == Lifecycle::kFrozen;
	}

	bool OwnedShaderRegistry::SetBaselineReady(
		std::string_view a_identity,
		bool a_ready)
	{
		if (!Frozen())
			return false;
		const auto index = FindIdentity(a_identity);
		if (!index)
			return false;
		_identityState[*index].baselineReady = a_ready;
		return true;
	}

	bool OwnedShaderRegistry::SetOverlayReady(
		std::string_view a_identity,
		bool a_ready)
	{
		if (!Frozen())
			return false;
		const auto index = FindIdentity(a_identity);
		if (!index)
			return false;
		_identityState[*index].overlayReady = a_ready;
		return true;
	}

	bool OwnedShaderRegistry::SetGroupReady(
		std::string_view a_group,
		bool a_ready)
	{
		if (!Frozen())
			return false;
		const auto index = FindGroup(a_group);
		if (!index)
			return false;
		_groupState[*index].ready = a_ready;
		return true;
	}

	OwnedShaderResolution OwnedShaderRegistry::Resolve(
		const sha1::Sha1Result& a_stockSha1,
		std::optional<ShaderVariantKeyView> a_variant) const noexcept
	{
		if (_lifecycle != Lifecycle::kFrozen)
			return {};

		const auto owner = std::ranges::lower_bound(
			_stockShaOwners,
			a_stockSha1.bytes,
			{},
			&StockShaOwner::bytes);
		if (owner == _stockShaOwners.end()
			|| owner->bytes != a_stockSha1.bytes) {
			return {};
		}

		const auto& state = _identityState[owner->identity];

		OwnedShaderResolution resolution;
		resolution.identity = _identities[owner->identity].name;
		if (state.group) {
			resolution.group = _groups[*state.group].name;
			resolution.bindAnchor = state.bindAnchor;

			if (a_variant
				&& ShaderVariantKeysConflict(
					ViewShaderVariantKey(state.variantKey),
					*a_variant)
				&& state.baselineReady
				&& _groupState[*state.group].ready
				&& GroupOverlaysReady(*state.group)) {
				resolution.kind = OwnedShaderResolutionKind::kOverlay;
				return resolution;
			}
		}

		resolution.kind = state.baselineReady
			? OwnedShaderResolutionKind::kBaseline
			: OwnedShaderResolutionKind::kBaselineNotReady;
		return resolution;
	}

	std::span<const OwnedShaderIdentityDesc>
		OwnedShaderRegistry::Identities() const noexcept
	{
		if (!Frozen())
			return {};
		return _identities;
	}

	std::span<const OwnedShaderGroup>
		OwnedShaderRegistry::Groups() const noexcept
	{
		if (!Frozen())
			return {};
		return _groups;
	}

	const OwnedShaderSourceDesc* OwnedShaderRegistry::SourceFor(
		std::string_view a_identity) const
	{
		if (!Frozen())
			return nullptr;
		const auto index = FindIdentity(a_identity);
		if (!index)
			return nullptr;
		return &_sources[_identityState[*index].source];
	}

	const OwnedShaderGroup* OwnedShaderRegistry::Group(
		std::string_view a_group) const
	{
		if (!Frozen())
			return nullptr;
		const auto index = FindGroup(a_group);
		if (!index)
			return nullptr;
		return &_groups[*index];
	}

	OwnedShaderFreezeResult OwnedShaderRegistry::Build(
		std::span<const std::string> a_legacyStockSha1)
	{
		if (auto result = BuildSources(); !result.Ok())
			return result;
		if (auto result = BuildIdentities(a_legacyStockSha1); !result.Ok())
			return result;
		if (auto result = BuildDeliveries(); !result.Ok())
			return result;
		return BuildContributions();
	}

	OwnedShaderFreezeResult OwnedShaderRegistry::BuildSources()
	{
		for (std::size_t index = 0; index < _sources.size(); ++index) {
			const auto& source = _sources[index];
			if (!_sourceIndex.try_emplace(source.name, index).second) {
				return {
					OwnedShaderFreezeStatus::kDuplicateSource,
					source.name
				};
			}
		}
		return {};
	}

	OwnedShaderFreezeResult OwnedShaderRegistry::BuildIdentities(
		std::span<const std::string> a_legacyStockSha1)
	{
		std::vector<std::array<std::uint8_t, 20>> legacy;
		legacy.reserve(a_legacyStockSha1.size());
		for (const auto& hex : a_legacyStockSha1) {
			std::array<std::uint8_t, 20> bytes{};
			if (!ParseStockSha1(hex, bytes))
				return { OwnedShaderFreezeStatus::kMalformedStockSha1, hex };
			legacy.push_back(bytes);
		}

		_identityState.assign(_identities.size(), IdentityState{});
		_stockShaOwners.reserve(_identities.size());

		for (std::size_t index = 0; index < _identities.size(); ++index) {
			const auto& identity = _identities[index];
			if (!_identityIndex.try_emplace(identity.name, index).second) {
				return {
					OwnedShaderFreezeStatus::kDuplicateIdentity,
					identity.name
				};
			}

			const auto source = _sourceIndex.find(identity.sourceName);
			if (source == _sourceIndex.end()) {
				return {
					OwnedShaderFreezeStatus::kUnknownSource,
					identity.sourceName
				};
			}
			_identityState[index].source = source->second;

			std::array<std::uint8_t, 20> bytes{};
			if (!ParseStockSha1(identity.expectedStockSha1, bytes)) {
				return {
					OwnedShaderFreezeStatus::kMalformedStockSha1,
					identity.name
				};
			}
			if (std::ranges::find(legacy, bytes) != legacy.end()) {
				return {
					OwnedShaderFreezeStatus::kLegacyStockSha1Overlap,
					identity.name
				};
			}

			_stockShaOwners.push_back({ bytes, index });
		}

		std::ranges::stable_sort(
			_stockShaOwners, {}, &StockShaOwner::bytes);
		const auto duplicate = std::ranges::adjacent_find(
			_stockShaOwners, {}, &StockShaOwner::bytes);
		if (duplicate != _stockShaOwners.end()) {
			return {
				OwnedShaderFreezeStatus::kDuplicateStockSha1,
				_identities[duplicate->identity].name
			};
		}
		return {};
	}

	OwnedShaderFreezeResult OwnedShaderRegistry::BuildDeliveries()
	{
		for (const auto& delivery : _deliveries) {
			const auto index = FindIdentity(delivery.identityName);
			if (!index) {
				return {
					OwnedShaderFreezeStatus::kUnknownDeliveryIdentity,
					delivery.identityName
				};
			}
			if (_identityState[*index].group) {
				return {
					OwnedShaderFreezeStatus::kDuplicateDeliveryIdentity,
					delivery.identityName
				};
			}

			const auto claimed = std::ranges::any_of(
				_identityState,
				[&](const IdentityState& a_state) {
					return a_state.group
						&& ShaderVariantKeysConflict(
							ViewShaderVariantKey(a_state.variantKey),
							ViewShaderVariantKey(delivery.variantKey));
				});
			if (claimed) {
				return {
					OwnedShaderFreezeStatus::kConflictingDeliveryKey,
					delivery.identityName
				};
			}

			const auto group = EnsureGroup(delivery.group);
			auto& state = _identityState[*index];
			state.group = group;
			state.variantKey = delivery.variantKey;
			state.bindAnchor = delivery.bindAnchor;
			_groups[group].identities.push_back(_identities[*index].name);
			_groupState[group].members.push_back(*index);
		}
		return {};
	}

	OwnedShaderFreezeResult OwnedShaderRegistry::BuildContributions()
	{
		for (const auto& contribution : _contributions) {
			const auto group = FindGroup(contribution.group);
			if (!group) {
				return {
					OwnedShaderFreezeStatus::kGroupWithoutDelivery,
					contribution.group
				};
			}

			auto& merged = _groups[*group];
			if (std::ranges::find(
					merged.contributors,
					contribution.contributor) != merged.contributors.end()) {
				return {
					OwnedShaderFreezeStatus::kDuplicateContributor,
					contribution.contributor
				};
			}

			for (const auto& [name, value] : contribution.defines) {
				const auto existing = merged.defines.find(name);
				if (existing != merged.defines.end()
					&& existing->second != value) {
					return {
						OwnedShaderFreezeStatus::kConflictingDefine,
						name
					};
				}
				merged.defines.insert_or_assign(name, value);
			}

			for (const auto& claim : contribution.slotClaims) {
				if (std::ranges::find(merged.slotClaims, claim)
					!= merged.slotClaims.end()) {
					return {
						OwnedShaderFreezeStatus::kConflictingSlot,
						contribution.group
					};
				}
				merged.slotClaims.push_back(claim);
			}

			merged.contributors.push_back(contribution.contributor);
		}
		return {};
	}

	std::optional<std::size_t> OwnedShaderRegistry::FindIdentity(
		std::string_view a_name) const
	{
		const auto found = _identityIndex.find(a_name);
		if (found == _identityIndex.end())
			return std::nullopt;
		return found->second;
	}

	std::optional<std::size_t> OwnedShaderRegistry::FindGroup(
		std::string_view a_name) const
	{
		const auto found = _groupIndex.find(a_name);
		if (found == _groupIndex.end())
			return std::nullopt;
		return found->second;
	}

	std::size_t OwnedShaderRegistry::EnsureGroup(const std::string& a_name)
	{
		const auto [entry, inserted] =
			_groupIndex.try_emplace(a_name, _groups.size());
		if (inserted) {
			OwnedShaderGroup group;
			group.name = a_name;
			_groups.push_back(std::move(group));
			_groupState.emplace_back();
		}
		return entry->second;
	}

	bool OwnedShaderRegistry::GroupOverlaysReady(
		std::size_t a_group) const noexcept
	{
		return std::ranges::all_of(
			_groupState[a_group].members,
			[this](std::size_t a_identity) {
				return _identityState[a_identity].baselineReady
					&& _identityState[a_identity].overlayReady;
			});
	}
}

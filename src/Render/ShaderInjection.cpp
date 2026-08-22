#include "Render/ShaderInjection.h"

#include "Log.h"
#include "LogThrottle.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjectionStaticFamilyRegistrations.h"
#include "Render/ShaderInjectionVariantFactory.h"
#include "Render/ShaderVariantCompilation.h"
#include "Render/SharedData.h"
#include "Utils/CSSha1.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <d3d11.h>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>

namespace cs::engine
{
#include "Render/BsdfShaderReplacementVariants.generated.inl"

	namespace
	{
		constexpr std::array<ShaderInjectionDefineMetadata, 0> kNoDefines{};

		constexpr std::array<ShaderInjectionTargetMetadata,
			static_cast<std::size_t>(ShaderInjectionTarget::kCount)> kTargets{ {
			{
				ShaderInjectionTarget::kDeferredComposite,
				"deferred_composite",
				L"DeferredComposite.hlsl",
				"main",
				"ps_5_0",
				kNoDefines
			},
			{
				ShaderInjectionTarget::kDeferredPrepass,
				"deferred_prepass",
				L"BSDFPrePass.hlsl",
				"main",
				"ps_5_0",
				kNoDefines
			},
			{
				ShaderInjectionTarget::kVlsSliceScatter,
				"vls_slice_scatter",
				L"VolumetricLighting.hlsl",
				"main",
				"ps_5_0",
				kNoDefines
			},
			{
				ShaderInjectionTarget::kBsSky,
				"bssky",
				L"BSSkyShader.hlsl",
				"main",
				"ps_5_0",
				kNoDefines
			},
			{
				ShaderInjectionTarget::kBsWater,
				"bswater",
				L"BSWaterShader.hlsl",
				"main",
				"ps_5_0",
				kNoDefines
			},
			{
				ShaderInjectionTarget::kBsLighting,
				"bslighting",
				L"BSLightingShader.hlsl",
				"main",
				"ps_5_0",
				kNoDefines
			},
			{
				ShaderInjectionTarget::kBsdfLight,
				"bsdf_light",
				L"BSDFLightShader.hlsl",
				"main",
				"ps_5_0",
				kNoDefines
			},
			{
				ShaderInjectionTarget::kBsdfComposite,
				"bsdf_composite",
				L"BSDFCompositeShader.hlsl",
				"main",
				"ps_5_0",
				kNoDefines
			}
		} };

		// Composite and VLS lack stock hash guards.
		constexpr std::array<ShaderInjectionTarget, 6>
			kBaselineOwnableTargets{
				ShaderInjectionTarget::kDeferredPrepass,
				ShaderInjectionTarget::kBsSky,
				ShaderInjectionTarget::kBsWater,
				ShaderInjectionTarget::kBsLighting,
				ShaderInjectionTarget::kBsdfLight,
				ShaderInjectionTarget::kBsdfComposite
			};

		constexpr std::string_view StageName(ShaderStage a_stage) noexcept
		{
			static_assert(
				static_cast<std::uint8_t>(ShaderStage::kCount) == 2);
			switch (a_stage) {
			case ShaderStage::kVertex:
				return "vertex";
			case ShaderStage::kPixel:
				return "pixel";
			}
			std::unreachable();
		}

		constexpr std::string_view StageAbbreviation(
			ShaderStage a_stage) noexcept
		{
			static_assert(
				static_cast<std::uint8_t>(ShaderStage::kCount) == 2);
			switch (a_stage) {
			case ShaderStage::kVertex:
				return "vs";
			case ShaderStage::kPixel:
				return "ps";
			}
			std::unreachable();
		}

		constexpr ShaderStageMask kValidShaderStages =
			ShaderStageBit(ShaderStage::kVertex)
			| ShaderStageBit(ShaderStage::kPixel);

		std::vector<ShaderReplacementVariantRegistration>
		MakeDefaultShaderReplacementVariants()
		{
			using Target = ShaderInjectionTarget;
			auto variants =
				std::vector<ShaderReplacementVariantRegistration>{
				MakeDefaultVariantRegistration(
					Target::kDeferredComposite,
					"default",
					{},
					{}),
				MakeDefaultVariantRegistration(
					Target::kDeferredPrepass,
					"default",
					{},
					"c493970c042ccd90363c57596ff53f6fdd22ce5f"),
				MakeDefaultVariantRegistration(
					Target::kVlsSliceScatter,
					"default",
					{},
					{})
			};
			AppendStaticFamilyShaderReplacementVariants(variants);
			AppendBsdfFamilyShaderReplacementVariants(variants);
			return variants;
		}

		constexpr auto kDefaultShaderRoot = L"Data\\Shaders";
		auto* L = cs::log::Get("cs.render.shaderinjection");

		enum class Lifecycle : std::uint8_t
		{
			kCollecting,
			kFrozen,
			kPublished
		};

		struct TargetRuntimeState
		{
			std::atomic<bool>          requested{ false };
			std::atomic<bool>          compileAttempted{ false };
			std::atomic<bool>          compileOk{ false };
			std::atomic<bool>          compileComplete{ false };
			std::atomic<bool>          swappable{ false };
			std::atomic<bool>          slotCollision{ false };
			std::atomic<std::uint8_t>  requestReasons{ 0 };
			std::atomic<std::size_t>   contributors{ 0 };
			std::atomic<std::uint64_t> matches{ 0 };
			std::atomic<std::uint64_t> substitutions{ 0 };
			std::atomic<std::uint64_t> passthroughCompileFail{ 0 };
			std::atomic<std::uint64_t> passthroughNotReady{ 0 };
			std::atomic<std::uint64_t> passthroughDisabled{ 0 };
			std::atomic<std::uint64_t> dispatches{ 0 };
			DeveloperShaderOverride   developerOverride = DeveloperShaderOverride::kAuto;
			ShaderInjectionDefines    defines;
			std::string               compiledSha1;
			std::string               compileError;
		};

		struct FrozenTarget
		{
			const ShaderInjectionTargetMetadata* metadata = nullptr;
			ShaderInjectionDefines               defines;
			std::vector<ShaderReplacementRegistration> contributions;
			std::vector<ShaderInjectionBindCallback> binds;
			std::vector<ShaderReplacementVariantRegistration> variants;
			std::size_t                          contributors = 0;
			bool                                 slotCollision = false;
		};

		struct PublishedVariant
		{
			ShaderInjectionTarget            targetId =
				ShaderInjectionTarget::kCount;
			std::string                      name;
			std::shared_ptr<ShaderVariantCompilationHandle> compilation;
		};

		struct PreparedVariant
		{
			PublishedVariant variant;
			std::vector<PixelShaderSwapVariantKey> keys;
			ShaderVariantCompilationState compilationState =
				ShaderVariantCompilationState::kFailed;
			std::string compiledSha1;
		};

		struct PublishedTarget
		{
			ShaderInjectionTarget id = ShaderInjectionTarget::kCount;
			std::vector<ShaderInjectionBindCallback> binds;
		};

		struct PublishedPlan
		{
			std::shared_ptr<ShaderVariantCompilationPolicy>
				compilationPolicy;
			std::vector<PublishedTarget> targets;
			std::vector<PublishedVariant> variants;
			std::vector<PixelShaderSwapVariantKey> variantKeys;
		};

		// first claimant wins, in feature-registration order
		struct TargetClaimLedger
		{
			std::vector<ShaderSlotClaim> slots;
			std::array<ShaderInjectionDefines,
				static_cast<std::size_t>(ShaderStage::kCount)> defines;
		};

		struct Service
		{
			std::mutex mutex;
			Lifecycle lifecycle = Lifecycle::kCollecting;
			bool enabled = true;
			bool developerForceOffEnabled = false;
			std::wstring developerSourceRoot;
			std::array<bool,
				static_cast<std::size_t>(ShaderInjectionTarget::kCount)> baselineOwnership{};
			std::array<DeveloperShaderOverride,
				static_cast<std::size_t>(ShaderInjectionTarget::kCount)> developerOverrides{};
			std::vector<ShaderReplacementRegistration> registrations;
			std::array<TargetClaimLedger,
				static_cast<std::size_t>(ShaderInjectionTarget::kCount)> ledgers;
			std::vector<ShaderReplacementVariantRegistration>
				variantRegistrations =
					GetDefaultShaderReplacementVariants();
			std::array<TargetRuntimeState,
				static_cast<std::size_t>(ShaderInjectionTarget::kCount)> runtime;
			std::atomic<std::shared_ptr<const PublishedPlan>> published;
			std::atomic_flag swapCountersLock = ATOMIC_FLAG_INIT;
			bool resolverRegistered = false;
		};

		Service& GetService()
		{
			static Service service;
			return service;
		}

		class SwapCountersGuard
		{
		public:
			explicit SwapCountersGuard(Service& a_service) noexcept :
				_service(a_service)
			{
				while (_service.swapCountersLock.test_and_set(
					std::memory_order_acquire)) {
				}
			}

			~SwapCountersGuard()
			{
				_service.swapCountersLock.clear(std::memory_order_release);
			}

			SwapCountersGuard(const SwapCountersGuard&) = delete;
			SwapCountersGuard& operator=(const SwapCountersGuard&) = delete;

		private:
			Service& _service;
		};

		constexpr std::size_t ToIndex(ShaderInjectionTarget a_target)
		{
			return static_cast<std::size_t>(a_target);
		}

		bool IsValidTarget(ShaderInjectionTarget a_target)
		{
			return ToIndex(a_target) < kTargets.size();
		}

		bool IsBaselineOwnableTarget(ShaderInjectionTarget a_target)
		{
			return std::ranges::find(kBaselineOwnableTargets, a_target)
				!= kBaselineOwnableTargets.end();
		}

		enum class MatchedShaderOutcome : std::uint8_t
		{
			kKeptStock,
			kCompileFailed,
			kNotReady,
			kDisabled,
			kReplaced
		};

		struct RecordedSwapCounts
		{
			std::uint64_t targetSubstitutions = 0;
			std::uint64_t totalSubstitutions = 0;
		};

		RecordedSwapCounts RecordMatchedShaderOutcome(
			ShaderInjectionTarget a_target,
			MatchedShaderOutcome a_outcome) noexcept
		{
			auto& service = GetService();
			SwapCountersGuard guard(service);
			auto& runtime = service.runtime[ToIndex(a_target)];
			runtime.matches.fetch_add(1, std::memory_order_relaxed);
			switch (a_outcome) {
			case MatchedShaderOutcome::kCompileFailed:
				runtime.passthroughCompileFail.fetch_add(
					1, std::memory_order_relaxed);
				break;
			case MatchedShaderOutcome::kNotReady:
				runtime.passthroughNotReady.fetch_add(
					1, std::memory_order_relaxed);
				break;
			case MatchedShaderOutcome::kDisabled:
				runtime.passthroughDisabled.fetch_add(
					1, std::memory_order_relaxed);
				break;
			case MatchedShaderOutcome::kReplaced:
				runtime.substitutions.fetch_add(
					1, std::memory_order_relaxed);
				break;
			case MatchedShaderOutcome::kKeptStock:
				break;
			}

			RecordedSwapCounts counts;
			counts.targetSubstitutions =
				runtime.substitutions.load(std::memory_order_relaxed);
			for (const auto& targetRuntime : service.runtime) {
				counts.totalSubstitutions +=
					targetRuntime.substitutions.load(
						std::memory_order_relaxed);
			}
			return counts;
		}

		std::string_view ContributorName(
			const ShaderReplacementRegistration& a_registration,
			std::size_t a_registrationIndex)
		{
			if (!a_registration.contributor.empty())
				return a_registration.contributor;

			thread_local std::string generated;
			generated = "registration#" + std::to_string(a_registrationIndex);
			return generated;
		}

		void LogLateMutation(std::string_view a_operation)
		{
			L->warn("{} rejected after shader-injection freeze; restart required.", a_operation);
		}

		template <class Registration>
		bool RegistrationHasDuplicateClaims(const Registration& a_registration)
		{
			auto claims = a_registration.slotClaims;
			std::ranges::sort(claims);
			return std::ranges::adjacent_find(claims) != claims.end();
		}

		bool IsReady(
			const ShaderReplacementRegistration& a_registration,
			std::size_t a_registrationIndex)
		{
			if (!a_registration.isReady)
				return true;

			try {
				return a_registration.isReady();
			} catch (const std::exception& e) {
				L->warn(
					"Readiness predicate for '{}' on '{}' failed: {}; contributor skipped.",
					ContributorName(a_registration, a_registrationIndex),
					kTargets[ToIndex(a_registration.targetId)].name,
					e.what());
			} catch (...) {
				L->warn(
					"Readiness predicate for '{}' on '{}' failed; contributor skipped.",
					ContributorName(a_registration, a_registrationIndex),
					kTargets[ToIndex(a_registration.targetId)].name);
			}
			return false;
		}

		bool HasDefineConflict(
			const ShaderReplacementRegistration& a_registration,
			const ShaderInjectionTargetMetadata& a_target,
			std::span<const ShaderReplacementRegistration> a_contributions,
			std::string_view& a_conflictingName,
			std::string_view& a_existingValue)
		{
			for (const auto& [name, value] : a_registration.defines) {
				const auto baseDefine = std::ranges::find(
					a_target.baseDefines,
					name,
					&ShaderInjectionDefineMetadata::name);
				if (baseDefine != a_target.baseDefines.end()
					&& baseDefine->value != value) {
					a_conflictingName = name;
					a_existingValue = baseDefine->value;
					return true;
				}
				for (const auto& contribution : a_contributions) {
					if ((contribution.stages & a_registration.stages) == 0)
						continue;
					const auto existing = contribution.defines.find(name);
					if (existing != contribution.defines.end()
						&& existing->second != value) {
						a_conflictingName = name;
						a_existingValue = existing->second;
						return true;
					}
				}
			}
			return false;
		}

		template <class Registration>
		std::optional<ShaderSlotClaim> FindSlotCollision(
			const Registration& a_registration,
			const std::vector<ShaderSlotClaim>& a_claimedSlots)
		{
			for (const auto& claim : a_registration.slotClaims) {
				if (std::ranges::find(a_claimedSlots, claim) != a_claimedSlots.end())
					return claim;
			}
			return std::nullopt;
		}

		// substrate reserves b5/b6 on active stages
		std::optional<ShaderSlotClaim> FindSubstrateReservation(
			std::span<const ShaderSlotClaim> a_claims)
		{
			for (const auto& claim : a_claims) {
				if (claim.resourceType != ShaderResourceType::kConstantBuffer)
					continue;
				if (claim.slot != render::kSharedDataSlot
					&& claim.slot != render::kFeatureDataSlot) {
					continue;
				}
				return claim;
			}
			return std::nullopt;
		}

		template <class Visitor>
		void ForEachStage(ShaderStageMask a_stages, Visitor&& a_visitor)
		{
			for (std::size_t stage = 0;
				stage < static_cast<std::size_t>(ShaderStage::kCount);
				++stage) {
				if ((a_stages & ShaderStageBit(static_cast<ShaderStage>(stage))) != 0)
					a_visitor(stage);
			}
		}

		// registration-time ledger admission; freeze only reasserts the invariant
		bool ClaimsAvailable(
			const TargetClaimLedger& a_ledger,
			const ShaderReplacementRegistration& a_registration,
			const ShaderInjectionTargetMetadata& a_target)
		{
			if (const auto reserved =
					FindSubstrateReservation(a_registration.slotClaims)) {
				L->error(
					"Replacement registration '{}' for '{}' rejected: stage={} constant buffer b{} is reserved for the shared substrate (b{} and b{}).",
					a_registration.contributor,
					a_target.name,
					static_cast<unsigned>(reserved->stage),
					reserved->slot,
					render::kSharedDataSlot,
					render::kFeatureDataSlot);
				return false;
			}
			if (const auto collision =
					FindSlotCollision(a_registration, a_ledger.slots)) {
				L->error(
					"Replacement registration '{}' for '{}' rejected: stage={} type={} slot={} is already claimed.",
					a_registration.contributor,
					a_target.name,
					static_cast<unsigned>(collision->stage),
					static_cast<unsigned>(collision->resourceType),
					collision->slot);
				return false;
			}
			for (const auto& [name, value] : a_registration.defines) {
				const auto baseDefine = std::ranges::find(
					a_target.baseDefines,
					name,
					&ShaderInjectionDefineMetadata::name);
				if (baseDefine != a_target.baseDefines.end()
					&& baseDefine->value != value) {
					L->error(
						"Replacement registration '{}' for '{}' rejected: {}={} conflicts with the target base define {}.",
						a_registration.contributor,
						a_target.name,
						name,
						value,
						baseDefine->value);
					return false;
				}
				bool conflict = false;
				std::string_view conflictingValue;
				ForEachStage(
					a_registration.stages,
					[&](std::size_t a_stage) {
						const auto existing =
							a_ledger.defines[a_stage].find(name);
						if (existing != a_ledger.defines[a_stage].end()
							&& existing->second != value) {
							conflict = true;
							conflictingValue = existing->second;
						}
					});
				if (conflict) {
					L->error(
						"Replacement registration '{}' for '{}' rejected: {}={} conflicts with the claimed value {}.",
						a_registration.contributor,
						a_target.name,
						name,
						value,
						conflictingValue);
					return false;
				}
			}
			return true;
		}

		void CommitClaims(
			TargetClaimLedger& a_ledger,
			const ShaderReplacementRegistration& a_registration)
		{
			a_ledger.slots.insert(
				a_ledger.slots.end(),
				a_registration.slotClaims.begin(),
				a_registration.slotClaims.end());
			ForEachStage(
				a_registration.stages,
				[&](std::size_t a_stage) {
					a_ledger.defines[a_stage].insert(
						a_registration.defines.begin(),
						a_registration.defines.end());
				});
		}

		std::vector<FrozenTarget> FreezeTargets(
			const std::vector<ShaderReplacementRegistration>& a_registrations,
			const std::vector<ShaderReplacementVariantRegistration>&
				a_variantRegistrations,
			bool a_developerForceOffEnabled,
			const std::array<bool,
				static_cast<std::size_t>(ShaderInjectionTarget::kCount)>&
				a_baselineOwnership,
			const std::array<DeveloperShaderOverride,
				static_cast<std::size_t>(ShaderInjectionTarget::kCount)>& a_developerOverrides)
		{
			std::vector<FrozenTarget> frozen;
			frozen.reserve(kTargets.size());

			for (const auto& metadata : kTargets) {
				FrozenTarget target;
				target.metadata = &metadata;
				for (const auto& define : metadata.baseDefines)
					target.defines.emplace(define.name, define.value);

				const auto targetIndex = ToIndex(metadata.id);
				auto developerOverride = a_developerOverrides[targetIndex];
				if (developerOverride == DeveloperShaderOverride::kForceOff
					&& !a_developerForceOffEnabled)
					developerOverride = DeveloperShaderOverride::kAuto;
				auto requestReasons = ShaderInjectionRequestReason::kNone;
				if (a_baselineOwnership[targetIndex]) {
					requestReasons |=
						ShaderInjectionRequestReason::kBaselineOwnership;
				}
				if (developerOverride == DeveloperShaderOverride::kForceOn) {
					requestReasons |=
						ShaderInjectionRequestReason::kDeveloperForceOn;
				}
				std::vector<ShaderSlotClaim> claimedSlots;

				for (std::size_t registrationIndex = 0;
					registrationIndex < a_registrations.size();
					++registrationIndex) {
					const auto& registration = a_registrations[registrationIndex];
					if (registration.targetId != metadata.id)
						continue;
					if (!render::IsSharedDataReady()) {
						L->error(
							"Contributor '{}' for '{}' dropped because the shared substrate is unavailable.",
							ContributorName(registration, registrationIndex),
							metadata.name);
						continue;
					}
					if (!IsReady(registration, registrationIndex)) {
						L->warn(
							"Contributor '{}' was not ready at freeze; '{}' will run stock for this session (injection readiness is frozen once at startup).",
							registration.contributor,
							metadata.name);
						continue;
					}

					std::string_view conflictingName;
					std::string_view existingValue;
					if (HasDefineConflict(
							registration,
							metadata,
							target.contributions,
							conflictingName,
							existingValue)) {
						L->error(
							"Contributor '{}' for '{}' conflicts on {}={} (requested {}) after registration admitted it; contributor dropped.",
							ContributorName(registration, registrationIndex),
							metadata.name,
							conflictingName,
							existingValue,
							registration.defines.find(conflictingName)->second);
						target.slotCollision = true;
						continue;
					}

					// registration already rejected these; a hit here is a broker invariant break
					if (const auto reserved = FindSubstrateReservation(
							registration.slotClaims)) {
						L->error(
							"Substrate slot collision on '{}' (stage={}, constant buffer b{}) from '{}'; b{} and b{} are reserved for the shared substrate; contributor dropped.",
							metadata.name,
							static_cast<unsigned>(reserved->stage),
							reserved->slot,
							ContributorName(registration, registrationIndex),
							render::kSharedDataSlot,
							render::kFeatureDataSlot);
						target.slotCollision = true;
						continue;
					}

					if (const auto collision = FindSlotCollision(registration, claimedSlots)) {
						L->error(
							"Slot collision on '{}' (stage={}, type={}, slot={}) from '{}'; contributor dropped.",
							metadata.name,
							static_cast<unsigned>(collision->stage),
							static_cast<unsigned>(collision->resourceType),
							collision->slot,
							ContributorName(registration, registrationIndex));
						target.slotCollision = true;
						continue;
					}

					requestReasons |=
						ShaderInjectionRequestReason::kFeatureContributor;
					++target.contributors;
					target.defines.insert(registration.defines.begin(), registration.defines.end());
					target.contributions.push_back(registration);
					claimedSlots.insert(
						claimedSlots.end(),
						registration.slotClaims.begin(),
						registration.slotClaims.end());
					if (registration.bind)
						target.binds.push_back(registration.bind);
				}

				if (developerOverride == DeveloperShaderOverride::kForceOff) {
					requestReasons = ShaderInjectionRequestReason::kNone;
				}
				const bool requested =
					requestReasons != ShaderInjectionRequestReason::kNone;

				for (const auto& variant : a_variantRegistrations) {
					if (variant.targetId == metadata.id)
						target.variants.push_back(variant);
				}
				for (const auto& contribution : target.contributions) {
					const bool stageMatched = std::ranges::any_of(
						target.variants,
						[&contribution](const auto& a_variant) {
							return (
								contribution.stages
								& ShaderStageBit(a_variant.stage))
								!= 0;
						});
					if (!stageMatched) {
						L->warn(
							"Contributor '{}' for '{}' targets no registered shader stages; defines ignored.",
							contribution.contributor.empty()
								? "<unnamed>"
								: contribution.contributor,
							metadata.name);
					}
				}

				auto& runtime = GetService().runtime[targetIndex];
				runtime.requested.store(requested, std::memory_order_relaxed);
				runtime.slotCollision.store(target.slotCollision, std::memory_order_relaxed);
				runtime.requestReasons.store(
					static_cast<std::uint8_t>(requestReasons),
					std::memory_order_relaxed);
				runtime.contributors.store(target.contributors, std::memory_order_relaxed);
				runtime.developerOverride = developerOverride;
				runtime.defines = target.defines;

				if (requested)
					frozen.push_back(std::move(target));
			}
			return frozen;
		}

		std::filesystem::path ResolveSourcePath(
			const std::wstring& a_sourcePath,
			DeveloperShaderOverride a_developerOverride,
			const std::wstring& a_developerSourceRoot)
		{
			const bool useDeveloperRoot =
				a_developerOverride == DeveloperShaderOverride::kForceOn
				&& !a_developerSourceRoot.empty();
			const std::filesystem::path root =
				useDeveloperRoot ? a_developerSourceRoot : kDefaultShaderRoot;
			return root / a_sourcePath;
		}

		std::optional<std::vector<PixelShaderSwapVariantKey>>
			BuildVariantKeys(
			const ShaderReplacementVariantRegistration& a_variant)
		{
			std::optional<sha1::Sha1Result> expected;
			if (!a_variant.expectedStockSha1.empty()) {
				sha1::Sha1Result parsed;
				if (!sha1::Sha1FromHex(
						a_variant.expectedStockSha1,
						parsed)) {
					return std::nullopt;
				}
				expected = parsed;
			}

			std::vector<PixelShaderSwapVariantKey> keys;
			keys.reserve(
				std::max<std::size_t>(
					1,
					a_variant.variantKeys.size()));
			if (a_variant.variantKeys.empty()) {
				keys.push_back({
					.variant = std::nullopt,
					.expectedStockSha1 = expected,
					.routeGroup = ToIndex(a_variant.targetId),
					.stage = a_variant.stage
				});
				return keys;
			}
			for (const auto& variantKey : a_variant.variantKeys) {
				keys.push_back({
					.variant = variantKey,
					.expectedStockSha1 = expected,
					.routeGroup = ToIndex(a_variant.targetId),
					.stage = a_variant.stage
				});
			}
			return keys;
		}

		std::optional<PreparedVariant> PrepareVariant(
			ShaderVariantCompilationPolicy& a_policy,
			ID3D11Device* a_device,
			const FrozenTarget& a_target,
			const ShaderReplacementVariantRegistration& a_variant,
			const std::wstring& a_developerSourceRoot,
			std::string& a_error)
		{
			const auto targetIndex = ToIndex(a_target.metadata->id);
			auto& runtime = GetService().runtime[targetIndex];
			auto keys = BuildVariantKeys(a_variant);
			if (!keys) {
				a_error = "invalid expected stock SHA1";
				L->error(
					"Compile '{}/{}' rejected: {}",
					a_target.metadata->name,
					a_variant.name,
					a_error);
				return std::nullopt;
			}

			auto effectiveRequest = BuildEffectiveShaderCompileRequest(
				*a_target.metadata,
				a_variant,
				a_target.contributions,
				&a_error);
			if (!effectiveRequest) {
				L->error(
					"Compile '{}/{}' rejected: {}",
					a_target.metadata->name,
					a_variant.name,
					a_error);
				return std::nullopt;
			}

			const auto sourcePath = ResolveSourcePath(
				effectiveRequest->sourcePath,
				runtime.developerOverride,
				a_developerSourceRoot);

			ShaderVariantCompilationRequest request;
			request.device.copy_from(a_device);
			request.sourcePath = sourcePath;
			request.entryPoint = std::move(effectiveRequest->entryPoint);
			request.profile = std::move(effectiveRequest->profile);
			request.stage = a_variant.stage;
			request.defines.reserve(effectiveRequest->defines.size());
			for (auto& define : effectiveRequest->defines)
				request.defines.push_back(define);

			auto result = a_policy.Prepare(std::move(request));
			if (result.state == ShaderVariantCompilationState::kFailed
				|| !result.handle) {
				a_error = result.error.empty()
					? "compilation policy failed"
					: std::move(result.error);
				L->error(
					"Compile '{}/{}' failed ({}): {}",
					a_target.metadata->name,
					a_variant.name,
					sourcePath.string(),
					a_error);
				return std::nullopt;
			}

			if (result.state == ShaderVariantCompilationState::kReady) {
				L->info(
					"Compile '{}/{}' ok: {} bytes, dxbc-sha1={}, source={}",
					a_target.metadata->name,
					a_variant.name,
					result.bytecodeSize,
					result.compiledSha1,
					result.servedFromCache ? "cache" : "compiler");
			} else {
				L->info(
					"Compile '{}/{}' pending.",
					a_target.metadata->name,
					a_variant.name);
			}

			PreparedVariant prepared;
			prepared.variant.targetId = a_target.metadata->id;
			prepared.variant.name = a_variant.name;
			prepared.variant.compilation = std::move(result.handle);
			prepared.keys = std::move(*keys);
			prepared.compilationState = result.state;
			prepared.compiledSha1 = std::move(result.compiledSha1);
			return prepared;
		}

		const PublishedTarget* FindPublishedTarget(
			const PublishedPlan& a_plan,
			ShaderInjectionTarget a_target)
		{
			const auto target = std::ranges::find(
				a_plan.targets,
				a_target,
				&PublishedTarget::id);
			return target == a_plan.targets.end() ? nullptr : &*target;
		}

		bool MatchesHlslInjectedPixelShader(
			const PublishedPlan& a_plan,
			ShaderInjectionTarget a_target,
			ID3D11PixelShader* a_shader) noexcept
		{
			if (!a_shader)
				return false;
			return std::ranges::any_of(
				a_plan.variants,
				[a_target, a_shader](const PublishedVariant& a_variant) {
					return a_variant.targetId == a_target
						&& a_variant.compilation
						&& a_variant.compilation->GetStage()
							== ShaderStage::kPixel
						&& a_variant.compilation->PeekShader()
							== static_cast<ID3D11DeviceChild*>(a_shader);
				});
		}

		ShaderSwapResolverResult ResolveInjectedShader(
			const ShaderSwapRequest& a_request) noexcept
		{
			try {
				const auto& a_resolvedVariant = a_request.variant;
				const auto& a_sha = a_request.stockSha1;
				const auto plan =
					GetService().published.load(std::memory_order_acquire);
				if (!plan)
					return ShaderSwapResolverResult::kNoMatch;

				const auto selection = SelectPixelShaderSwapVariant(
					plan->variantKeys,
					a_resolvedVariant,
					a_sha,
					a_request.stage);
				if (selection.kind
					== PixelShaderSwapSelectionKind::kNoMatch) {
					return ShaderSwapResolverResult::kNoMatch;
				}
				if (selection.kind
					== PixelShaderSwapSelectionKind::kUnmappedVariant) {
					CS_LOG_EVERY_MS(
						L,
						2000,
						spdlog::level::warn,
						"Kept stock {} shader sha={}: no replacement registered "
						"for variant {}:{}+0x{:X}.",
						StageName(a_request.stage),
						sha1::Sha1ToHex(a_sha),
						a_resolvedVariant
							? a_resolvedVariant->subclass
							: "<none>",
						a_resolvedVariant
							? StageAbbreviation(
								a_resolvedVariant->stage)
							: "--",
						a_resolvedVariant
							? a_resolvedVariant->id.Value()
							: 0);
					return ShaderSwapResolverResult::kKeepStock;
				}
				if (selection.routeIndex
					>= plan->variantKeys.size()
					|| selection.replacementIndex
						>= plan->variants.size()) {
					return ShaderSwapResolverResult::kKeepStock;
				}

				const auto& variant =
					plan->variants[selection.replacementIndex];
				auto& runtime =
					GetService().runtime[ToIndex(variant.targetId)];
				if (selection.kind
					== PixelShaderSwapSelectionKind::kHashMismatch) {
					const auto& key =
						plan->variantKeys[selection.routeIndex];
					const auto expected = key.expectedStockSha1
						? sha1::Sha1ToHex(*key.expectedStockSha1)
						: std::string("<unknown>");
					CS_LOG_EVERY_MS(
						L,
						2000,
						spdlog::level::err,
						"Refused {} shader replacement '{}/{}': "
						"variant {}:{}+0x{:X} expected sha={} but received {}.",
						StageName(a_request.stage),
						kTargets[ToIndex(variant.targetId)].name,
						variant.name,
						a_resolvedVariant
							? a_resolvedVariant->subclass
							: "<none>",
						a_resolvedVariant
							? StageAbbreviation(
								a_resolvedVariant->stage)
							: "--",
						a_resolvedVariant
							? a_resolvedVariant->id.Value()
							: 0,
						expected,
						sha1::Sha1ToHex(a_sha));
					return ShaderSwapResolverResult::kKeepStock;
				}

				if (!runtime.requested.load(std::memory_order_relaxed)) {
					RecordMatchedShaderOutcome(
						variant.targetId,
						MatchedShaderOutcome::kDisabled);
					return ShaderSwapResolverResult::kKeepStock;
				}
				if (!variant.compilation
					|| variant.compilation->GetStage()
						!= a_request.stage) {
					RecordMatchedShaderOutcome(
						variant.targetId,
						MatchedShaderOutcome::kKeptStock);
					return ShaderSwapResolverResult::kKeepStock;
				}
				auto replacement = variant.compilation
					->AcquireOrRequest();
				if (!ShouldSubstitutePixelShader(
						selection.kind,
						static_cast<bool>(replacement))) {
					const auto state = variant.compilation
						? variant.compilation->GetState()
						: ShaderVariantCompilationState::kFailed;
					RecordMatchedShaderOutcome(
						variant.targetId,
						state == ShaderVariantCompilationState::kFailed
							? MatchedShaderOutcome::kCompileFailed
							: MatchedShaderOutcome::kNotReady);
					return ShaderSwapResolverResult::kKeepStock;
				}

				if (!a_request.output || !*a_request.output) {
					RecordMatchedShaderOutcome(
						variant.targetId,
						MatchedShaderOutcome::kKeptStock);
					return ShaderSwapResolverResult::kKeepStock;
				}
				(*a_request.output)->Release();
				*a_request.output = replacement.detach();
				const auto counts = RecordMatchedShaderOutcome(
					variant.targetId,
					MatchedShaderOutcome::kReplaced);
				if (counts.targetSubstitutions == 1) {
					L->info(
						"Replaced {} shader sha={} -> {}/{} (target_replacements=1, total_replacements={})",
						StageName(a_request.stage),
						sha1::Sha1ToHex(a_sha),
						kTargets[ToIndex(variant.targetId)].name,
						variant.name,
						counts.totalSubstitutions);
				} else if (L->should_log(spdlog::level::debug)) {
					CS_LOG_EVERY_MS(
						L,
						2000,
						spdlog::level::debug,
						"Shader replacements: stage={} target={}/{} target_total={} total={}.",
						StageName(a_request.stage),
						kTargets[ToIndex(variant.targetId)].name,
						variant.name,
						counts.targetSubstitutions,
						counts.totalSubstitutions);
				}
				return ShaderSwapResolverResult::kReplaced;
			} catch (const std::exception& e) {
				CS_LOG_EVERY_MS(
					L,
					2000,
					spdlog::level::err,
					"Shader replacement resolution failed: {}.",
					e.what());
				return ShaderSwapResolverResult::kKeepStock;
			} catch (...) {
				CS_LOG_EVERY_MS(
					L,
					2000,
					spdlog::level::err,
					"Shader replacement resolution failed.");
				return ShaderSwapResolverResult::kKeepStock;
			}
		}
	}

	std::span<const ShaderInjectionTargetMetadata> GetShaderInjectionTargets() noexcept
	{
		return kTargets;
	}

	const ShaderInjectionTargetMetadata* GetShaderInjectionTarget(
		ShaderInjectionTarget a_target) noexcept
	{
		return IsValidTarget(a_target) ? &kTargets[ToIndex(a_target)] : nullptr;
	}

	const ShaderInjectionTargetMetadata* FindShaderInjectionTarget(
		std::string_view a_name) noexcept
	{
		const auto target = std::ranges::find(kTargets, a_name, &ShaderInjectionTargetMetadata::name);
		return target == kTargets.end() ? nullptr : &*target;
	}

	std::vector<ShaderReplacementVariantRegistration>
		GetDefaultShaderReplacementVariants()
	{
		return MakeDefaultShaderReplacementVariants();
	}

	bool RegisterReplacement(ShaderReplacementRegistration a_registration)
	{
		auto& service = GetService();
		const bool installsPreDrawHook = static_cast<bool>(a_registration.bind);
		const auto admissible = [&service](
			const ShaderReplacementRegistration& a_candidate) {
			if (service.lifecycle != Lifecycle::kCollecting) {
				LogLateMutation("Replacement registration");
				return false;
			}
			if (!IsValidTarget(a_candidate.targetId)) {
				L->error("Replacement registration rejected: unknown target.");
				return false;
			}
			const auto& metadata = kTargets[ToIndex(a_candidate.targetId)];
			if (a_candidate.stages == 0
				|| (a_candidate.stages & ~kValidShaderStages) != 0) {
				L->error(
					"Replacement registration '{}' for '{}' rejected: invalid shader stage mask 0x{:X}.",
					a_candidate.contributor,
					metadata.name,
					a_candidate.stages);
				return false;
			}
			if (RegistrationHasDuplicateClaims(a_candidate)) {
				L->error(
					"Replacement registration '{}' for '{}' rejected: duplicate slot claim.",
					a_candidate.contributor,
					metadata.name);
				return false;
			}
			return ClaimsAvailable(
				service.ledgers[ToIndex(a_candidate.targetId)],
				a_candidate,
				metadata);
		};

		{
			std::scoped_lock lock(service.mutex);
			if (!admissible(a_registration))
				return false;
		}

		// active substrate requires current b5/b6 data
		render::EnsureSharedDataUpdateInstalled();
		// a bind callback without its draw anchor would never run
		if (installsPreDrawHook && !EnsurePreSunLightDrawInstalled()) {
			L->error(
				"Replacement registration '{}' for '{}' rejected: the deferred draw anchor is unavailable.",
				a_registration.contributor,
				kTargets[ToIndex(a_registration.targetId)].name);
			return false;
		}

		std::scoped_lock lock(service.mutex);
		if (!admissible(a_registration))
			return false;
		CommitClaims(
			service.ledgers[ToIndex(a_registration.targetId)],
			a_registration);
		service.registrations.push_back(std::move(a_registration));
		return true;
	}

	bool RegisterReplacementIfEnabled(
		bool a_enabled,
		ShaderReplacementRegistration a_registration)
	{
		return !a_enabled || RegisterReplacement(std::move(a_registration));
	}

	bool RegisterReplacementVariant(
		ShaderReplacementVariantRegistration a_registration)
	{
		auto& service = GetService();
		std::scoped_lock lock(service.mutex);
		if (service.lifecycle != Lifecycle::kCollecting) {
			LogLateMutation("Replacement variant registration");
			return false;
		}
		if (!IsValidTarget(a_registration.targetId)) {
			L->error(
				"Replacement variant registration rejected: "
				"unknown target.");
			return false;
		}
		if (a_registration.name.empty()) {
			L->error(
				"Replacement variant registration for '{}' rejected: "
				"empty name.",
				kTargets[ToIndex(a_registration.targetId)].name);
			return false;
		}
		if (a_registration.stage != ShaderStage::kPixel
			&& !a_registration.variantKeys.empty()) {
			L->error(
				"Replacement variant '{}/{}' rejected: "
				"non-pixel variant keys require a runtime variant resolver.",
				kTargets[ToIndex(a_registration.targetId)].name,
				a_registration.name);
			return false;
		}
		for (const auto& key : a_registration.variantKeys) {
			if (key.subclass.empty()) {
				L->error(
					"Replacement variant '{}/{}' rejected: "
					"empty shader subclass.",
					kTargets[ToIndex(a_registration.targetId)].name,
					a_registration.name);
				return false;
			}
			if (key.stage != a_registration.stage) {
				L->error(
					"Replacement variant '{}/{}' rejected: "
					"variant key stage does not match compilation stage.",
					kTargets[ToIndex(a_registration.targetId)].name,
					a_registration.name);
				return false;
			}
		}
		if (!a_registration.variantKeys.empty()
			&& a_registration.expectedStockSha1.empty()) {
			L->error(
				"Replacement variant '{}/{}' rejected: "
				"missing expected stock SHA1 guard.",
				kTargets[ToIndex(a_registration.targetId)].name,
				a_registration.name);
			return false;
		}
		if (a_registration.compilation.sourcePath.empty()) {
			L->error(
				"Replacement variant '{}/{}' rejected: "
				"empty source path.",
				kTargets[ToIndex(a_registration.targetId)].name,
				a_registration.name);
			return false;
		}
		if (a_registration.compilation.entryPoint.empty()) {
			L->error(
				"Replacement variant '{}/{}' rejected: "
				"empty entry point.",
				kTargets[ToIndex(a_registration.targetId)].name,
				a_registration.name);
			return false;
		}
		if (a_registration.compilation.profile
			!= ProfileForStage(a_registration.stage)) {
			L->error(
				"Replacement variant '{}/{}' rejected: "
				"shader profile does not match its stage.",
				kTargets[ToIndex(a_registration.targetId)].name,
				a_registration.name);
			return false;
		}
		if (!a_registration.expectedStockSha1.empty()) {
			sha1::Sha1Result expected;
			if (!sha1::Sha1FromHex(
					a_registration.expectedStockSha1, expected)) {
				L->error(
					"Replacement variant '{}/{}' rejected: "
					"invalid expected stock SHA1.",
					kTargets[ToIndex(a_registration.targetId)].name,
					a_registration.name);
				return false;
			}
			a_registration.expectedStockSha1 =
				sha1::Sha1ToHex(expected);
		}

		for (std::size_t i = 0;
			i < a_registration.variantKeys.size();
			++i) {
			for (std::size_t j = i + 1;
				j < a_registration.variantKeys.size();
				++j) {
				if (!ShaderVariantKeysConflict(
						ViewShaderVariantKey(
							a_registration.variantKeys[i]),
						ViewShaderVariantKey(
							a_registration.variantKeys[j]))) {
					continue;
				}
				const auto& duplicate =
					a_registration.variantKeys[j];
				L->error(
					"Replacement variant '{}/{}' rejected: "
					"duplicate variant key {}:{}+0x{:X}.",
					kTargets[ToIndex(a_registration.targetId)].name,
					a_registration.name,
					duplicate.subclass,
					StageAbbreviation(duplicate.stage),
					duplicate.id.Value());
				return false;
			}
		}

		for (const auto& existing : service.variantRegistrations) {
			if (existing.targetId == a_registration.targetId
				&& existing.name == a_registration.name) {
				L->error(
					"Replacement variant '{}/{}' rejected: "
					"duplicate name.",
					kTargets[ToIndex(a_registration.targetId)].name,
					a_registration.name);
				return false;
			}
			for (const auto& existingKey : existing.variantKeys) {
				for (const auto& newKey :
					a_registration.variantKeys) {
					if (!ShaderVariantKeysConflict(
							ViewShaderVariantKey(existingKey),
							ViewShaderVariantKey(newKey))) {
						continue;
					}
					L->error(
						"Replacement variant '{}/{}' rejected: "
						"duplicate variant key {}:{}+0x{:X}.",
						kTargets[ToIndex(
							a_registration.targetId)].name,
						a_registration.name,
						newKey.subclass,
						StageAbbreviation(newKey.stage),
						newKey.id.Value());
					return false;
				}
			}
			if (!existing.expectedStockSha1.empty()
				&& !a_registration.expectedStockSha1.empty()
				&& existing.expectedStockSha1
					== a_registration.expectedStockSha1) {
				L->error(
					"Replacement variant '{}/{}' rejected: "
					"stock SHA1 already maps to '{}/{}'.",
					kTargets[ToIndex(a_registration.targetId)].name,
					a_registration.name,
					kTargets[ToIndex(existing.targetId)].name,
					existing.name);
				return false;
			}
		}

		service.variantRegistrations.push_back(
			std::move(a_registration));
		return true;
	}

	bool SetBaselineShaderOwnership(
		ShaderInjectionTarget a_target,
		bool a_enabled)
	{
		if (!IsValidTarget(a_target) || !IsBaselineOwnableTarget(a_target)) {
			L->error(
				"Baseline shader ownership rejected: target is not ownable.");
			return false;
		}

		auto& service = GetService();
		std::scoped_lock lock(service.mutex);
		if (service.lifecycle != Lifecycle::kCollecting) {
			LogLateMutation("Baseline shader ownership");
			return false;
		}
		service.baselineOwnership[ToIndex(a_target)] = a_enabled;
		return true;
	}

	bool SetDeveloperShaderForceOffEnabled(bool a_enabled)
	{
		auto& service = GetService();
		std::scoped_lock lock(service.mutex);
		if (service.lifecycle != Lifecycle::kCollecting) {
			LogLateMutation("Developer override state");
			return false;
		}
		service.developerForceOffEnabled = a_enabled;
		return true;
	}

	bool SetDeveloperShaderOverride(
		ShaderInjectionTarget a_target,
		DeveloperShaderOverride a_override)
	{
		if (!IsValidTarget(a_target))
			return false;

		auto& service = GetService();
		std::scoped_lock lock(service.mutex);
		if (service.lifecycle != Lifecycle::kCollecting) {
			LogLateMutation("Developer target override");
			return false;
		}
		service.developerOverrides[ToIndex(a_target)] = a_override;
		return true;
	}

	bool SetDeveloperShaderSourceRoot(std::wstring a_sourceRoot)
	{
		auto& service = GetService();
		std::scoped_lock lock(service.mutex);
		if (service.lifecycle != Lifecycle::kCollecting) {
			LogLateMutation("Developer shader source root");
			return false;
		}
		service.developerSourceRoot = std::move(a_sourceRoot);
		return true;
	}

	bool SetShaderInjectionEnabled(bool a_enabled)
	{
		auto& service = GetService();
		std::scoped_lock lock(service.mutex);
		if (service.lifecycle != Lifecycle::kCollecting) {
			LogLateMutation("Shader-injection kill switch");
			return false;
		}
		service.enabled = a_enabled;
		return true;
	}

	void FreezeAndCompileShaderInjections(ID3D11Device* a_device)
	{
		auto& service = GetService();
		std::vector<ShaderReplacementRegistration> registrations;
		std::vector<ShaderReplacementVariantRegistration>
			variantRegistrations;
		std::array<DeveloperShaderOverride,
			static_cast<std::size_t>(ShaderInjectionTarget::kCount)> developerOverrides{};
		std::array<bool,
			static_cast<std::size_t>(ShaderInjectionTarget::kCount)> baselineOwnership{};
		std::wstring developerSourceRoot;
		bool developerForceOffEnabled = false;
		bool enabled = false;

		{
			std::scoped_lock lock(service.mutex);
			if (service.lifecycle != Lifecycle::kCollecting)
				return;
			service.lifecycle = Lifecycle::kFrozen;
			enabled = service.enabled;
			developerForceOffEnabled = service.developerForceOffEnabled;
			baselineOwnership = service.baselineOwnership;
			developerOverrides = service.developerOverrides;
			developerSourceRoot = service.developerSourceRoot;
			registrations = service.registrations;
			variantRegistrations = service.variantRegistrations;
		}

		sha1::Sha1InitOnce();
		auto plan = std::make_shared<PublishedPlan>();
		plan->compilationPolicy =
			CreateCachingShaderVariantCompilationPolicy();
		std::size_t compileRequested = 0;
		std::size_t compileSucceeded = 0;
		std::size_t swappableVariants = 0;
		ShaderStageMask swappableStages = 0;
		std::vector<FrozenTarget> frozenTargets;
		if (enabled) {
			frozenTargets = FreezeTargets(
				registrations,
				variantRegistrations,
				developerForceOffEnabled,
				baselineOwnership,
				developerOverrides);
		}

		if (!enabled) {
			L->warn("Shader injection disabled by core kill switch.");
		} else if (!a_device) {
			L->error("Shader injection freeze failed: no D3D11 device.");
		} else {
			plan->targets.reserve(frozenTargets.size());
			plan->variants.reserve(variantRegistrations.size());
			std::size_t routeCount = 0;
			for (const auto& registration :
				variantRegistrations) {
				routeCount += std::max<std::size_t>(
					1,
					registration.variantKeys.size());
			}
			plan->variantKeys.reserve(routeCount);
			for (const auto& frozenTarget : frozenTargets) {
				const auto targetIndex =
					ToIndex(frozenTarget.metadata->id);
				auto& runtime = service.runtime[targetIndex];
				runtime.compileAttempted.store(
					!frozenTarget.variants.empty(),
					std::memory_order_relaxed);
				compileRequested += frozenTarget.variants.size();

				std::size_t targetPrepared = 0;
				std::size_t targetReady = 0;
				bool targetSwappable = false;
				std::string firstError;
				std::string onlyCompiledSha1;
				for (const auto& variant : frozenTarget.variants) {
					std::string compileError;
					auto prepared = PrepareVariant(
						*plan->compilationPolicy,
						a_device,
						frozenTarget,
						variant,
						developerSourceRoot,
						compileError);
					if (!prepared) {
						if (firstError.empty())
							firstError = std::move(compileError);
						continue;
					}

					++targetPrepared;
					if (prepared->compilationState
						== ShaderVariantCompilationState::kReady) {
						++compileSucceeded;
						++targetReady;
						onlyCompiledSha1 = prepared->compiledSha1;
					}
					const bool variantSwappable =
						std::ranges::any_of(
							prepared->keys,
							[](const auto& a_key) {
								return a_key.variant.has_value()
									|| a_key.expectedStockSha1
										.has_value();
							});
					targetSwappable =
						targetSwappable || variantSwappable;
					if (variantSwappable)
						++swappableVariants;
					if (variantSwappable) {
						for (const auto& key : prepared->keys)
							swappableStages |= ShaderStageBit(key.stage);
					}
					const auto replacementIndex =
						plan->variants.size();
					for (auto& key : prepared->keys) {
						key.replacementIndex =
							replacementIndex;
						plan->variantKeys.push_back(
							std::move(key));
					}
					plan->variants.push_back(
						std::move(prepared->variant));
				}

				runtime.compileOk.store(
					targetReady > 0,
					std::memory_order_release);
				runtime.compileComplete.store(
					!frozenTarget.variants.empty()
						&& targetReady == frozenTarget.variants.size(),
					std::memory_order_release);
				runtime.swappable.store(
					targetSwappable,
					std::memory_order_release);
				runtime.compileError =
					targetPrepared == frozenTarget.variants.size()
					? std::string{}
					: std::move(firstError);
				if (targetReady == 1) {
					runtime.compiledSha1 =
						std::move(onlyCompiledSha1);
				} else if (targetReady > 1) {
					runtime.compiledSha1 =
						std::to_string(targetReady)
						+ " variants";
				}

				if (targetPrepared > 0) {
					plan->targets.push_back(PublishedTarget{
						frozenTarget.metadata->id,
						frozenTarget.binds
					});
				}
			}
		}

		service.published.store(plan, std::memory_order_release);
		{
			std::scoped_lock lock(service.mutex);
			service.lifecycle = Lifecycle::kPublished;
			if (swappableStages != 0 && !service.resolverRegistered) {
				service.resolverRegistered =
					RegisterPixelShaderSwapResolver({
						.resolver = &ResolveInjectedShader,
						.priority = kHlslReplacementResolverPriority,
						.stages = swappableStages
					});
				if (service.resolverRegistered) {
					L->info(
						"Registered HLSL shader swap resolver (broker hooks={}).",
						PixelShaderSwapBrokerHooksInstalled() ? "present" : "absent");
				} else {
					// no resolver means no target can ever swap
					for (auto& runtime : service.runtime)
						runtime.swappable.store(false, std::memory_order_release);
					L->error(
						"Shader swap resolver registration failed; all targets remain stock.");
				}
			}
		}
		if (compileRequested == 0 && !registrations.empty()) {
			L->warn(
				"{} injection contributor(s) registered but no target was baked; all shaders remain stock.",
				registrations.size());
		}
		const auto summary = GetShaderInjectionSummary();
		L->info(
			"Shader injection freeze: targets requested={} compile_attempted={} compiled_ok={} compile_complete={} swappable={} reasons(feature_contributor={}, baseline_ownership={}, developer_force_on={}); replacement_variants attempted={} compiled_ok={} swappable={}.",
			summary.requested,
			summary.compileAttempted,
			summary.compiled,
			summary.compileComplete,
			summary.swappable,
			summary.requestedByFeatureContributor,
			summary.requestedByBaselineOwnership,
			summary.requestedByDeveloperForceOn,
			compileRequested,
			compileSucceeded,
			swappableVariants);
	}

	void DispatchShaderInjections(
		ShaderInjectionTarget a_target,
		ID3D11DeviceContext* a_context) noexcept
	{
		if (!a_context || !IsValidTarget(a_target))
			return;

		const auto plan = GetService().published.load(std::memory_order_acquire);
		const auto* target = plan ? FindPublishedTarget(*plan, a_target) : nullptr;
		if (!target)
			return;

		// restore b5/b6 before feature binds
		render::BindSharedData(a_context);

		auto& runtime = GetService().runtime[ToIndex(a_target)];
		for (const auto& bind : target->binds) {
			try {
				bind(a_context);
				runtime.dispatches.fetch_add(1, std::memory_order_relaxed);
			} catch (const std::exception& e) {
				CS_LOG_EVERY_MS(
					L,
					2000,
					spdlog::level::warn,
					"Shader injection for '{}' failed: {}.",
					kTargets[ToIndex(a_target)].name,
					e.what());
			} catch (...) {
				CS_LOG_EVERY_MS(
					L,
					2000,
					spdlog::level::warn,
					"Shader injection for '{}' failed.",
					kTargets[ToIndex(a_target)].name);
			}
		}
	}

	void DispatchInjectionsForBoundPixelShader(
		ID3D11DeviceContext* a_context) noexcept
	{
		if (!a_context)
			return;

		const auto plan = GetService().published.load(std::memory_order_acquire);
		if (!plan)
			return;

		ID3D11PixelShader* boundShader = nullptr;
		a_context->PSGetShader(&boundShader, nullptr, nullptr);
		if (!boundShader)
			return;

		for (const auto& target : plan->targets) {
			if (MatchesHlslInjectedPixelShader(
					*plan,
					target.id,
					boundShader)) {
				DispatchShaderInjections(target.id, a_context);
				break;
			}
		}
		boundShader->Release();
	}

	ID3D11PixelShader* GetInjectedPixelShader(ShaderInjectionTarget a_target) noexcept
	{
		if (!IsValidTarget(a_target))
			return nullptr;
		const auto plan = GetService().published.load(std::memory_order_acquire);
		if (!plan)
			return nullptr;
		const auto variant = std::ranges::find(
			plan->variants,
			a_target,
			&PublishedVariant::targetId);
		for (auto current = variant;
			current != plan->variants.end()
				&& current->targetId == a_target;
			++current) {
			if (current->compilation
				&& current->compilation->GetStage()
					== ShaderStage::kPixel) {
				return static_cast<ID3D11PixelShader*>(
					current->compilation->PeekShader());
			}
		}
		return nullptr;
	}

	bool IsInjectedPixelShader(
		ShaderInjectionTarget a_target,
		ID3D11PixelShader* a_shader) noexcept
	{
		if (!a_shader || !IsValidTarget(a_target))
			return false;
		const auto plan =
			GetService().published.load(std::memory_order_acquire);
		return plan && MatchesHlslInjectedPixelShader(
			*plan,
			a_target,
			a_shader);
	}

	ShaderInjectionTargetSnapshot GetShaderInjectionTargetSnapshot(
		ShaderInjectionTarget a_target)
	{
		ShaderInjectionTargetSnapshot snapshot;
		if (!IsValidTarget(a_target))
			return snapshot;

		auto& service = GetService();
		std::scoped_lock lock(service.mutex);
		const auto& metadata = kTargets[ToIndex(a_target)];
		const auto& runtime = service.runtime[ToIndex(a_target)];
		snapshot.id = a_target;
		snapshot.name = metadata.name;
		snapshot.requested = runtime.requested.load(std::memory_order_relaxed);
		snapshot.compileAttempted = runtime.compileAttempted.load(std::memory_order_relaxed);
		snapshot.compileOk = runtime.compileOk.load(std::memory_order_relaxed);
		snapshot.compileComplete =
			runtime.compileComplete.load(std::memory_order_relaxed);
		snapshot.swappable = runtime.swappable.load(std::memory_order_relaxed);
		snapshot.slotCollision = runtime.slotCollision.load(std::memory_order_relaxed);
		snapshot.developerOverride = runtime.developerOverride;
		snapshot.requestReasons =
			static_cast<ShaderInjectionRequestReason>(
				runtime.requestReasons.load(std::memory_order_relaxed));
		snapshot.contributors = runtime.contributors.load(std::memory_order_relaxed);
		snapshot.defines = runtime.defines;
		snapshot.compiledSha1 = runtime.compiledSha1;
		snapshot.compileError = runtime.compileError;
		{
			SwapCountersGuard counterGuard(service);
			snapshot.matches = runtime.matches.load(std::memory_order_relaxed);
			snapshot.substitutions =
				runtime.substitutions.load(std::memory_order_relaxed);
			snapshot.passthroughCompileFail =
				runtime.passthroughCompileFail.load(
					std::memory_order_relaxed);
			snapshot.passthroughNotReady =
				runtime.passthroughNotReady.load(
					std::memory_order_relaxed);
			snapshot.passthroughDisabled =
				runtime.passthroughDisabled.load(
					std::memory_order_relaxed);
		}
		snapshot.dispatches = runtime.dispatches.load(std::memory_order_relaxed);
		return snapshot;
	}

	ShaderInjectionSummary GetShaderInjectionSummary() noexcept
	{
		ShaderInjectionSummary summary;
		auto& service = GetService();
		SwapCountersGuard counterGuard(service);
		for (const auto& runtime : service.runtime) {
			if (runtime.requested.load(std::memory_order_relaxed))
				++summary.requested;
			if (runtime.compileAttempted.load(std::memory_order_relaxed))
				++summary.compileAttempted;
			if (runtime.compileOk.load(std::memory_order_relaxed))
				++summary.compiled;
			if (runtime.compileComplete.load(std::memory_order_relaxed))
				++summary.compileComplete;
			if (runtime.swappable.load(std::memory_order_relaxed))
				++summary.swappable;
			const auto requestReasons =
				static_cast<ShaderInjectionRequestReason>(
					runtime.requestReasons.load(
						std::memory_order_relaxed));
			if (HasShaderInjectionRequestReason(
					requestReasons,
					ShaderInjectionRequestReason::
						kFeatureContributor)) {
				++summary.requestedByFeatureContributor;
			}
			if (HasShaderInjectionRequestReason(
					requestReasons,
					ShaderInjectionRequestReason::
						kBaselineOwnership)) {
				++summary.requestedByBaselineOwnership;
			}
			if (HasShaderInjectionRequestReason(
					requestReasons,
					ShaderInjectionRequestReason::
						kDeveloperForceOn)) {
				++summary.requestedByDeveloperForceOn;
			}
			summary.matches += runtime.matches.load(std::memory_order_relaxed);
			summary.substitutions += runtime.substitutions.load(std::memory_order_relaxed);
			summary.passthroughCompileFail +=
				runtime.passthroughCompileFail.load(
					std::memory_order_relaxed);
			summary.passthroughNotReady +=
				runtime.passthroughNotReady.load(
					std::memory_order_relaxed);
			summary.passthroughDisabled +=
				runtime.passthroughDisabled.load(
					std::memory_order_relaxed);
			summary.dispatches += runtime.dispatches.load(std::memory_order_relaxed);
		}
		return summary;
	}

}

#include "Render/ShaderInjection.h"

#include "Log.h"
#include "LogThrottle.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderVariantCompilation.h"
#include "Render/ShaderVariantResolver.h"
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
	namespace
	{
		constexpr std::array<ShaderInjectionDefineMetadata, 0> kNoDefines{};

		// Routes must reconstruct the stock blob they replace.
		constexpr std::array<ShaderInjectionDefineMetadata, 5> kBsdfPointDefines{ {
			{ "DIRSPLITS", "2" },
			{ "GOBOPROJECTION", "1" },
			{ "POINTOMNI", "1" },
			{ "RGBSPEC", "1" },
			{ "SPECULAR", "1" }
		} };
		constexpr std::array<ShaderInjectionDefineMetadata, 7> kBsdfDirectionalDefines{ {
			{ "BLENDSPLIT", "1" },
			{ "DIRECTIONAL", "1" },
			{ "DIRSPLITS", "2" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" }
		} };
		constexpr std::array<ShaderInjectionDefineMetadata, 8> kBsdfDirectionalIblDefines{ {
			{ "AMBIENT", "1" },
			{ "BLENDSPLIT", "1" },
			{ "DIRECTIONAL", "1" },
			{ "DIRSPLITS", "2" },
			{ "FILTER_POISSON", "1" },
			{ "RGBSPEC", "1" },
			{ "SHADOW", "1" },
			{ "SPECULAR", "1" }
		} };
		// Family 2 matches the runtime ambient pass.
		constexpr std::array<ShaderInjectionDefineMetadata, 1> kAmbientIblDefines{ {
			{ "BSDF_COMPOSITE_FAMILY", "2" }
		} };

		constexpr std::array<ShaderInjectionTargetMetadata,
			static_cast<std::size_t>(ShaderInjectionTarget::kCount)> kTargets{ {
			{
				ShaderInjectionTarget::kDeferredComposite,
				"deferred_composite",
				L"lighting/deferred_composite.hlsl",
				"main",
				"ps_5_0",
				kNoDefines
			},
			{
				ShaderInjectionTarget::kDeferredPrepass,
				"deferred_prepass",
				L"lighting/deferred_prepass.hlsl",
				"main",
				"ps_5_0",
				kNoDefines
			},
			{
				ShaderInjectionTarget::kBsdfLightDeferredPoint,
				"bsdf_light_deferred_point",
				L"lighting/bsdf_light_deferred_gobo.hlsl",
				"main",
				"ps_5_0",
				kBsdfPointDefines
			},
			{
				ShaderInjectionTarget::kAmbientIblPass,
				"ambient_ibl_pass",
				L"lighting/BSDFComposite.hlsl",
				"main",
				"ps_5_0",
				kAmbientIblDefines
			},
			{
				ShaderInjectionTarget::kBsdfLightDeferredDirectional,
				"bsdf_light_deferred_directional",
				L"lighting/bsdf_light_deferred_dirsplits2.hlsl",
				"main",
				"ps_5_0",
				kBsdfDirectionalDefines
			},
			{
				ShaderInjectionTarget::kBsdfLightDeferredDirectionalIbl,
				"bsdf_light_deferred_directional_ibl",
				L"lighting/bsdf_light_deferred_dirsplits2.hlsl",
				"main",
				"ps_5_0",
				kBsdfDirectionalIblDefines
			},
			{
				ShaderInjectionTarget::kVlsSliceScatter,
				"vls_slice_scatter",
				L"lighting/vls_slice_scatter.hlsl",
				"main",
				"ps_5_0",
				kNoDefines
			}
		} };

		// Composite and VLS lack stock hash guards.
		constexpr std::array<ShaderInjectionTarget, 5>
			kBaselineOwnableTargets{
				ShaderInjectionTarget::kDeferredPrepass,
				ShaderInjectionTarget::kBsdfLightDeferredPoint,
				ShaderInjectionTarget::kAmbientIblPass,
				ShaderInjectionTarget::kBsdfLightDeferredDirectional,
				ShaderInjectionTarget::kBsdfLightDeferredDirectionalIbl
			};

		ShaderReplacementVariantRegistration
			MakeDefaultVariantRegistration(
				ShaderInjectionTarget a_target,
				std::string a_name,
				std::vector<ShaderVariantKey> a_variantKeys,
				std::string a_expectedStockSha1,
				ShaderInjectionDefines a_defines = {})
		{
			const auto& metadata =
				kTargets[static_cast<std::size_t>(a_target)];
			ShaderReplacementVariantRegistration registration;
			registration.targetId = a_target;
			registration.name = std::move(a_name);
			registration.variantKeys = std::move(a_variantKeys);
			registration.expectedStockSha1 =
				std::move(a_expectedStockSha1);
			registration.compilation.sourcePath =
				std::wstring(metadata.sourcePath);
			registration.compilation.entryPoint =
				std::string(metadata.entryPoint);
			registration.compilation.profile =
				std::string(metadata.profile);
			registration.compilation.defines =
				std::move(a_defines);
			return registration;
		}

		template <std::size_t Size>
		std::vector<ShaderVariantKey> OwnVariantKeys(
			const std::array<ShaderVariantKeyView, Size>& a_keys)
		{
			std::vector<ShaderVariantKey> keys;
			keys.reserve(a_keys.size());
			for (const auto key : a_keys)
				keys.push_back(OwnShaderVariantKey(key));
			return keys;
		}

		std::vector<ShaderReplacementVariantRegistration>
		MakeDefaultShaderReplacementVariants()
		{
			using Target = ShaderInjectionTarget;
			return {
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
					Target::kBsdfLightDeferredPoint,
					"default",
					OwnVariantKeys(
						shader_variants::
							kBsdfLightDeferredPoint),
					"9969e800683c8a7c8afc25f41582415d79cbe47e"),
				MakeDefaultVariantRegistration(
					Target::kAmbientIblPass,
					"tilelight",
					{
						OwnShaderVariantKey(
							shader_variants::
								kBsdfCompositeAmbientIblTilelight)
					},
					"2b6e36c08aca7ff0a3bd10da326e00b3b0367383",
					{ { "TILELIGHT", "1" } }),
				MakeDefaultVariantRegistration(
					Target::kAmbientIblPass,
					"no-tilelight",
					{
						OwnShaderVariantKey(
							shader_variants::
								kBsdfCompositeAmbientIbl)
					},
					"6d726d0fe6b6c474da30edbffcecfa067c795873"),
				MakeDefaultVariantRegistration(
					Target::kBsdfLightDeferredDirectional,
					"default",
					OwnVariantKeys(
						shader_variants::
							kBsdfLightDeferredDirectional),
					"50e2618e8d1a8c3400c2bdb0129e510fe395d19a"),
				MakeDefaultVariantRegistration(
					Target::kBsdfLightDeferredDirectionalIbl,
					"default",
					OwnVariantKeys(
						shader_variants::
							kBsdfLightDeferredDirectionalIbl),
					"94f8385edd1b4eb232b1de269e1ad7b21122a293"),
				MakeDefaultVariantRegistration(
					Target::kVlsSliceScatter,
					"default",
					{},
					{})
			};
		}

		constexpr auto kDefaultShaderRoot =
			L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Shaders";
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
			const ShaderInjectionDefines& a_defines,
			std::string_view& a_conflictingName,
			std::string_view& a_existingValue)
		{
			for (const auto& [name, value] : a_registration.defines) {
				const auto existing = a_defines.find(name);
				if (existing != a_defines.end() && existing->second != value) {
					a_conflictingName = name;
					a_existingValue = existing->second;
					return true;
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
							target.defines,
							conflictingName,
							existingValue)) {
						L->error(
							"Contributor '{}' for '{}' conflicts on {}={} (requested {}); contributor dropped.",
							ContributorName(registration, registrationIndex),
							metadata.name,
							conflictingName,
							existingValue,
							registration.defines.find(conflictingName)->second);
						continue;
					}

					if (const auto collision = FindSlotCollision(registration, claimedSlots)) {
						L->error(
							"Slot collision on '{}' (stage={}, type={}, slot={}) from '{}'; target quarantined.",
							metadata.name,
							static_cast<unsigned>(collision->stage),
							static_cast<unsigned>(collision->resourceType),
							collision->slot,
							ContributorName(registration, registrationIndex));
						target.slotCollision = true;
						break;
					}

					requestReasons |=
						ShaderInjectionRequestReason::kFeatureContributor;
					++target.contributors;
					target.defines.insert(registration.defines.begin(), registration.defines.end());
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
					.routeGroup = ToIndex(a_variant.targetId)
				});
				return keys;
			}
			for (const auto& variantKey : a_variant.variantKeys) {
				keys.push_back({
					.variant = variantKey,
					.expectedStockSha1 = expected,
					.routeGroup = ToIndex(a_variant.targetId)
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

			const auto sourcePath = ResolveSourcePath(
				a_variant.compilation.sourcePath,
				runtime.developerOverride,
				a_developerSourceRoot);
			auto mergedDefines = a_target.defines;
			for (const auto& [name, value] :
				a_variant.compilation.defines) {
				const auto [it, inserted] =
					mergedDefines.emplace(name, value);
				if (!inserted && it->second != value) {
					a_error =
						"variant define conflicts with target define: "
						+ name;
					L->error(
						"Compile '{}/{}' rejected: {}",
						a_target.metadata->name,
						a_variant.name,
						a_error);
					return std::nullopt;
				}
			}

			ShaderVariantCompilationRequest request;
			request.device.copy_from(a_device);
			request.sourcePath = sourcePath;
			request.entryPoint = a_variant.compilation.entryPoint;
			request.profile = a_variant.compilation.profile;
			request.defines.reserve(mergedDefines.size());
			for (const auto& define : mergedDefines)
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
					"Compile '{}/{}' ok: {} bytes, dxbc-sha1={}",
					a_target.metadata->name,
					a_variant.name,
					result.bytecodeSize,
					result.compiledSha1);
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
						&& a_variant.compilation->PeekPixelShader()
							== a_shader;
				});
		}

		PixelShaderSwapResolverResult ResolveInjectedPixelShader(
			const PixelShaderSwapRequest& a_request) noexcept
		{
			try {
				const auto& a_resolvedVariant = a_request.variant;
				const auto& a_sha = a_request.stockSha1;
				const auto plan =
					GetService().published.load(std::memory_order_acquire);
				if (!plan)
					return PixelShaderSwapResolverResult::kNoMatch;

				const auto selection = SelectPixelShaderSwapVariant(
					plan->variantKeys,
					a_resolvedVariant,
					a_sha);
				if (selection.kind
					== PixelShaderSwapSelectionKind::kNoMatch) {
					return PixelShaderSwapResolverResult::kNoMatch;
				}
				if (selection.kind
					== PixelShaderSwapSelectionKind::kUnmappedVariant) {
					CS_LOG_EVERY_MS(
						L,
						2000,
						spdlog::level::warn,
						"Kept stock PS sha={}: no replacement registered "
						"for variant {}:{}+0x{:X}.",
						sha1::Sha1ToHex(a_sha),
						a_resolvedVariant
							? a_resolvedVariant->subclass
							: "<none>",
						a_resolvedVariant
							&& a_resolvedVariant->stage
								== ShaderStage::kPixel
							? "ps"
							: "vs",
						a_resolvedVariant
							? a_resolvedVariant->id.Value()
							: 0);
					return PixelShaderSwapResolverResult::kKeepStock;
				}
				if (selection.routeIndex
					>= plan->variantKeys.size()
					|| selection.replacementIndex
						>= plan->variants.size()) {
					return PixelShaderSwapResolverResult::kKeepStock;
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
						"Refused PS replacement '{}/{}': "
						"variant {}:{}+0x{:X} expected sha={} but received {}.",
						kTargets[ToIndex(variant.targetId)].name,
						variant.name,
						a_resolvedVariant
							? a_resolvedVariant->subclass
							: "<none>",
						a_resolvedVariant
							&& a_resolvedVariant->stage
								== ShaderStage::kPixel
							? "ps"
							: "vs",
						a_resolvedVariant
							? a_resolvedVariant->id.Value()
							: 0,
						expected,
						sha1::Sha1ToHex(a_sha));
					return PixelShaderSwapResolverResult::kKeepStock;
				}

				if (!runtime.requested.load(std::memory_order_relaxed)) {
					RecordMatchedShaderOutcome(
						variant.targetId,
						MatchedShaderOutcome::kDisabled);
					return PixelShaderSwapResolverResult::kKeepStock;
				}
				auto replacement = variant.compilation
					? variant.compilation->AcquireOrRequest()
					: winrt::com_ptr<ID3D11PixelShader>{};
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
					return PixelShaderSwapResolverResult::kKeepStock;
				}

				if (!a_request.output || !*a_request.output) {
					RecordMatchedShaderOutcome(
						variant.targetId,
						MatchedShaderOutcome::kKeptStock);
					return PixelShaderSwapResolverResult::kKeepStock;
				}
				(*a_request.output)->Release();
				*a_request.output = replacement.detach();
				const auto counts = RecordMatchedShaderOutcome(
					variant.targetId,
					MatchedShaderOutcome::kReplaced);
				if (counts.targetSubstitutions == 1) {
					L->info(
						"Replaced PS sha={} -> {}/{} (target_replacements=1, total_replacements={})",
						sha1::Sha1ToHex(a_sha),
						kTargets[ToIndex(variant.targetId)].name,
						variant.name,
						counts.totalSubstitutions);
				} else if (L->should_log(spdlog::level::debug)) {
					CS_LOG_EVERY_MS(
						L,
						2000,
						spdlog::level::debug,
						"Pixel-shader replacements: target={}/{} target_total={} total={}.",
						kTargets[ToIndex(variant.targetId)].name,
						variant.name,
						counts.targetSubstitutions,
						counts.totalSubstitutions);
				}
				return PixelShaderSwapResolverResult::kReplaced;
			} catch (const std::exception& e) {
				CS_LOG_EVERY_MS(
					L,
					2000,
					spdlog::level::err,
					"Pixel-shader replacement resolution failed: {}.",
					e.what());
				return PixelShaderSwapResolverResult::kKeepStock;
			} catch (...) {
				CS_LOG_EVERY_MS(
					L,
					2000,
					spdlog::level::err,
					"Pixel-shader replacement resolution failed.");
				return PixelShaderSwapResolverResult::kKeepStock;
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
		{
			std::scoped_lock lock(service.mutex);
			if (service.lifecycle != Lifecycle::kCollecting) {
				LogLateMutation("Replacement registration");
				return false;
			}
			if (!IsValidTarget(a_registration.targetId)) {
				L->error("Replacement registration rejected: unknown target.");
				return false;
			}
			if (RegistrationHasDuplicateClaims(a_registration)) {
				L->error(
					"Replacement registration '{}' for '{}' rejected: duplicate slot claim.",
					a_registration.contributor,
					kTargets[ToIndex(a_registration.targetId)].name);
				return false;
			}

			service.registrations.push_back(std::move(a_registration));
		}

		if (installsPreDrawHook)
			EnsurePreSunLightDrawInstalled();
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
		for (const auto& key : a_registration.variantKeys) {
			if (key.subclass.empty()) {
				L->error(
					"Replacement variant '{}/{}' rejected: "
					"empty shader subclass.",
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
		if (a_registration.compilation.profile.empty()) {
			L->error(
				"Replacement variant '{}/{}' rejected: "
				"empty shader profile.",
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
					duplicate.stage == ShaderStage::kPixel
						? "ps"
						: "vs",
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
						newKey.stage == ShaderStage::kPixel
							? "ps"
							: "vs",
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
			CreateEagerShaderVariantCompilationPolicy();
		std::size_t compileRequested = 0;
		std::size_t compileSucceeded = 0;
		std::size_t swappableVariants = 0;
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
				if (frozenTarget.slotCollision)
					continue;

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
			const bool hasSwappableTarget = std::ranges::any_of(
				plan->variantKeys,
				[](const PixelShaderSwapVariantKey& a_key) {
					return a_key.variant.has_value()
						|| a_key.expectedStockSha1.has_value();
				});
			if (hasSwappableTarget && !service.resolverRegistered) {
				service.resolverRegistered =
					RegisterPixelShaderSwapResolver(&ResolveInjectedPixelShader);
				if (service.resolverRegistered) {
					L->info(
						"Registered HLSL pixel-shader swap resolver (broker hook={}).",
						PixelShaderSwapBrokerHooksInstalled() ? "present" : "absent");
				} else {
					L->error(
						"Pixel-shader swap resolver registration failed; all targets remain stock.");
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
			"Shader injection freeze: targets requested={} compile_attempted={} compiled_ok={} swappable={} reasons(feature_contributor={}, baseline_ownership={}, developer_force_on={}); replacement_variants attempted={} compiled_ok={} swappable={}.",
			summary.requested,
			summary.compileAttempted,
			summary.compiled,
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
			if (target.binds.empty())
				continue;
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
			if (current->compilation) {
				if (auto* shader =
						current->compilation->PeekPixelShader()) {
					return shader;
				}
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

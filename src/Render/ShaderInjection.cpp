#include "Render/ShaderInjection.h"

#include "Log.h"
#include "LogThrottle.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Utils/CSSha1.h"
#include "Utils/ShaderCompile.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <d3d11.h>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <winrt/base.h>

namespace cs::engine
{
	namespace
	{
		constexpr std::array<ShaderInjectionDefineMetadata, 0> kNoDefines{};

		constexpr std::array<ShaderInjectionDefineMetadata, 1> kBsdfPointDefines{ {
			{ "LIGHT_TYPE", "2" }
		} };
		constexpr std::array<ShaderInjectionDefineMetadata, 1> kBsdfDirectionalDefines{ {
			{ "LIGHT_TYPE", "1" }
		} };
		constexpr std::array<ShaderInjectionDefineMetadata, 2> kBsdfDirectionalIblDefines{ {
			{ "AMBIENT_IBL_IN_LIGHT", "1" },
			{ "LIGHT_TYPE", "1" }
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
				L"lighting/bsdf_light_deferred.hlsl",
				"main",
				"ps_5_0",
				kBsdfPointDefines
			},
			{
				ShaderInjectionTarget::kAmbientIblPass,
				"ambient_ibl_pass",
				L"lighting/ambient_ibl_pass_runtime.hlsl",
				"main",
				"ps_5_0",
				kNoDefines
			},
			{
				ShaderInjectionTarget::kBsdfLightDeferredDirectional,
				"bsdf_light_deferred_directional",
				L"lighting/bsdf_light_deferred.hlsl",
				"main",
				"ps_5_0",
				kBsdfDirectionalDefines
			},
			{
				ShaderInjectionTarget::kBsdfLightDeferredDirectionalIbl,
				"bsdf_light_deferred_directional_ibl",
				L"lighting/bsdf_light_deferred.hlsl",
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

		std::vector<ShaderReplacementVariantRegistration>
		DefaultVariantRegistrations()
		{
			using Target = ShaderInjectionTarget;
			return {
				{
					Target::kDeferredComposite,
					"default",
					std::nullopt,
					{},
					{}
				},
				{
					Target::kDeferredPrepass,
					"default",
					std::nullopt,
					"c493970c042ccd90363c57596ff53f6fdd22ce5f",
					{}
				},
				{
					Target::kBsdfLightDeferredPoint,
					"default",
					std::nullopt,
					"9969e800683c8a7c8afc25f41582415d79cbe47e",
					{}
				},
				{
					Target::kAmbientIblPass,
					"tilelight",
					PixelShaderTechnique{
						"BSDFCompositeShader",
						0x10B60
					},
					"2b6e36c08aca7ff0a3bd10da326e00b3b0367383",
					{}
				},
				{
					Target::kBsdfLightDeferredDirectional,
					"default",
					std::nullopt,
					"50e2618e8d1a8c3400c2bdb0129e510fe395d19a",
					{}
				},
				{
					Target::kBsdfLightDeferredDirectionalIbl,
					"default",
					std::nullopt,
					"94f8385edd1b4eb232b1de269e1ad7b21122a293",
					{}
				},
				{
					Target::kVlsSliceScatter,
					"default",
					std::nullopt,
					{},
					{}
				}
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
			std::atomic<std::size_t>   contributors{ 0 };
			std::atomic<std::uint64_t> matches{ 0 };
			std::atomic<std::uint64_t> substitutions{ 0 };
			std::atomic<std::uint64_t> passthroughCompileFail{ 0 };
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
			winrt::com_ptr<ID3D11PixelShader> shader;
		};

		struct CompiledVariant
		{
			PublishedVariant variant;
			PixelShaderSwapVariantKey key;
			std::string compiledSha1;
		};

		struct PublishedTarget
		{
			ShaderInjectionTarget id = ShaderInjectionTarget::kCount;
			std::vector<ShaderInjectionBindCallback> binds;
		};

		struct PublishedPlan
		{
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
			std::array<DeveloperShaderOverride,
				static_cast<std::size_t>(ShaderInjectionTarget::kCount)> developerOverrides{};
			std::vector<ShaderReplacementRegistration> registrations;
			std::vector<ShaderReplacementVariantRegistration>
				variantRegistrations = DefaultVariantRegistrations();
			std::array<TargetRuntimeState,
				static_cast<std::size_t>(ShaderInjectionTarget::kCount)> runtime;
			std::atomic<std::shared_ptr<const PublishedPlan>> published;
			bool resolverRegistered = false;
		};

		Service& GetService()
		{
			static Service service;
			return service;
		}

		constexpr std::size_t ToIndex(ShaderInjectionTarget a_target)
		{
			return static_cast<std::size_t>(a_target);
		}

		bool IsValidTarget(ShaderInjectionTarget a_target)
		{
			return ToIndex(a_target) < kTargets.size();
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

		bool RegistrationHasDuplicateClaims(const ShaderReplacementRegistration& a_registration)
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

		std::optional<ShaderSlotClaim> FindSlotCollision(
			const ShaderReplacementRegistration& a_registration,
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
				bool requested = developerOverride == DeveloperShaderOverride::kForceOn;
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

					requested = true;
					++target.contributors;
					target.defines.insert(registration.defines.begin(), registration.defines.end());
					claimedSlots.insert(
						claimedSlots.end(),
						registration.slotClaims.begin(),
						registration.slotClaims.end());
					if (registration.bind)
						target.binds.push_back(registration.bind);
				}

				if (developerOverride == DeveloperShaderOverride::kForceOff)
					requested = false;

				for (const auto& variant : a_variantRegistrations) {
					if (variant.targetId == metadata.id)
						target.variants.push_back(variant);
				}

				auto& runtime = GetService().runtime[targetIndex];
				runtime.requested.store(requested, std::memory_order_relaxed);
				runtime.slotCollision.store(target.slotCollision, std::memory_order_relaxed);
				runtime.contributors.store(target.contributors, std::memory_order_relaxed);
				runtime.developerOverride = developerOverride;
				runtime.defines = target.defines;

				if (requested)
					frozen.push_back(std::move(target));
			}
			return frozen;
		}

		std::filesystem::path ResolveSourcePath(
			const ShaderInjectionTargetMetadata& a_metadata,
			DeveloperShaderOverride a_developerOverride,
			const std::wstring& a_developerSourceRoot)
		{
			const bool useDeveloperRoot =
				a_developerOverride == DeveloperShaderOverride::kForceOn
				&& !a_developerSourceRoot.empty();
			const std::filesystem::path root =
				useDeveloperRoot ? a_developerSourceRoot : kDefaultShaderRoot;
			return root / a_metadata.sourcePath;
		}

		std::optional<PixelShaderSwapVariantKey> BuildVariantKey(
			const ShaderReplacementVariantRegistration& a_variant)
		{
			PixelShaderSwapVariantKey key;
			key.technique = a_variant.technique;
			key.routeGroup = ToIndex(a_variant.targetId);
			if (a_variant.expectedStockSha1.empty())
				return key;

			sha1::Sha1Result expected;
			if (!sha1::Sha1FromHex(
					a_variant.expectedStockSha1, expected)) {
				return std::nullopt;
			}
			key.expectedStockSha1 = expected;
			return key;
		}

		std::optional<CompiledVariant> CompileVariant(
			ID3D11Device* a_device,
			const FrozenTarget& a_target,
			const ShaderReplacementVariantRegistration& a_variant,
			const std::wstring& a_developerSourceRoot,
			std::string& a_error)
		{
			const auto targetIndex = ToIndex(a_target.metadata->id);
			auto& runtime = GetService().runtime[targetIndex];
			const auto key = BuildVariantKey(a_variant);
			if (!key) {
				a_error = "invalid expected stock SHA1";
				L->error(
					"Compile '{}/{}' rejected: {}",
					a_target.metadata->name,
					a_variant.name,
					a_error);
				return std::nullopt;
			}

			const auto sourcePath = ResolveSourcePath(
				*a_target.metadata,
				runtime.developerOverride,
				a_developerSourceRoot);
			auto mergedDefines = a_target.defines;
			for (const auto& [name, value] : a_variant.defines) {
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

			std::vector<std::pair<const char*, const char*>> defines;
			defines.reserve(mergedDefines.size());
			for (const auto& [name, value] : mergedDefines)
				defines.emplace_back(name.c_str(), value.c_str());

			std::string compileError;
			const auto blob = util::CompileShaderToBlob(
				sourcePath.c_str(),
				defines,
				a_target.metadata->profile.data(),
				a_target.metadata->entryPoint.data(),
				&compileError);
			if (!blob) {
				a_error = compileError.empty()
					? "shader compilation failed"
					: std::move(compileError);
				L->error(
					"Compile '{}/{}' failed ({}): {}",
					a_target.metadata->name,
					a_variant.name,
					sourcePath.string(),
					a_error);
				return std::nullopt;
			}

			winrt::com_ptr<ID3D11PixelShader> shader;
			HRESULT result = E_FAIL;
			{
				ScopedPixelShaderBrokerBypass bypassBroker;
				result = a_device->CreatePixelShader(
					blob->GetBufferPointer(),
					blob->GetBufferSize(),
					nullptr,
					shader.put());
			}
			if (FAILED(result) || !shader) {
				char buffer[64]{};
				std::snprintf(
					buffer,
					sizeof(buffer),
					"CreatePixelShader hr=0x%08x",
					static_cast<unsigned>(result));
				a_error = buffer;
				L->error(
					"Compile '{}/{}' failed: {}",
					a_target.metadata->name,
					a_variant.name,
					a_error);
				return std::nullopt;
			}

			const auto compiledSha1 = sha1::Sha1Compute(
				blob->GetBufferPointer(),
				blob->GetBufferSize());
			const auto compiledSha1Hex = sha1::Sha1ToHex(compiledSha1);

			L->info(
				"Compile '{}/{}' ok: {} bytes, dxbc-sha1={}",
				a_target.metadata->name,
				a_variant.name,
				static_cast<std::size_t>(blob->GetBufferSize()),
				compiledSha1Hex);

			CompiledVariant compiled;
			compiled.variant.targetId = a_target.metadata->id;
			compiled.variant.name = a_variant.name;
			compiled.variant.shader = std::move(shader);
			compiled.key = *key;
			compiled.compiledSha1 = compiledSha1Hex;
			return compiled;
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

		bool ResolveInjectedPixelShader(
			const void*,
			std::size_t,
			std::optional<PixelShaderTechniqueView> a_technique,
			const sha1::Sha1Result& a_sha,
			ID3D11PixelShader** a_out) noexcept
		{
			try {
				const auto plan =
					GetService().published.load(std::memory_order_acquire);
				if (!plan)
					return false;

				const auto selection = SelectPixelShaderSwapVariant(
					plan->variantKeys,
					a_technique,
					a_sha);
				if (selection.kind
					== PixelShaderSwapSelectionKind::kNoMatch) {
					return false;
				}
				if (selection.variantIndex >= plan->variants.size())
					return false;

				const auto& variant =
					plan->variants[selection.variantIndex];
				auto& runtime =
					GetService().runtime[ToIndex(variant.targetId)];
				if (selection.kind
					== PixelShaderSwapSelectionKind::kHashMismatch) {
					const auto& key =
						plan->variantKeys[selection.variantIndex];
					const auto expected = key.expectedStockSha1
						? sha1::Sha1ToHex(*key.expectedStockSha1)
						: std::string("<unknown>");
					CS_LOG_EVERY_MS(
						L,
						2000,
						spdlog::level::err,
						"Refused PS replacement '{}/{}': "
						"technique {}+0x{:X} expected sha={} but received {}.",
						kTargets[ToIndex(variant.targetId)].name,
						variant.name,
						a_technique ? a_technique->subclass : "<none>",
						a_technique ? a_technique->techniqueBits : 0,
						expected,
						sha1::Sha1ToHex(a_sha));
					return false;
				}

				runtime.matches.fetch_add(1, std::memory_order_relaxed);
				if (!runtime.requested.load(std::memory_order_relaxed)) {
					runtime.passthroughDisabled.fetch_add(1, std::memory_order_relaxed);
					return false;
				}
				if (!variant.shader) {
					runtime.passthroughCompileFail.fetch_add(1, std::memory_order_relaxed);
					return false;
				}

				ID3D11PixelShader* replacement = variant.shader.get();
				replacement->AddRef();
				(*a_out)->Release();
				*a_out = replacement;
				const auto previous = runtime.substitutions.fetch_add(1, std::memory_order_relaxed);
				if (previous == 0) {
					L->info(
						"Replaced PS sha={} -> {}/{}",
						sha1::Sha1ToHex(a_sha),
						kTargets[ToIndex(variant.targetId)].name,
						variant.name);
				}
				return true;
			} catch (const std::exception& e) {
				CS_LOG_EVERY_MS(
					L,
					2000,
					spdlog::level::err,
					"Pixel-shader replacement resolution failed: {}.",
					e.what());
				return false;
			} catch (...) {
				CS_LOG_EVERY_MS(
					L,
					2000,
					spdlog::level::err,
					"Pixel-shader replacement resolution failed.");
				return false;
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

	bool RegisterReplacement(ShaderReplacementRegistration a_registration)
	{
		auto& service = GetService();
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
		return true;
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
		if (a_registration.technique
			&& a_registration.technique->subclass.empty()) {
			L->error(
				"Replacement variant '{}/{}' rejected: "
				"empty shader subclass.",
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
			if (existing.technique && a_registration.technique
				&& existing.technique->subclass
					== a_registration.technique->subclass
				&& existing.technique->techniqueBits
					== a_registration.technique->techniqueBits) {
				L->error(
					"Replacement variant '{}/{}' rejected: "
					"duplicate technique {}+0x{:X}.",
					kTargets[ToIndex(a_registration.targetId)].name,
					a_registration.name,
					a_registration.technique->subclass,
					a_registration.technique->techniqueBits);
				return false;
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
			developerOverrides = service.developerOverrides;
			developerSourceRoot = service.developerSourceRoot;
			registrations = service.registrations;
			variantRegistrations = service.variantRegistrations;
		}

		sha1::Sha1InitOnce();
		auto plan = std::make_shared<PublishedPlan>();
		std::size_t compileRequested = 0;
		std::size_t compileSucceeded = 0;

		if (!enabled) {
			L->warn("Shader injection disabled by core kill switch.");
		} else if (!a_device) {
			L->error("Shader injection freeze failed: no D3D11 device.");
		} else {
			auto frozenTargets = FreezeTargets(
				registrations,
				variantRegistrations,
				developerForceOffEnabled,
				developerOverrides);
			plan->targets.reserve(frozenTargets.size());
			plan->variants.reserve(variantRegistrations.size());
			plan->variantKeys.reserve(variantRegistrations.size());
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

				std::size_t targetCompiled = 0;
				bool targetSwappable = false;
				std::string firstError;
				std::string onlyCompiledSha1;
				for (const auto& variant : frozenTarget.variants) {
					std::string compileError;
					auto compiled = CompileVariant(
						a_device,
						frozenTarget,
						variant,
						developerSourceRoot,
						compileError);
					if (!compiled) {
						if (firstError.empty())
							firstError = std::move(compileError);
						continue;
					}

					++compileSucceeded;
					++targetCompiled;
					onlyCompiledSha1 = compiled->compiledSha1;
					targetSwappable = targetSwappable
						|| compiled->key.technique.has_value()
						|| compiled->key.expectedStockSha1.has_value();
					plan->variantKeys.push_back(
						std::move(compiled->key));
					plan->variants.push_back(
						std::move(compiled->variant));
				}

				runtime.compileOk.store(
					targetCompiled > 0,
					std::memory_order_release);
				runtime.swappable.store(
					targetSwappable,
					std::memory_order_release);
				runtime.compileError =
					targetCompiled == frozenTarget.variants.size()
					? std::string{}
					: std::move(firstError);
				if (targetCompiled == 1) {
					runtime.compiledSha1 =
						std::move(onlyCompiledSha1);
				} else if (targetCompiled > 1) {
					runtime.compiledSha1 =
						std::to_string(targetCompiled)
						+ " variants";
				}

				if (targetCompiled > 0) {
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
					return a_key.technique.has_value()
						|| a_key.expectedStockSha1.has_value();
				});
			if (hasSwappableTarget && !service.resolverRegistered) {
				service.resolverRegistered =
					RegisterPixelShaderSwapResolver(&ResolveInjectedPixelShader);
				if (service.resolverRegistered) {
					L->info(
						"Registered sole pixel-shader swap resolver (broker hook={}).",
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
		} else {
			L->info("Compiled {}/{} replacements", compileSucceeded, compileRequested);
		}
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
		if (runtime.substitutions.load(std::memory_order_relaxed) == 0)
			return;

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
		return variant == plan->variants.end()
			? nullptr
			: variant->shader.get();
	}

	bool IsInjectedPixelShader(
		ShaderInjectionTarget a_target,
		ID3D11PixelShader* a_shader) noexcept
	{
		if (!a_shader || !IsValidTarget(a_target))
			return false;
		const auto plan =
			GetService().published.load(std::memory_order_acquire);
		return plan && std::ranges::any_of(
			plan->variants,
			[a_target, a_shader](const PublishedVariant& a_variant) {
				return a_variant.targetId == a_target
					&& a_variant.shader.get() == a_shader;
			});
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
		snapshot.contributors = runtime.contributors.load(std::memory_order_relaxed);
		snapshot.defines = runtime.defines;
		snapshot.compiledSha1 = runtime.compiledSha1;
		snapshot.compileError = runtime.compileError;
		snapshot.matches = runtime.matches.load(std::memory_order_relaxed);
		snapshot.substitutions = runtime.substitutions.load(std::memory_order_relaxed);
		snapshot.dispatches = runtime.dispatches.load(std::memory_order_relaxed);
		return snapshot;
	}

	ShaderInjectionSummary GetShaderInjectionSummary() noexcept
	{
		ShaderInjectionSummary summary;
		for (const auto& runtime : GetService().runtime) {
			if (runtime.requested.load(std::memory_order_relaxed))
				++summary.requested;
			if (runtime.compileOk.load(std::memory_order_relaxed))
				++summary.compiled;
			summary.matches += runtime.matches.load(std::memory_order_relaxed);
			summary.substitutions += runtime.substitutions.load(std::memory_order_relaxed);
			summary.dispatches += runtime.dispatches.load(std::memory_order_relaxed);
		}
		return summary;
	}
}

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
		constexpr std::array<std::string_view, 0> kNoStockSha1s{};
		constexpr std::array<ShaderInjectionDefineMetadata, 0> kNoDefines{};

		constexpr std::array<std::string_view, 1> kDeferredPrepassSha1s{
			"c493970c042ccd90363c57596ff53f6fdd22ce5f"
		};
		constexpr std::array<std::string_view, 1> kBsdfPointSha1s{
			"9969e800683c8a7c8afc25f41582415d79cbe47e"
		};
		constexpr std::array<std::string_view, 1> kAmbientIblSha1s{
			"2b6e36c08aca7ff0a3bd10da326e00b3b0367383"
		};
		constexpr std::array<std::string_view, 1> kBsdfDirectionalSha1s{
			"50e2618e8d1a8c3400c2bdb0129e510fe395d19a"
		};
		constexpr std::array<std::string_view, 1> kBsdfDirectionalIblSha1s{
			"94f8385edd1b4eb232b1de269e1ad7b21122a293"
		};

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
				kNoStockSha1s,
				kNoDefines
			},
			{
				ShaderInjectionTarget::kDeferredPrepass,
				"deferred_prepass",
				L"lighting/deferred_prepass.hlsl",
				"main",
				"ps_5_0",
				kDeferredPrepassSha1s,
				kNoDefines
			},
			{
				ShaderInjectionTarget::kBsdfLightDeferredPoint,
				"bsdf_light_deferred_point",
				L"lighting/bsdf_light_deferred.hlsl",
				"main",
				"ps_5_0",
				kBsdfPointSha1s,
				kBsdfPointDefines
			},
			{
				ShaderInjectionTarget::kAmbientIblPass,
				"ambient_ibl_pass",
				L"lighting/ambient_ibl_pass.hlsl",
				"main",
				"ps_5_0",
				kAmbientIblSha1s,
				kNoDefines
			},
			{
				ShaderInjectionTarget::kBsdfLightDeferredDirectional,
				"bsdf_light_deferred_directional",
				L"lighting/bsdf_light_deferred.hlsl",
				"main",
				"ps_5_0",
				kBsdfDirectionalSha1s,
				kBsdfDirectionalDefines
			},
			{
				ShaderInjectionTarget::kBsdfLightDeferredDirectionalIbl,
				"bsdf_light_deferred_directional_ibl",
				L"lighting/bsdf_light_deferred.hlsl",
				"main",
				"ps_5_0",
				kBsdfDirectionalIblSha1s,
				kBsdfDirectionalIblDefines
			},
			{
				ShaderInjectionTarget::kVlsSliceScatter,
				"vls_slice_scatter",
				L"lighting/vls_slice_scatter.hlsl",
				"main",
				"ps_5_0",
				kNoStockSha1s,
				kNoDefines
			}
		} };

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
			std::size_t                          contributors = 0;
			bool                                 slotCollision = false;
		};

		struct PublishedTarget
		{
			ShaderInjectionTarget              id = ShaderInjectionTarget::kCount;
			std::vector<sha1::Sha1Result>       stockSha1s;
			winrt::com_ptr<ID3D11PixelShader>   shader;
			std::vector<ShaderInjectionBindCallback> binds;
		};

		struct PublishedPlan
		{
			std::vector<PublishedTarget> targets;
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
					if (registration.targetId != metadata.id
						|| !IsReady(registration, registrationIndex))
						continue;

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

		std::optional<PublishedTarget> CompileTarget(
			ID3D11Device* a_device,
			const FrozenTarget& a_target,
			const std::wstring& a_developerSourceRoot)
		{
			const auto targetIndex = ToIndex(a_target.metadata->id);
			auto& runtime = GetService().runtime[targetIndex];
			runtime.compileAttempted.store(true, std::memory_order_relaxed);

			const auto sourcePath = ResolveSourcePath(
				*a_target.metadata,
				runtime.developerOverride,
				a_developerSourceRoot);
			std::vector<std::pair<const char*, const char*>> defines;
			defines.reserve(a_target.defines.size());
			for (const auto& [name, value] : a_target.defines)
				defines.emplace_back(name.c_str(), value.c_str());

			std::string compileError;
			const auto blob = util::CompileShaderToBlob(
				sourcePath.c_str(),
				defines,
				a_target.metadata->profile.data(),
				a_target.metadata->entryPoint.data(),
				&compileError);
			if (!blob) {
				runtime.compileError = compileError.empty()
					? "shader compilation failed"
					: std::move(compileError);
				L->error(
					"Compile '{}' failed ({}): {}",
					a_target.metadata->name,
					sourcePath.string(),
					runtime.compileError);
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
				runtime.compileError = buffer;
				L->error(
					"Compile '{}' failed: {}",
					a_target.metadata->name,
					runtime.compileError);
				return std::nullopt;
			}

			const auto compiledSha1 = sha1::Sha1Compute(
				blob->GetBufferPointer(),
				blob->GetBufferSize());
			runtime.compiledSha1 = sha1::Sha1ToHex(compiledSha1);
			runtime.compileError.clear();
			runtime.compileOk.store(true, std::memory_order_release);

			PublishedTarget publishedTarget;
			publishedTarget.id = a_target.metadata->id;
			publishedTarget.shader = std::move(shader);
			publishedTarget.binds = a_target.binds;
			for (const auto stockSha1Hex : a_target.metadata->stockSha1s) {
				sha1::Sha1Result stockSha1;
				if (sha1::Sha1FromHex(std::string(stockSha1Hex), stockSha1))
					publishedTarget.stockSha1s.push_back(stockSha1);
			}
			runtime.swappable.store(
				!publishedTarget.stockSha1s.empty(),
				std::memory_order_release);

			L->info(
				"Compile '{}' ok: {} bytes, dxbc-sha1={}",
				a_target.metadata->name,
				static_cast<std::size_t>(blob->GetBufferSize()),
				runtime.compiledSha1);
			return publishedTarget;
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

		bool Sha1Equals(const sha1::Sha1Result& a_left, const sha1::Sha1Result& a_right)
		{
			return a_left.bytes == a_right.bytes;
		}

		const ShaderInjectionTargetMetadata* FindTargetBySha1(
			const sha1::Sha1Result& a_sha)
		{
			for (const auto& target : kTargets) {
				for (const auto stockSha1Hex : target.stockSha1s) {
					sha1::Sha1Result stockSha1;
					if (sha1::Sha1FromHex(std::string(stockSha1Hex), stockSha1)
						&& Sha1Equals(stockSha1, a_sha))
						return &target;
				}
			}
			return nullptr;
		}

		bool ResolveInjectedPixelShader(
			const void*,
			std::size_t,
			const sha1::Sha1Result& a_sha,
			ID3D11PixelShader** a_out) noexcept
		{
			try {
				const auto* metadata = FindTargetBySha1(a_sha);
				if (!metadata)
					return false;

				auto& runtime = GetService().runtime[ToIndex(metadata->id)];
				runtime.matches.fetch_add(1, std::memory_order_relaxed);

				const auto plan = GetService().published.load(std::memory_order_acquire);
				const auto* target = plan ? FindPublishedTarget(*plan, metadata->id) : nullptr;
				if (!target || !runtime.requested.load(std::memory_order_relaxed)) {
					runtime.passthroughDisabled.fetch_add(1, std::memory_order_relaxed);
					return false;
				}
				if (!target->shader || target->stockSha1s.empty()) {
					runtime.passthroughCompileFail.fetch_add(1, std::memory_order_relaxed);
					return false;
				}

				ID3D11PixelShader* replacement = target->shader.get();
				replacement->AddRef();
				(*a_out)->Release();
				*a_out = replacement;
				const auto previous = runtime.substitutions.fetch_add(1, std::memory_order_relaxed);
				if (previous == 0) {
					L->info(
						"Replaced PS sha={} -> {}",
						sha1::Sha1ToHex(a_sha),
						metadata->name);
				}
				return true;
			} catch (...) {
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
				developerForceOffEnabled,
				developerOverrides);
			compileRequested = frozenTargets.size();
			plan->targets.reserve(frozenTargets.size());
			for (const auto& frozenTarget : frozenTargets) {
				if (frozenTarget.slotCollision)
					continue;
				if (auto compiledTarget = CompileTarget(
						a_device,
						frozenTarget,
						developerSourceRoot)) {
					++compileSucceeded;
					plan->targets.push_back(std::move(*compiledTarget));
				}
			}
		}

		service.published.store(plan, std::memory_order_release);
		{
			std::scoped_lock lock(service.mutex);
			service.lifecycle = Lifecycle::kPublished;
			const bool hasSwappableTarget = std::ranges::any_of(
				plan->targets,
				[](const PublishedTarget& a_target) {
					return !a_target.stockSha1s.empty() && a_target.shader;
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
		L->info("Compiled {}/{} replacements", compileSucceeded, compileRequested);
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
		const auto* target = plan ? FindPublishedTarget(*plan, a_target) : nullptr;
		return target ? target->shader.get() : nullptr;
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

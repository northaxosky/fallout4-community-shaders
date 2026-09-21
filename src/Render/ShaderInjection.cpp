#include "Render/ShaderInjection.h"

#include "Log.h"
#include "LogThrottle.h"
#include "Render/Engine.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderFamilyDescriptor.h"
#include "Render/ShaderVariantCompilation.h"
#include "Render/SharedData.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace cs::engine
{
	namespace
	{
		constexpr auto& kTargets = kShaderInjectionTargets;

		constexpr std::string_view StageName(ShaderStage a_stage) noexcept
		{
			static_assert(
				static_cast<std::uint8_t>(ShaderStage::kCount) == 3);
			switch (a_stage) {
			case ShaderStage::kVertex:
				return "vertex";
			case ShaderStage::kPixel:
				return "pixel";
			case ShaderStage::kCompute:
				return "compute";
			}
			std::unreachable();
		}

		constexpr std::string_view StageAbbreviation(
			ShaderStage a_stage) noexcept
		{
			static_assert(
				static_cast<std::uint8_t>(ShaderStage::kCount) == 3);
			switch (a_stage) {
			case ShaderStage::kVertex:
				return "vs";
			case ShaderStage::kPixel:
				return "ps";
			case ShaderStage::kCompute:
				return "cs";
			}
			std::unreachable();
		}

		constexpr std::string_view ProfileForStage(
			ShaderStage a_stage) noexcept
		{
			switch (a_stage) {
			case ShaderStage::kVertex:
				return "vs_5_0";
			case ShaderStage::kPixel:
				return "ps_5_0";
			case ShaderStage::kCompute:
				return "cs_5_0";
			default:
				return {};
			}
		}

		constexpr ShaderStageMask kValidShaderStages =
			ShaderStageBit(ShaderStage::kVertex)
			| ShaderStageBit(ShaderStage::kPixel)
			| ShaderStageBit(ShaderStage::kCompute);

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
			std::atomic<bool>          slotCollision{ false };
			std::atomic<std::uint8_t>  requestReasons{ 0 };
			std::atomic<std::size_t>   contributors{ 0 };
			std::atomic<std::uint64_t> matches{ 0 };
			std::atomic<std::uint64_t> substitutions{ 0 };
			std::atomic<std::uint64_t> compileFailures{ 0 };
			std::atomic<std::uint64_t> passthroughNotReady{ 0 };
			std::atomic<std::uint64_t> passthroughDisabled{ 0 };
			std::atomic<std::uint64_t> dispatches{ 0 };
			DeveloperShaderOverride   developerOverride = DeveloperShaderOverride::kAuto;
			ShaderInjectionDefines    defines;
			std::string               publicationError;
		};

		struct FrozenTarget
		{
			const ShaderInjectionTargetMetadata* metadata = nullptr;
			ShaderInjectionDefines               defines;
			std::vector<ShaderReplacementRegistration> contributions;
			struct Bind
			{
				ShaderStageMask stages = 0;
				ShaderInjectionBindCallback callback;
			};
			std::vector<Bind>                    binds;
			std::size_t                          contributors = 0;
			bool                                 slotCollision = false;
		};

		struct PublishedTarget
		{
			ShaderInjectionTarget id = ShaderInjectionTarget::kCount;
			ShaderStageMask contributedStages = 0;
			std::vector<FrozenTarget::Bind> binds;
			std::vector<ShaderReplacementRegistration> contributions;
		};

		struct PublishedPlan
		{
			std::shared_ptr<ShaderVariantCompilationCache>
				compilationCache;
			winrt::com_ptr<ID3D11Device> device;
			std::wstring developerSourceRoot;
			std::vector<PublishedTarget> targets;
		};

		struct NativeVariantKey
		{
			ShaderInjectionTarget target = ShaderInjectionTarget::kCount;
			ShaderStage stage = ShaderStage::kPixel;
			std::uint32_t descriptor = 0;
			std::string nativeName;
			std::string nativeClassName;
			std::string nativeSourceGroup;
			bool forceEarlyDepthStencil = false;
			ShaderInjectionDefines nativeMacros;

			bool operator==(const NativeVariantKey&) const = default;
		};

		struct NativeVariantKeyHash
		{
			std::size_t operator()(const NativeVariantKey& a_key) const noexcept
			{
				auto value = static_cast<std::size_t>(a_key.target);
				value = value * 131U + static_cast<std::size_t>(a_key.stage);
				value = value * 131U + a_key.descriptor;
				value = value * 131U +
					std::hash<std::string>{}(a_key.nativeName);
				value = value * 131U +
					std::hash<std::string>{}(a_key.nativeClassName);
				value = value * 131U +
					std::hash<std::string>{}(a_key.nativeSourceGroup);
				value = value * 131U + a_key.forceEarlyDepthStencil;
				for (const auto& [name, macroValue] : a_key.nativeMacros) {
					value = value * 131U
						+ std::hash<std::string>{}(name);
					value = value * 131U
						+ std::hash<std::string>{}(macroValue);
				}
				return value;
			}
		};

		struct NativeVariant
		{
			enum class State : std::uint8_t
			{
				kResolving,
				kUnsupported,
				kCompilation
			};

			mutable std::mutex mutex;
			std::shared_ptr<ShaderVariantCompilationHandle> compilation;
			ShaderInjectionDefines effectiveDefines;
			ShaderInjectionTarget target = ShaderInjectionTarget::kCount;
			ShaderStage stage = ShaderStage::kPixel;
			bool preparationStarted = false;
			std::atomic<State> state{ State::kResolving };
			std::atomic<bool> failureDiagnosed{ false };
			std::atomic<bool> pendingObserved{ false };
		};

		struct NativeReplacementWrapperKey
		{
			const void* nativeWrapper = nullptr;
			const void* replacementShader = nullptr;

			bool operator==(
				const NativeReplacementWrapperKey&) const = default;
		};

		struct NativeReplacementWrapperKeyHash
		{
			std::size_t operator()(
				const NativeReplacementWrapperKey& a_key) const noexcept
			{
				auto value = std::hash<const void*>{}(
					a_key.nativeWrapper);
				value = value * 131U
					+ std::hash<const void*>{}(
						a_key.replacementShader);
				return value;
			}
		};

		struct NativeReplacementWrapper
		{
			std::unique_ptr<std::byte[]> storage;
			winrt::com_ptr<ID3D11DeviceChild> shader;
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
			std::array<TargetRuntimeState,
				static_cast<std::size_t>(ShaderInjectionTarget::kCount)> runtime;
			std::atomic<std::shared_ptr<const PublishedPlan>> published;
			std::mutex nativeVariantMutex;
			std::unordered_map<NativeVariantKey, std::shared_ptr<NativeVariant>,
				NativeVariantKeyHash> nativeVariants;
			std::unordered_map<ID3D11DeviceChild*, NativeVariantKey>
				nativeShaderIdentities;
			struct NativeShaderMetadata
			{
				bool forceEarlyDepthStencil = false;
			};
			std::unordered_map<ID3D11DeviceChild*, NativeShaderMetadata>
				nativeShaderMetadata;
			std::unordered_map<ID3D11ComputeShader*, NativeVariantKey>
				nativeComputeOwners;
			std::unordered_map<
				NativeReplacementWrapperKey,
				NativeReplacementWrapper,
				NativeReplacementWrapperKeyHash>
				nativeReplacementWrappers;
			std::atomic_flag swapCountersLock = ATOMIC_FLAG_INIT;
		};

		// The engine tail survives D3D11's lazy dispatch-table replacement.
		constexpr std::ptrdiff_t kRunComputeShaderDispatchTailOffset = 0xAB;
		constexpr std::array<std::uint8_t, 7>
			kRunComputeShaderDispatchTail{
				0x48, 0xFF, 0xA0, 0x48, 0x01, 0x00, 0x00
			};

		std::mutex g_computeDispatchBridgeInstallMutex;
		std::atomic<std::uintptr_t> g_computeDispatchBridgeTail{ 0 };
		std::atomic<ID3D11DeviceContext*> g_computeDispatchContext{ nullptr };
		std::array<std::uint8_t, 7> g_computeDispatchBridgePatch{};
		std::atomic_uint64_t g_computeBridgeCalls{ 0 };
		std::atomic_uint64_t g_computeMatchingDispatches{ 0 };
		std::atomic_uint64_t g_computeContextRejections{ 0 };
		std::atomic_uint64_t g_computePhaseRejections{ 0 };
		std::atomic_uint64_t g_computeShaderRejections{ 0 };

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

		const char* NativeShaderFilename(
			const RE::BSShader* a_shader,
			std::optional<bool> a_modernLayoutForTesting = std::nullopt)
		{
#ifdef FO4CS_SHADER_INJECTION_TESTING
			if (a_modernLayoutForTesting) {
				return native::FxpFilenameForTesting(
					a_shader, *a_modernLayoutForTesting);
			}
#else
			(void)a_modernLayoutForTesting;
#endif
			return native::FxpFilename(a_shader);
		}

		std::optional<ShaderInjectionTarget> ResolveNativeShaderTarget(
			const RE::BSShader& a_shader,
			std::optional<bool> a_modernLayoutForTesting = std::nullopt)
		{
			if (native::ShaderType(&a_shader) == 0xC)
				return ShaderInjectionTarget::kImageSpace;
			const auto filename = NativeShaderFilename(
				&a_shader, a_modernLayoutForTesting);
			if (!filename)
				return std::nullopt;
			std::string name(filename);
			std::ranges::transform(
				name, name.begin(), [](unsigned char a_character) {
					return static_cast<char>(std::tolower(a_character));
				});

			if (name.contains("prepass"))
				return ShaderInjectionTarget::kDeferredPrepass;
			if (name.contains("tiledlighting"))
				return ShaderInjectionTarget::kDfTiledLighting;
			if (name.contains("composite"))
				return ShaderInjectionTarget::kBsdfComposite;
			if (name.contains("bsdflight") || name == "dflight")
				return ShaderInjectionTarget::kBsdfLight;
			if (name.contains("utility"))
				return ShaderInjectionTarget::kUtility;
			if (name.contains("particle"))
				return ShaderInjectionTarget::kParticle;
			if (name.contains("effect"))
				return ShaderInjectionTarget::kEffect;
			if (name.contains("blood"))
				return ShaderInjectionTarget::kBloodSplatter;
			if (name.contains("distanttree"))
				return ShaderInjectionTarget::kDistantTree;
			if (name.contains("facecustom"))
				return ShaderInjectionTarget::kFaceCustomization;
			if (name.starts_with("is") || name.contains("imagespace"))
				return ShaderInjectionTarget::kImageSpace;
			if (name.contains("sky"))
				return ShaderInjectionTarget::kBsSky;
			if (name.contains("water"))
				return ShaderInjectionTarget::kBsWater;
			if (name.contains("lighting"))
				return ShaderInjectionTarget::kBsLighting;
			return std::nullopt;
		}

		struct NativeShaderFamilyContext
		{
			ShaderInjectionTarget target =
				ShaderInjectionTarget::kCount;
			std::string_view nativeName;
			std::string_view nativeClassName;
			std::string_view nativeSourceGroup;
			ShaderInjectionDefines nativeMacros;
		};

		std::optional<NativeShaderFamilyContext>
			ResolveNativeShaderFamilyContextImpl(
				RE::BSShader* a_shader,
				bool a_allowUnvalidatedImageSpaceRuntime,
				std::optional<bool> a_modernLayoutForTesting =
					std::nullopt)
		{
			if (!a_shader)
				return std::nullopt;
			const auto target = ResolveNativeShaderTarget(
				*a_shader, a_modernLayoutForTesting);
			if (!target)
				return std::nullopt;
			const auto nativeName = NativeShaderFilename(
				a_shader, a_modernLayoutForTesting);

			NativeShaderFamilyContext result{
				.target = *target,
				.nativeName = nativeName ? nativeName : ""
			};
			if (*target != ShaderInjectionTarget::kImageSpace)
				return result;

			const char* sourceGroup = nullptr;
			const char* className = nullptr;
			std::optional<native::ImageSpaceMacroSet> emitted;
#ifdef FO4CS_SHADER_INJECTION_TESTING
			if (a_allowUnvalidatedImageSpaceRuntime) {
				sourceGroup =
					native::ImageSpaceShaderPrefixForTesting(a_shader);
				className =
					native::ImageSpaceShaderClassNameForTesting(a_shader);
				emitted =
					native::GetImageSpaceMacrosForTesting(a_shader);
			} else
#else
			(void)a_allowUnvalidatedImageSpaceRuntime;
#endif
			{
				sourceGroup =
					native::ImageSpaceShaderPrefix(a_shader);
				className =
					native::ImageSpaceShaderClassName(a_shader);
				emitted = native::GetImageSpaceMacros(a_shader);
			}
			if (!sourceGroup || !className || !emitted)
				return std::nullopt;
			result.nativeClassName = className;
			result.nativeSourceGroup = sourceGroup;
			for (std::size_t index = 0;
				index < emitted->count;
				++index) {
				result.nativeMacros.insert_or_assign(
					emitted->values[index].first,
					emitted->values[index].second);
			}
			return result;
		}

		std::optional<NativeShaderFamilyContext>
			ResolveNativeShaderFamilyContext(RE::BSShader* a_shader)
		{
			return ResolveNativeShaderFamilyContextImpl(
				a_shader, false);
		}

		NativeVariantKey MakeNativeVariantKey(
			const ShaderFamilyDescriptor& a_descriptor)
		{
			return {
				.target = a_descriptor.target,
				.stage = a_descriptor.stage,
				.descriptor = a_descriptor.descriptor,
				.nativeName = std::string(a_descriptor.nativeName),
				.nativeClassName =
					std::string(a_descriptor.nativeClassName),
				.nativeSourceGroup =
					std::string(a_descriptor.nativeSourceGroup),
				.forceEarlyDepthStencil =
					a_descriptor.forceEarlyDepthStencil,
				.nativeMacros = a_descriptor.nativeMacros
			};
		}

		NativeVariantKey MakeNativeVariantKey(
			const NativeShaderFamilyContext& a_family,
			ShaderStage a_stage,
			std::uint32_t a_descriptor,
			bool a_forceEarlyDepthStencil = false)
		{
			return MakeNativeVariantKey({
				.target = a_family.target,
				.stage = a_stage,
				.descriptor = a_descriptor,
				.nativeName = a_family.nativeName,
				.nativeClassName = a_family.nativeClassName,
				.nativeSourceGroup = a_family.nativeSourceGroup,
				.forceEarlyDepthStencil = a_forceEarlyDepthStencil,
				.nativeMacros = a_family.nativeMacros
			});
		}

		ShaderFamilyDescriptor DescribeNativeVariant(
			const NativeVariantKey& a_key)
		{
			return {
				.target = a_key.target,
				.stage = a_key.stage,
				.descriptor = a_key.descriptor,
				.nativeName = a_key.nativeName,
				.nativeClassName = a_key.nativeClassName,
				.nativeSourceGroup = a_key.nativeSourceGroup,
				.forceEarlyDepthStencil =
					a_key.forceEarlyDepthStencil,
				.nativeMacros = a_key.nativeMacros
			};
		}

		constexpr ShaderStageMask SupportedStages(
			ShaderInjectionTarget a_target) noexcept
		{
			constexpr auto graphics =
				ShaderStageBit(ShaderStage::kVertex)
				| ShaderStageBit(ShaderStage::kPixel);
			switch (a_target) {
			case ShaderInjectionTarget::kFaceCustomization:
				return ShaderStageBit(ShaderStage::kVertex);
			case ShaderInjectionTarget::kImageSpace:
				return graphics | ShaderStageBit(ShaderStage::kCompute);
			case ShaderInjectionTarget::kDfTiledLighting:
				return ShaderStageBit(ShaderStage::kCompute);
			default:
				return graphics;
			}
		}

		constexpr bool IsDfTiledStandaloneLoadTechnique(
			std::uint32_t a_descriptor) noexcept
		{
			return a_descriptor == 0
				|| (a_descriptor >= 3 && a_descriptor <= 18);
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
				runtime.compileFailures.fetch_add(
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
					if (registration.bind) {
						target.binds.push_back({
							registration.stages,
							registration.bind
						});
					}
				}

				if (developerOverride == DeveloperShaderOverride::kForceOff) {
					requestReasons = ShaderInjectionRequestReason::kNone;
				}
				const bool requested =
					requestReasons != ShaderInjectionRequestReason::kNone;

				for (const auto& contribution : target.contributions) {
					const bool stageMatched =
						(contribution.stages
							& SupportedStages(metadata.id))
						!= 0;
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

		struct VariantObservationCounts
		{
			std::size_t observed = 0;
			std::size_t pending = 0;
			std::size_t ready = 0;
			std::size_t failed = 0;
			std::size_t unsupported = 0;
		};

		VariantObservationCounts CollectNativeVariantObservations(
			std::optional<ShaderInjectionTarget> a_target = std::nullopt)
		{
			VariantObservationCounts counts;
			auto& service = GetService();
			std::scoped_lock lock(service.nativeVariantMutex);
			for (const auto& [key, variant] : service.nativeVariants) {
				if (a_target && key.target != *a_target)
					continue;

				++counts.observed;
				switch (variant->state.load(std::memory_order_acquire)) {
				case NativeVariant::State::kUnsupported:
					++counts.unsupported;
					break;
				case NativeVariant::State::kResolving:
					++counts.pending;
					break;
				case NativeVariant::State::kCompilation:
				{
					std::shared_ptr<ShaderVariantCompilationHandle>
						compilation;
					{
						std::scoped_lock variantLock(variant->mutex);
						compilation = variant->compilation;
					}
					if (!compilation) {
						++counts.pending;
						break;
					}
					switch (compilation->GetState()) {
					case ShaderVariantCompilationState::kReady:
						++counts.ready;
						break;
					case ShaderVariantCompilationState::kPending:
						++counts.pending;
						break;
					case ShaderVariantCompilationState::kFailed:
						++counts.failed;
						break;
					}
					break;
				}
				}
			}
			return counts;
		}

		void DiagnoseNativeVariantCompilationFailure(
			const std::weak_ptr<NativeVariant>& a_variant,
			ShaderInjectionTarget a_target,
			std::uint32_t a_descriptor,
			std::string_view a_error) noexcept
		{
			const auto variant = a_variant.lock();
			if (!variant
				|| variant->failureDiagnosed.exchange(
					true, std::memory_order_relaxed)) {
				return;
			}

			const auto* metadata = GetShaderInjectionTarget(a_target);
			L->error(
				"Native descriptor compile failed for '{}/{}/{:#010x}': {}",
				metadata ? metadata->name : "unknown",
				StageName(variant->stage),
				a_descriptor,
				a_error.empty() ?
					"shader compilation failed" :
					a_error);
			GetService().runtime[ToIndex(a_target)]
				.compileFailures.fetch_add(
					1, std::memory_order_relaxed);
		}

		std::shared_ptr<NativeVariant> GetOrCreateNativeVariant(
			const NativeVariantKey& a_key)
		{
			auto candidate = std::make_shared<NativeVariant>();
			candidate->target = a_key.target;
			candidate->stage = a_key.stage;
			auto& service = GetService();
			std::scoped_lock lock(service.nativeVariantMutex);
			const auto [entry, inserted] =
				service.nativeVariants.emplace(a_key, candidate);
			return inserted ? std::move(candidate) : entry->second;
		}

		void PrepareNativeVariant(
			const PublishedPlan& a_plan,
			const PublishedTarget& a_target,
			const NativeVariantKey& a_key,
			const std::shared_ptr<NativeVariant>& a_variant)
		{
			if (!a_variant)
				return;
			{
				std::scoped_lock lock(a_variant->mutex);
				if (a_variant->preparationStarted
					|| a_variant->state.load(std::memory_order_acquire)
						!= NativeVariant::State::kResolving) {
					return;
				}
				a_variant->preparationStarted = true;
			}

			const auto descriptor = DescribeNativeVariant(a_key);
			auto family =
				BuildShaderFamilyCompilationDescriptor(descriptor);
			if (!family) {
				a_variant->state.store(
					NativeVariant::State::kUnsupported,
					std::memory_order_release);
				return;
			}

			std::string error;
			const auto* metadata =
				GetShaderInjectionTarget(descriptor.target);
			if (!metadata) {
				a_variant->state.store(
					NativeVariant::State::kUnsupported,
					std::memory_order_release);
				return;
			}
			auto effective = BuildEffectiveShaderCompileRequest(
				*metadata,
				descriptor.stage,
				*family,
				a_target.contributions,
				&error);
			if (!effective) {
				a_variant->state.store(
					NativeVariant::State::kUnsupported,
					std::memory_order_release);
				L->error(
					"Native descriptor compile request rejected for '{}/{}/{:#010x}': {}",
					metadata->name,
					StageName(descriptor.stage),
					descriptor.descriptor,
					error);
				return;
			}

			auto& service = GetService();
			ShaderVariantCompilationRequest request;
			request.device = a_plan.device;
			request.sourcePath = ResolveSourcePath(
				effective->sourcePath,
				service.runtime[ToIndex(descriptor.target)]
					.developerOverride,
				a_plan.developerSourceRoot);
			request.entryPoint = effective->entryPoint;
			request.profile = effective->profile;
			request.stage = descriptor.stage;
			request.familyId =
				static_cast<std::uint32_t>(descriptor.target);
			request.descriptor = descriptor.descriptor;
			request.owner =
				!descriptor.nativeClassName.empty() ?
					std::string(descriptor.nativeClassName) :
					std::string(descriptor.nativeName);
			if (!descriptor.nativeSourceGroup.empty()) {
				request.owner += "|";
				request.owner += descriptor.nativeSourceGroup;
			}
			request.defines.reserve(effective->defines.size());
			for (const auto& define : effective->defines)
				request.defines.push_back(define);
			request.completion = [
				variant = std::weak_ptr<NativeVariant>(a_variant),
				target = descriptor.target,
				nativeDescriptor = descriptor.descriptor](
					ShaderVariantCompilationState a_state,
					std::string_view a_error) {
				if (a_state == ShaderVariantCompilationState::kFailed) {
					DiagnoseNativeVariantCompilationFailure(
						variant,
						target,
						nativeDescriptor,
						a_error);
				}
			};

			auto compilation =
				a_plan.compilationCache->Request(std::move(request));
			if (!compilation) {
				a_variant->state.store(
					NativeVariant::State::kUnsupported,
					std::memory_order_release);
				L->error(
					"Native descriptor compile failed for '{}/{}/{:#010x}': {}",
					metadata->name,
					StageName(descriptor.stage),
					descriptor.descriptor,
					"compilation cache rejected request");
				service.runtime[ToIndex(descriptor.target)]
					.compileFailures.fetch_add(
						1, std::memory_order_relaxed);
				return;
			}

			{
				std::scoped_lock lock(a_variant->mutex);
				a_variant->compilation = std::move(compilation);
				a_variant->effectiveDefines =
					std::move(effective->defines);
			}
			a_variant->state.store(
				NativeVariant::State::kCompilation,
				std::memory_order_release);
		}

		std::shared_ptr<NativeVariant> FindOrPrepareNativeVariant(
			const PublishedPlan& a_plan,
			const PublishedTarget& a_target,
			const NativeVariantKey& a_key)
		{
			auto variant = GetOrCreateNativeVariant(a_key);
			PrepareNativeVariant(a_plan, a_target, a_key, variant);
			return variant;
		}

		bool ObserveNativeVariant(NativeVariantKey a_key)
		{
			if (!IsValidTarget(a_key.target))
				return false;
			auto& service = GetService();
			auto plan = service.published.load(std::memory_order_acquire);
			if (plan && !FindPublishedTarget(*plan, a_key.target))
				return false;

			auto variant = GetOrCreateNativeVariant(a_key);
			plan = service.published.load(std::memory_order_acquire);
			if (!plan)
				return true;
			const auto* target =
				FindPublishedTarget(*plan, a_key.target);
			if (!target) {
				std::scoped_lock lock(service.nativeVariantMutex);
				const auto entry = service.nativeVariants.find(a_key);
				if (entry != service.nativeVariants.end()
					&& entry->second == variant) {
					service.nativeVariants.erase(entry);
				}
				return false;
			}
			PrepareNativeVariant(*plan, *target, a_key, variant);
			return true;
		}

		void PrepareObservedNativeVariants(
			const std::shared_ptr<const PublishedPlan>& a_plan)
		{
			if (!a_plan)
				return;
			std::vector<std::pair<
				NativeVariantKey,
				std::shared_ptr<NativeVariant>>> pending;
			auto& service = GetService();
			{
				std::scoped_lock lock(service.nativeVariantMutex);
				for (auto entry = service.nativeVariants.begin();
					entry != service.nativeVariants.end();) {
					if (!FindPublishedTarget(
							*a_plan, entry->first.target)) {
						entry = service.nativeVariants.erase(entry);
						continue;
					}
					pending.emplace_back(
						entry->first, entry->second);
					++entry;
				}
			}
			for (const auto& [key, variant] : pending) {
				if (const auto* target =
						FindPublishedTarget(*a_plan, key.target)) {
					PrepareNativeVariant(
						*a_plan, *target, key, variant);
				}
			}
		}

		void RecordNativeComputeShader(
			NativeVariantKey a_key,
			ID3D11ComputeShader* a_shader,
			bool a_prequeue)
		{
			if (!IsValidTarget(a_key.target) || !a_shader)
				return;
			{
				auto& service = GetService();
				std::scoped_lock lock(service.nativeVariantMutex);
				service.nativeComputeOwners.insert_or_assign(
					a_shader, a_key);
			}
			if (a_prequeue)
				std::ignore = ObserveNativeVariant(std::move(a_key));
		}

		template <class TShader>
		TShader* AcquireNativeReplacement(
			const PublishedPlan& a_plan,
			const PublishedTarget& a_target,
			const NativeVariantKey& a_key)
		{
			auto variant = FindOrPrepareNativeVariant(
				a_plan, a_target, a_key);
			if (!variant)
				return nullptr;
			std::shared_ptr<ShaderVariantCompilationHandle> compilation;
			{
				std::scoped_lock lock(variant->mutex);
				compilation = variant->compilation;
			}
			if (!compilation)
				return nullptr;
			auto shader = compilation->Acquire();
			if (!shader) {
				auto& runtime =
					GetService().runtime[ToIndex(a_key.target)];
				if (compilation->GetState()
					== ShaderVariantCompilationState::kFailed) {
					DiagnoseNativeVariantCompilationFailure(
						variant,
						a_key.target,
						a_key.descriptor,
						compilation->GetError());
				} else if (!variant->pendingObserved.exchange(
						true, std::memory_order_relaxed)) {
					runtime.passthroughNotReady.fetch_add(
						1, std::memory_order_relaxed);
				}
				return nullptr;
			}

			{
				auto& service = GetService();
				std::scoped_lock lock(service.nativeVariantMutex);
				service.nativeShaderIdentities.insert_or_assign(
					shader.get(), a_key);
			}
			return static_cast<TShader*>(shader.detach());
		}

		template <class TWrapper, class TD3DShader>
		TWrapper* CacheNativeReplacementWrapper(
			TWrapper* a_nativeWrapper,
			TD3DShader* a_replacement)
		{
			if (!a_nativeWrapper || !a_replacement)
				return nullptr;

			auto& service = GetService();
			std::scoped_lock lock(service.nativeVariantMutex);
			const NativeReplacementWrapperKey key{
				.nativeWrapper = a_nativeWrapper,
				.replacementShader = a_replacement
			};
			if (const auto existing =
					service.nativeReplacementWrappers.find(key);
				existing != service.nativeReplacementWrappers.end()) {
				return reinterpret_cast<TWrapper*>(
					existing->second.storage.get());
			}

			std::size_t trailingBytecodeSize = 0;
			if constexpr (
				std::is_same_v<TWrapper, RE::BSGraphics::VertexShader>
				|| std::is_same_v<
					TWrapper,
					RE::BSGraphics::ComputeShader>) {
				trailingBytecodeSize = a_nativeWrapper->byteCodeSize;
			}
			if (trailingBytecodeSize
				> std::numeric_limits<std::size_t>::max()
					- sizeof(TWrapper)) {
				return nullptr;
			}
			auto storage = std::make_unique<std::byte[]>(
				sizeof(TWrapper) + trailingBytecodeSize);
			std::memcpy(
				storage.get(),
				a_nativeWrapper,
				sizeof(TWrapper) + trailingBytecodeSize);
			auto* wrapper =
				reinterpret_cast<TWrapper*>(storage.get());
			wrapper->shader =
				reinterpret_cast<decltype(wrapper->shader)>(
					a_replacement);
			winrt::com_ptr<ID3D11DeviceChild> shaderLifetime;
			if (FAILED(a_replacement->QueryInterface(
					IID_PPV_ARGS(shaderLifetime.put())))) {
				return nullptr;
			}
			const auto [inserted, unused] =
				service.nativeReplacementWrappers.emplace(
					key,
					NativeReplacementWrapper{
						.storage = std::move(storage),
						.shader = std::move(shaderLifetime)
					});
			return reinterpret_cast<TWrapper*>(
				inserted->second.storage.get());
		}

		template <class TWrapper, class TD3DShader>
		TWrapper* AcquireNativeReplacementWrapper(
			const PublishedPlan& a_plan,
			const PublishedTarget& a_target,
			const NativeVariantKey& a_key,
			TWrapper* a_nativeWrapper)
		{
			if (!a_nativeWrapper)
				return nullptr;
			auto* replacement = AcquireNativeReplacement<TD3DShader>(
				a_plan, a_target, a_key);
			if (!replacement)
				return nullptr;
			winrt::com_ptr<TD3DShader> replacementReference;
			replacementReference.attach(replacement);
			return CacheNativeReplacementWrapper(
				a_nativeWrapper, replacementReference.get());
		}

		thread_local const ShaderInjectionDefines* t_activeDefines = nullptr;
		thread_local ShaderInjectionTarget t_activeTarget =
			ShaderInjectionTarget::kCount;

		class ActiveVariantScope
		{
		public:
			explicit ActiveVariantScope(const NativeVariant* a_variant) noexcept :
				_previousDefines(t_activeDefines),
				_previousTarget(t_activeTarget)
			{
				t_activeDefines = a_variant ?
					&a_variant->effectiveDefines :
					nullptr;
				t_activeTarget = a_variant ?
					a_variant->target :
					ShaderInjectionTarget::kCount;
			}
			~ActiveVariantScope() noexcept
			{
				t_activeDefines = _previousDefines;
				t_activeTarget = _previousTarget;
			}

			ActiveVariantScope(const ActiveVariantScope&) = delete;
			ActiveVariantScope& operator=(const ActiveVariantScope&) = delete;

		private:
			const ShaderInjectionDefines* _previousDefines;
			ShaderInjectionTarget _previousTarget;
		};

		void DispatchPublishedTarget(
			const PublishedTarget& a_target,
			ShaderStage a_stage,
			ID3D11DeviceContext* a_context) noexcept
		{
			if (a_stage == ShaderStage::kPixel)
				render::BindSharedData(a_context, a_stage);

			auto& runtime = GetService().runtime[ToIndex(a_target.id)];
			for (const auto& bind : a_target.binds) {
				if ((bind.stages & ShaderStageBit(a_stage)) == 0)
					continue;
				try {
					bind.callback(a_context);
					runtime.dispatches.fetch_add(1, std::memory_order_relaxed);
				} catch (const std::exception& e) {
					CS_LOG_EVERY_MS(
						L,
						2000,
						spdlog::level::warn,
						"Shader injection for '{}' failed: {}.",
						kTargets[ToIndex(a_target.id)].name,
						e.what());
				} catch (...) {
					CS_LOG_EVERY_MS(
						L,
						2000,
						spdlog::level::warn,
						"Shader injection for '{}' failed.",
						kTargets[ToIndex(a_target.id)].name);
				}
			}
		}

		void ExecuteComputeDispatch(
			ID3D11DeviceContext* a_context,
			UINT a_threadGroupCountX,
			UINT a_threadGroupCountY,
			UINT a_threadGroupCountZ) noexcept
		{
			g_computeBridgeCalls.fetch_add(1, std::memory_order_relaxed);
			if (!a_context) {
				g_computeContextRejections.fetch_add(
					1, std::memory_order_relaxed);
				return;
			}

			if (a_context != g_computeDispatchContext.load(
					std::memory_order_acquire)) {
				g_computeContextRejections.fetch_add(
					1, std::memory_order_relaxed);
				a_context->Dispatch(
					a_threadGroupCountX,
					a_threadGroupCountY,
					a_threadGroupCountZ);
				return;
			}

			const auto plan =
				GetService().published.load(std::memory_order_acquire);
			if (!plan) {
				g_computeShaderRejections.fetch_add(
					1, std::memory_order_relaxed);
				a_context->Dispatch(
					a_threadGroupCountX,
					a_threadGroupCountY,
					a_threadGroupCountZ);
				return;
			}

			ID3D11ComputeShader* shader = nullptr;
			a_context->CSGetShader(&shader, nullptr, nullptr);
			winrt::com_ptr<ID3D11ComputeShader> boundShader;
			boundShader.attach(shader);
			if (!shader) {
				g_computeShaderRejections.fetch_add(
					1, std::memory_order_relaxed);
				a_context->Dispatch(
					a_threadGroupCountX,
					a_threadGroupCountY,
					a_threadGroupCountZ);
				return;
			}

			std::optional<NativeVariantKey> owner;
			std::shared_ptr<NativeVariant> variant;
			{
				auto& service = GetService();
				std::scoped_lock lock(service.nativeVariantMutex);
				const auto identity =
					service.nativeShaderIdentities.find(shader);
				if (identity != service.nativeShaderIdentities.end()) {
					owner = identity->second;
					const auto found =
						service.nativeVariants.find(identity->second);
					if (found != service.nativeVariants.end())
						variant = found->second;
				}
			}
			if (!owner) {
				g_computeShaderRejections.fetch_add(
					1, std::memory_order_relaxed);
				a_context->Dispatch(
					a_threadGroupCountX,
					a_threadGroupCountY,
					a_threadGroupCountZ);
				return;
			}
			const auto* target =
				FindPublishedTarget(*plan, owner->target);
			if (!target) {
				g_computeShaderRejections.fetch_add(
					1, std::memory_order_relaxed);
				a_context->Dispatch(
					a_threadGroupCountX,
					a_threadGroupCountY,
					a_threadGroupCountZ);
				return;
			}

			g_computeMatchingDispatches.fetch_add(
				1, std::memory_order_relaxed);
			const bool hasFeatureBindings =
				(target->contributedStages
					& ShaderStageBit(ShaderStage::kCompute))
				!= 0;
			if (hasFeatureBindings) {
				if (owner->target !=
						ShaderInjectionTarget::kDfTiledLighting ||
					!render::IsDeferredLightsActive()) {
					g_computePhaseRejections.fetch_add(
						1, std::memory_order_relaxed);
					a_context->Dispatch(
						a_threadGroupCountX,
						a_threadGroupCountY,
						a_threadGroupCountZ);
					return;
				}
				render::ScopedComputeSharedDataBinding bindings(a_context);
				if (!bindings.IsActive()) {
					g_computePhaseRejections.fetch_add(
						1, std::memory_order_relaxed);
					a_context->Dispatch(
						a_threadGroupCountX,
						a_threadGroupCountY,
						a_threadGroupCountZ);
					return;
				}
				const ActiveVariantScope variantScope(variant.get());
				DispatchPublishedTarget(
					*target,
					ShaderStage::kCompute,
					a_context);
				a_context->Dispatch(
					a_threadGroupCountX,
					a_threadGroupCountY,
					a_threadGroupCountZ);
			} else {
				a_context->Dispatch(
					a_threadGroupCountX,
					a_threadGroupCountY,
					a_threadGroupCountZ);
			}
		}

		void STDMETHODCALLTYPE RunComputeShaderDispatchBridge(
			ID3D11DeviceContext* a_context,
			UINT a_threadGroupCountX,
			UINT a_threadGroupCountY,
			UINT a_threadGroupCountZ) noexcept
		{
			ExecuteComputeDispatch(
				a_context,
				a_threadGroupCountX,
				a_threadGroupCountY,
				a_threadGroupCountZ);
		}

		bool IsRelativeBranchReachable(
			std::uintptr_t a_source,
			std::uintptr_t a_target) noexcept
		{
			const auto displacement =
				static_cast<std::int64_t>(a_target)
				- static_cast<std::int64_t>(
					a_source + sizeof(REL::ASM::JMP5));
			return displacement
				>= std::numeric_limits<std::int32_t>::min()
				&& displacement
				<= std::numeric_limits<std::int32_t>::max();
		}

		bool InstallComputeDispatchBridgeAt(
			ID3D11DeviceContext* a_immediateContext,
			std::uintptr_t a_validatedTail) noexcept
		{
			if (!a_immediateContext || !a_validatedTail)
				return false;

			std::scoped_lock lock(g_computeDispatchBridgeInstallMutex);
			const auto installedTail =
				g_computeDispatchBridgeTail.load(
					std::memory_order_acquire);
			if (installedTail != 0) {
				return installedTail == a_validatedTail
					&& g_computeDispatchContext.load(
						std::memory_order_acquire)
						== a_immediateContext
					&& std::memcmp(
						reinterpret_cast<const void*>(
							installedTail),
						g_computeDispatchBridgePatch.data(),
						g_computeDispatchBridgePatch.size())
						== 0;
			}

			if (std::memcmp(
					reinterpret_cast<const void*>(a_validatedTail),
					kRunComputeShaderDispatchTail.data(),
					kRunComputeShaderDispatchTail.size())
				!= 0) {
				L->error(
					"RunComputeShader dispatch bridge installation refused: "
					"the 7-byte tail does not match the supported layout.");
				return false;
			}

			try {
				auto& trampoline = REL::GetTrampoline();
				if (trampoline.free_size() < sizeof(REL::ASM::JMP14)) {
					L->error(
						"RunComputeShader dispatch bridge installation failed: "
						"insufficient trampoline space.");
					return false;
				}
				const auto branch = trampoline.allocate_branch5(
					reinterpret_cast<std::uintptr_t>(
						&RunComputeShaderDispatchBridge));
				if (!IsRelativeBranchReachable(
						a_validatedTail, branch)) {
					L->error(
						"RunComputeShader dispatch bridge installation failed: "
						"the allocated branch is outside rel32 range.");
					return false;
				}

				std::array<std::uint8_t, 7> patch{
					REL::NOP, REL::NOP, REL::NOP, REL::NOP,
					REL::NOP, REL::NOP, REL::NOP
				};
				const REL::ASM::JMP5 jump(a_validatedTail, branch);
				std::memcpy(
					patch.data(), std::addressof(jump), sizeof(jump));
				const bool protectionRestored = REL::WriteSafe(
					a_validatedTail,
					patch.data(),
					patch.size());
				const bool patchOwned = std::memcmp(
						reinterpret_cast<const void*>(
							a_validatedTail),
						patch.data(),
						patch.size())
					== 0;
				if (!patchOwned) {
					const bool restored = REL::WriteSafe(
						a_validatedTail,
						kRunComputeShaderDispatchTail.data(),
						kRunComputeShaderDispatchTail.size())
						&& std::memcmp(
							reinterpret_cast<const void*>(
								a_validatedTail),
							kRunComputeShaderDispatchTail.data(),
							kRunComputeShaderDispatchTail.size())
							== 0;
					L->error(
						"RunComputeShader dispatch bridge installation failed: "
						"the written tail could not be verified (rollback={}).",
						restored);
					if (!restored)
						std::terminate();
					FlushInstructionCache(
						GetCurrentProcess(),
						reinterpret_cast<const void*>(a_validatedTail),
						kRunComputeShaderDispatchTail.size());
					return false;
				}
				FlushInstructionCache(
					GetCurrentProcess(),
					reinterpret_cast<const void*>(a_validatedTail),
					patch.size());
				if (!protectionRestored) {
					L->warn(
						"RunComputeShader dispatch bridge owns the verified "
						"tail, but restoring its page protection failed.");
				}

				g_computeDispatchBridgePatch = patch;
				g_computeDispatchContext.store(
					a_immediateContext, std::memory_order_release);
				g_computeDispatchBridgeTail.store(
					a_validatedTail, std::memory_order_release);
				return true;
			} catch (const std::exception& e) {
				L->error(
					"RunComputeShader dispatch bridge installation failed: {}",
					e.what());
			} catch (...) {
				L->error(
					"RunComputeShader dispatch bridge installation failed: "
					"unknown exception.");
			}
			return false;
		}
	}

	bool RegisterReplacement(ShaderReplacementRegistration a_registration)
	{
		auto& service = GetService();
		const bool installsPreDrawHook =
			static_cast<bool>(a_registration.bind)
			&& (a_registration.stages
				& ShaderStageBit(ShaderStage::kPixel))
				!= 0;
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
		if (installsPreDrawHook && !EnsureDeferredDrawAnchorInstalled()) {
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

	bool SetBaselineShaderOwnership(
		ShaderInjectionTarget a_target,
		bool a_enabled)
	{
		if (!IsValidTarget(a_target)) {
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

	bool ValidateShaderInjectionRoutes(
		std::string_view a_contributor,
		std::string& a_error)
	{
		auto& service = GetService();
		std::scoped_lock lock(service.mutex);
		if (service.lifecycle != Lifecycle::kPublished) {
			a_error = std::string(a_contributor)
				+ " routes were validated before injection publication";
			return false;
		}
		if (!service.enabled) {
			a_error = std::string(a_contributor)
				+ " routes are disabled by the shader-injection core kill switch";
			return false;
		}

		const auto plan =
			service.published.load(std::memory_order_acquire);
		if (!plan || !plan->device) {
			a_error = std::string(a_contributor)
				+ " routes were not published because the D3D11 device is unavailable";
			return false;
		}

		const auto stageNames = [](ShaderStageMask a_stages) {
			std::string names;
			for (std::size_t stageIndex = 0;
				stageIndex < static_cast<std::size_t>(ShaderStage::kCount);
				++stageIndex) {
				const auto stage =
					static_cast<ShaderStage>(stageIndex);
				if ((a_stages & ShaderStageBit(stage)) == 0)
					continue;
				if (!names.empty())
					names += "|";
				names += StageName(stage);
			}
			return names.empty() ? std::string("none") : names;
		};
		const auto defineNames = [](const ShaderInjectionDefines& a_defines) {
			std::string names;
			for (const auto& [name, value] : a_defines) {
				if (!names.empty())
					names += ",";
				names += name;
				names += "=";
				names += value;
			}
			return names.empty() ? std::string("none") : names;
		};
		std::vector<const ShaderReplacementRegistration*> matched;
		bool foundRegistration = false;
		for (const auto& registration : service.registrations) {
			if (registration.contributor != a_contributor)
				continue;
			foundRegistration = true;

			const auto* metadata =
				GetShaderInjectionTarget(registration.targetId);
			if (!metadata) {
				a_error = "contributor '" + std::string(a_contributor)
					+ "' registered an unknown shader-injection target";
				return false;
			}

			const auto& runtime =
				service.runtime[ToIndex(registration.targetId)];
			if (runtime.developerOverride
				== DeveloperShaderOverride::kForceOff) {
				a_error = "'" + std::string(metadata->name)
					+ "' cannot deliver contributor '"
					+ std::string(a_contributor)
					+ "' because its developer override is force-off";
				return false;
			}
			if (!runtime.requested.load(std::memory_order_relaxed)
				|| !HasShaderInjectionRequestReason(
					static_cast<ShaderInjectionRequestReason>(
						runtime.requestReasons.load(
							std::memory_order_relaxed)),
					ShaderInjectionRequestReason::kFeatureContributor)) {
				a_error = "'" + std::string(metadata->name)
					+ "' did not freeze feature ownership for contributor '"
					+ std::string(a_contributor) + "'";
				return false;
			}
			if (runtime.slotCollision.load(std::memory_order_relaxed)) {
				a_error = "'" + std::string(metadata->name)
					+ "' cannot deliver contributor '"
					+ std::string(a_contributor)
					+ "' because its slot or define claims conflict";
				return false;
			}
			if (registration.targetId
					== ShaderInjectionTarget::kDfTiledLighting
				&& (registration.stages
					& ShaderStageBit(ShaderStage::kCompute))
					!= 0
				&& !ComputeDispatchBridgeInstalled()) {
				a_error = "'" + std::string(metadata->name)
					+ "' cannot deliver contributor '"
					+ std::string(a_contributor)
					+ "' because the RunComputeShader dispatch bridge is unavailable";
				return false;
			}

			const auto* published =
				FindPublishedTarget(*plan, registration.targetId);
			if (!published) {
				a_error = "'" + std::string(metadata->name)
					+ "' did not publish the registered route for contributor '"
					+ std::string(a_contributor) + "'";
				if (!runtime.publicationError.empty()) {
					a_error += ": ";
					a_error += runtime.publicationError;
				}
				return false;
			}

			const auto contribution = std::ranges::find_if(
				published->contributions,
				[&](const ShaderReplacementRegistration& a_candidate) {
					return a_candidate.contributor == a_contributor
						&& a_candidate.targetId == registration.targetId
						&& a_candidate.stages == registration.stages
						&& a_candidate.defines == registration.defines
						&& a_candidate.slotClaims == registration.slotClaims
						&& !std::ranges::contains(
							matched, std::addressof(a_candidate));
				});
			if (contribution == published->contributions.end()) {
				a_error = "'" + std::string(metadata->name)
					+ "' lost a registered route for contributor '"
					+ std::string(a_contributor)
					+ "' (stages=" + stageNames(registration.stages)
					+ ", defines=" + defineNames(registration.defines)
					+ ", slot_claims="
					+ std::to_string(registration.slotClaims.size())
					+ ")";
				return false;
			}
			matched.push_back(std::addressof(*contribution));
		}
		if (!foundRegistration) {
			a_error = "no shader routes were registered for contributor '"
				+ std::string(a_contributor) + "'";
			return false;
		}

		a_error.clear();
		return true;
	}

	bool EnsureComputeDispatchBridgeInstalled(
		ID3D11DeviceContext* a_immediateContext) noexcept
	{
		if (!a_immediateContext) {
			L->error(
				"RunComputeShader dispatch bridge installation failed: "
				"no immediate context.");
			return false;
		}

		const auto installedTail =
			g_computeDispatchBridgeTail.load(
				std::memory_order_acquire);
		if (installedTail != 0) {
			const bool sameContext =
				g_computeDispatchContext.load(
					std::memory_order_acquire)
				== a_immediateContext;
			if (!sameContext) {
				L->error(
					"RunComputeShader dispatch bridge rejects a replacement "
					"context; device recreation is unsupported for this process.");
			}
			return sameContext && ComputeDispatchBridgeInstalled();
		}

		try {
			const auto function =
				REL::ID({ 1108829, 2276940, 2276940 }).address();
			const auto tail =
				function + kRunComputeShaderDispatchTailOffset;
			const auto text =
				REX::FModule::GetExecutingModule().GetSection(
					".text");
			const auto textBegin = text.GetAddress();
			const auto textEnd = textBegin + text.GetSize();
			if (!function
				|| !textBegin
				|| tail < textBegin
				|| tail > textEnd
				|| textEnd - tail
					< kRunComputeShaderDispatchTail.size()) {
				L->error(
					"RunComputeShader dispatch bridge installation refused: "
					"REL target + {:#x} is outside the executable .text section.",
					kRunComputeShaderDispatchTailOffset);
				return false;
			}

			if (!InstallComputeDispatchBridgeAt(
					a_immediateContext, tail)) {
				return false;
			}
		} catch (const std::exception& e) {
			L->error(
				"RunComputeShader dispatch bridge resolution failed: {}",
				e.what());
			return false;
		} catch (...) {
			L->error(
				"RunComputeShader dispatch bridge resolution failed: "
				"unknown exception.");
			return false;
		}

		L->info(
			"RunComputeShader dispatch bridge installed at {:#x} for "
			"immediate context {:#x}.",
			g_computeDispatchBridgeTail.load(
				std::memory_order_acquire),
			reinterpret_cast<std::uintptr_t>(a_immediateContext));
		return true;
	}

	bool ComputeDispatchBridgeInstalled() noexcept
	{
		const auto tail =
			g_computeDispatchBridgeTail.load(
				std::memory_order_acquire);
		if (!tail
			|| !g_computeDispatchContext.load(
				std::memory_order_acquire)) {
			return false;
		}
		return std::memcmp(
			reinterpret_cast<const void*>(tail),
			g_computeDispatchBridgePatch.data(),
			g_computeDispatchBridgePatch.size())
			== 0;
	}

	ComputeDispatchBridgeStatus GetComputeDispatchBridgeStatus() noexcept
	{
		return {
			.installed = ComputeDispatchBridgeInstalled(),
			.bridgeCalls = g_computeBridgeCalls.load(
				std::memory_order_relaxed),
			.matchingDispatches = g_computeMatchingDispatches.load(
				std::memory_order_relaxed),
			.contextRejections = g_computeContextRejections.load(
				std::memory_order_relaxed),
			.phaseRejections = g_computePhaseRejections.load(
				std::memory_order_relaxed),
			.shaderRejections = g_computeShaderRejections.load(
				std::memory_order_relaxed)
		};
	}

#ifdef FO4CS_SHADER_INJECTION_TESTING
	bool InstallComputeDispatchBridgeForTesting(
		ID3D11DeviceContext* a_context,
		std::uintptr_t a_validatedTail) noexcept
	{
		return InstallComputeDispatchBridgeAt(
			a_context, a_validatedTail);
	}
#endif

	void FreezeAndCompileShaderInjections(ID3D11Device* a_device)
	{
		auto& service = GetService();
		std::vector<ShaderReplacementRegistration> registrations;
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
		}

		auto plan = std::make_shared<PublishedPlan>();
		plan->compilationCache =
			CreateCachingShaderVariantCompilationCache();
		plan->device.copy_from(a_device);
		plan->developerSourceRoot = developerSourceRoot;
		std::size_t publishedTargets = 0;
		std::vector<FrozenTarget> frozenTargets;
		if (enabled) {
			frozenTargets = FreezeTargets(
				registrations,
				developerForceOffEnabled,
				baselineOwnership,
				developerOverrides);
		}

		if (!enabled) {
			L->warn("Shader injection disabled by core kill switch.");
		} else if (!a_device) {
			L->error("Shader injection freeze failed: no D3D11 device.");
			for (const auto& frozenTarget : frozenTargets) {
				service.runtime[ToIndex(frozenTarget.metadata->id)]
					.publicationError = "no D3D11 device";
			}
		} else {
			plan->targets.reserve(frozenTargets.size());
			for (const auto& frozenTarget : frozenTargets) {
				const auto targetIndex =
					ToIndex(frozenTarget.metadata->id);
				auto& runtime = service.runtime[targetIndex];
				if (frozenTarget.metadata->id
						== ShaderInjectionTarget::kDfTiledLighting
					&& !ComputeDispatchBridgeInstalled()) {
					runtime.publicationError =
						"RunComputeShader dispatch bridge is unavailable";
					L->error(
						"Shader injection target '{}' remains native: {}.",
						frozenTarget.metadata->name,
						runtime.publicationError);
					continue;
				}
				runtime.publicationError.clear();

				ShaderStageMask contributedStages = 0;
				for (const auto& contribution :
					frozenTarget.contributions) {
					contributedStages |= contribution.stages;
				}
				plan->targets.push_back(PublishedTarget{
					.id = frozenTarget.metadata->id,
					.contributedStages = contributedStages,
					.binds = frozenTarget.binds,
					.contributions = frozenTarget.contributions
				});
				++publishedTargets;
			}
		}

		service.published.store(plan, std::memory_order_release);
		PrepareObservedNativeVariants(plan);
		{
			std::scoped_lock lock(service.mutex);
			service.lifecycle = Lifecycle::kPublished;
		}
		if (publishedTargets == 0 && !registrations.empty()) {
			L->warn(
				"{} injection contributor(s) registered but no target was baked; all shaders remain stock.",
				registrations.size());
		}
		const auto summary = GetShaderInjectionSummary();
		L->info(
			"Shader injection freeze: targets requested={} published={} reasons(feature_contributor={}, baseline_ownership={}, developer_force_on={}); native variants compile asynchronously from observed descriptors.",
			summary.requested,
			summary.published,
			summary.requestedByFeatureContributor,
			summary.requestedByBaselineOwnership,
			summary.requestedByDeveloperForceOn);
	}

	void InvalidateNativeShaderVariantCompilations() noexcept
	{
		auto& service = GetService();
		const auto plan =
			service.published.load(std::memory_order_acquire);
		if (!plan || !plan->compilationCache)
			return;

		plan->compilationCache->Invalidate();
		std::scoped_lock lock(service.nativeVariantMutex);
		service.nativeVariants.clear();
		service.nativeShaderIdentities.clear();
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
		DispatchPublishedTarget(*target, ShaderStage::kPixel, a_context);
	}

	namespace
	{
		bool NativePixelForcesEarlyDepthStencil(
			const RE::BSGraphics::PixelShader* a_nativePixel) noexcept
		{
			if (!a_nativePixel || !a_nativePixel->shader)
				return false;
			auto& service = GetService();
			std::scoped_lock lock(service.nativeVariantMutex);
			const auto metadata = service.nativeShaderMetadata.find(
				reinterpret_cast<ID3D11DeviceChild*>(
					a_nativePixel->shader));
			return metadata != service.nativeShaderMetadata.end()
				&& metadata->second.forceEarlyDepthStencil;
		}

		void ObserveNativeShaderImpl(
			RE::BSShader* a_shader,
			bool a_hasPayload,
			bool a_allowUnvalidatedImageSpaceRuntime,
			std::optional<bool> a_modernLayoutForTesting =
				std::nullopt) noexcept
		{
			if (!a_shader || !a_hasPayload)
				return;
			try {
				const auto family =
					ResolveNativeShaderFamilyContextImpl(
						a_shader,
						a_allowUnvalidatedImageSpaceRuntime,
						a_modernLayoutForTesting);
				if (!family)
					return;
				const auto supportedStages =
					SupportedStages(family->target);
				const auto& vertexShaders = [&]()
					-> const native::VertexShaderMap& {
#ifdef FO4CS_SHADER_INJECTION_TESTING
					if (a_modernLayoutForTesting) {
						return native::VertexShadersForTesting(
							a_shader,
							*a_modernLayoutForTesting);
					}
#endif
					return native::VertexShaders(a_shader);
				}();
				const auto& pixelShaders = [&]()
					-> const native::PixelShaderMap& {
#ifdef FO4CS_SHADER_INJECTION_TESTING
					if (a_modernLayoutForTesting) {
						return native::PixelShadersForTesting(
							a_shader,
							*a_modernLayoutForTesting);
					}
#endif
					return native::PixelShaders(a_shader);
				}();
				const auto& computeShaders = [&]()
					-> const native::ComputeShaderMap& {
#ifdef FO4CS_SHADER_INJECTION_TESTING
					if (a_modernLayoutForTesting) {
						return native::ComputeShadersForTesting(
							a_shader,
							*a_modernLayoutForTesting);
					}
#endif
					return native::ComputeShaders(a_shader);
				}();
				if ((supportedStages
						& ShaderStageBit(ShaderStage::kVertex))
					!= 0) {
					for (auto* entry : vertexShaders) {
						if (!entry || !entry->shader)
							continue;
						std::ignore = ObserveNativeVariant(
							MakeNativeVariantKey(
								*family,
								ShaderStage::kVertex,
								entry->id));
					}
				}
				if ((supportedStages
						& ShaderStageBit(ShaderStage::kPixel))
					!= 0) {
					for (auto* entry : pixelShaders) {
						if (!entry || !entry->shader)
							continue;
						std::ignore = ObserveNativeVariant(
							MakeNativeVariantKey(
								*family,
								ShaderStage::kPixel,
								entry->id,
								NativePixelForcesEarlyDepthStencil(
									entry)));
					}
				}
				if ((supportedStages
						& ShaderStageBit(ShaderStage::kCompute))
					!= 0) {
					for (auto* entry : computeShaders) {
						if (!entry || !entry->shader)
							continue;
						RecordNativeComputeShader(
							MakeNativeVariantKey(
								*family,
								ShaderStage::kCompute,
								entry->id),
							reinterpret_cast<ID3D11ComputeShader*>(
								entry->shader),
							true);
					}
				}
			} catch (const std::exception& e) {
				CS_LOG_EVERY_MS(
					L,
					2000,
					spdlog::level::warn,
					"Native shader load prequeue failed: {}.",
					e.what());
			} catch (...) {
				CS_LOG_EVERY_MS(
					L,
					2000,
					spdlog::level::warn,
					"Native shader load prequeue failed.");
			}
		}

		NativeGraphicsShaderBinding ResolveNativeGraphicsShaderBindingImpl(
		const NativeShaderFamilyContext& a_family,
		std::uint32_t a_vertexShaderId,
		std::uint32_t a_pixelShaderId,
		RE::BSGraphics::VertexShader* a_nativeVertex,
		RE::BSGraphics::PixelShader* a_nativePixel) noexcept
	{
		NativeGraphicsShaderBinding result{
			.vertex = a_nativeVertex,
			.pixel = a_nativePixel
		};
		if (!IsValidTarget(a_family.target))
			return result;

		try {
			const auto plan =
				GetService().published.load(std::memory_order_acquire);
			const auto* target =
				plan ? FindPublishedTarget(*plan, a_family.target) : nullptr;
			if (!target)
				return result;

			auto& runtime =
				GetService().runtime[ToIndex(a_family.target)];
			if (a_nativeVertex
				&& a_nativeVertex->id == a_vertexShaderId) {
				runtime.matches.fetch_add(1, std::memory_order_relaxed);
				if (auto* replacement =
						AcquireNativeReplacementWrapper<
							RE::BSGraphics::VertexShader,
							ID3D11VertexShader>(
							*plan,
							*target,
							MakeNativeVariantKey(
								a_family,
								ShaderStage::kVertex,
								a_vertexShaderId),
							a_nativeVertex)) {
					result.vertex = replacement;
					runtime.substitutions.fetch_add(
						1, std::memory_order_relaxed);
				}
			}

			if (a_nativePixel
				&& a_nativePixel->id == a_pixelShaderId) {
				runtime.matches.fetch_add(1, std::memory_order_relaxed);
				if (auto* replacement =
						AcquireNativeReplacementWrapper<
							RE::BSGraphics::PixelShader,
							ID3D11PixelShader>(
							*plan,
							*target,
							MakeNativeVariantKey(
								a_family,
								ShaderStage::kPixel,
								a_pixelShaderId,
								NativePixelForcesEarlyDepthStencil(
									a_nativePixel)),
							a_nativePixel)) {
					result.pixel = replacement;
					runtime.substitutions.fetch_add(
						1, std::memory_order_relaxed);
				}
			}
		} catch (const std::exception& e) {
			CS_LOG_EVERY_MS(
				L,
				2000,
				spdlog::level::warn,
				"Native shader descriptor routing failed: {}.",
				e.what());
		} catch (...) {
			CS_LOG_EVERY_MS(
				L,
				2000,
				spdlog::level::warn,
				"Native shader descriptor routing failed.");
		}
		return result;
	}
	}

	NativeGraphicsShaderBinding ResolveNativeGraphicsShaderBinding(
		RE::BSShader* a_shader,
		std::uint32_t a_vertexShaderId,
		std::uint32_t a_pixelShaderId,
		RE::BSGraphics::VertexShader* a_nativeVertex,
		RE::BSGraphics::PixelShader* a_nativePixel) noexcept
	{
		const auto family = ResolveNativeShaderFamilyContext(a_shader);
		if (!family) {
			return {
				.vertex = a_nativeVertex,
				.pixel = a_nativePixel
			};
		}
		return ResolveNativeGraphicsShaderBindingImpl(
			*family,
			a_vertexShaderId,
			a_pixelShaderId,
			a_nativeVertex,
			a_nativePixel);
	}

	RE::BSGraphics::ComputeShader* ResolveNativeComputeShaderBinding(
		RE::BSGraphics::ComputeShader* a_nativeCompute) noexcept
	{
		if (!a_nativeCompute || !a_nativeCompute->shader)
			return a_nativeCompute;
		try {
			const auto plan =
				GetService().published.load(std::memory_order_acquire);
			if (!plan)
				return a_nativeCompute;

			std::optional<NativeVariantKey> owner;
			{
				auto& service = GetService();
				std::scoped_lock lock(service.nativeVariantMutex);
				const auto found = service.nativeComputeOwners.find(
					reinterpret_cast<ID3D11ComputeShader*>(
						a_nativeCompute->shader));
				if (found != service.nativeComputeOwners.end())
					owner = found->second;
			}
			if (!owner)
				return a_nativeCompute;
			const auto* target =
				FindPublishedTarget(*plan, owner->target);
			if (!target)
				return a_nativeCompute;

			const bool hasFeatureBindings =
				(target->contributedStages
					& ShaderStageBit(ShaderStage::kCompute))
				!= 0;
			if (hasFeatureBindings
				&& (owner->target
						!= ShaderInjectionTarget::kDfTiledLighting
					|| !render::IsDeferredLightsActive()
					|| !ComputeDispatchBridgeInstalled())) {
				g_computePhaseRejections.fetch_add(
					1, std::memory_order_relaxed);
				return a_nativeCompute;
			}

			auto& runtime =
				GetService().runtime[ToIndex(owner->target)];
			runtime.matches.fetch_add(1, std::memory_order_relaxed);
			auto* replacement =
				AcquireNativeReplacementWrapper<
					RE::BSGraphics::ComputeShader,
					ID3D11ComputeShader>(
					*plan,
					*target,
					*owner,
					a_nativeCompute);
			if (!replacement)
				return a_nativeCompute;
			runtime.substitutions.fetch_add(
				1, std::memory_order_relaxed);
			return replacement;
		} catch (const std::exception& e) {
			CS_LOG_EVERY_MS(
				L,
				2000,
				spdlog::level::warn,
				"Native compute descriptor routing failed: {}.",
				e.what());
		} catch (...) {
			CS_LOG_EVERY_MS(
				L,
				2000,
				spdlog::level::warn,
				"Native compute descriptor routing failed.");
		}
		return a_nativeCompute;
	}

	void ObserveNativeShader(
		RE::BSShader* a_shader,
		bool a_hasPayload) noexcept
	{
		ObserveNativeShaderImpl(
			a_shader, a_hasPayload, false);
	}

	void ObserveNativeComputeOwner(
		const void* a_owner,
		ShaderInjectionTarget a_target,
		std::string_view a_nativeName,
		bool a_hasPayload) noexcept
	{
		if (!a_owner || !IsValidTarget(a_target))
			return;
		try {
			for (auto* entry : native::StandaloneComputeShaders(a_owner)) {
				if (!entry || !entry->shader)
					continue;
				const bool prequeue =
					a_hasPayload
					&& a_target
						== ShaderInjectionTarget::kDfTiledLighting
					&& a_nativeName == "DFTiledLighting"
					&& IsDfTiledStandaloneLoadTechnique(entry->id);
				RecordNativeComputeShader(
					MakeNativeVariantKey({
						.target = a_target,
						.stage = ShaderStage::kCompute,
						.descriptor = entry->id,
						.nativeName = a_nativeName
					}),
					reinterpret_cast<ID3D11ComputeShader*>(
						entry->shader),
					prequeue);
			}
		} catch (...) {
			CS_LOG_EVERY_MS(
				L,
				2000,
				spdlog::level::warn,
				"Native standalone compute observation failed.");
		}
	}

	void ObserveNativeShaderBytecode(
		ShaderStage a_stage,
		const void* a_bytecode,
		std::size_t a_bytecodeLength,
		ID3D11DeviceChild* a_shader) noexcept
	{
		if (a_stage != ShaderStage::kPixel
			|| !a_bytecode || a_bytecodeLength == 0 || !a_shader) {
			return;
		}
		winrt::com_ptr<ID3D11ShaderReflection> reflection;
		if (FAILED(D3DReflect(
				a_bytecode,
				a_bytecodeLength,
				IID_PPV_ARGS(reflection.put())))) {
			return;
		}
		Service::NativeShaderMetadata metadata;
		metadata.forceEarlyDepthStencil =
			(reflection->GetRequiresFlags()
				& D3D_SHADER_REQUIRES_EARLY_DEPTH_STENCIL)
			!= 0;
		auto& service = GetService();
		std::scoped_lock lock(service.nativeVariantMutex);
		service.nativeShaderMetadata.insert_or_assign(a_shader, metadata);
	}

#ifdef FO4CS_SHADER_INJECTION_TESTING
	std::optional<NativeShaderMetadataForTesting>
		GetObservedNativeShaderMetadataForTesting(
			ID3D11DeviceChild* a_shader) noexcept
	{
		if (!a_shader)
			return std::nullopt;
		auto& service = GetService();
		std::scoped_lock lock(service.nativeVariantMutex);
		const auto metadata = service.nativeShaderMetadata.find(a_shader);
		if (metadata == service.nativeShaderMetadata.end())
			return std::nullopt;
		return NativeShaderMetadataForTesting{
			.forceEarlyDepthStencil =
				metadata->second.forceEarlyDepthStencil
		};
	}

	ID3D11DeviceChild* PrepareNativeShaderVariantForTesting(
		const ShaderFamilyDescriptor& a_descriptor) noexcept
	{
		try {
			const auto plan =
				GetService().published.load(std::memory_order_acquire);
			const auto* target =
				plan ? FindPublishedTarget(*plan, a_descriptor.target) : nullptr;
			if (!target)
				return nullptr;
			switch (a_descriptor.stage) {
			case ShaderStage::kVertex:
				return AcquireNativeReplacement<ID3D11VertexShader>(
					*plan,
					*target,
					MakeNativeVariantKey(a_descriptor));
			case ShaderStage::kPixel:
				return AcquireNativeReplacement<ID3D11PixelShader>(
					*plan,
					*target,
					MakeNativeVariantKey(a_descriptor));
			case ShaderStage::kCompute:
				return AcquireNativeReplacement<ID3D11ComputeShader>(
					*plan,
					*target,
					MakeNativeVariantKey(a_descriptor));
			default:
				return nullptr;
			}
		} catch (...) {
			return nullptr;
		}
	}

	bool QueueNativeShaderVariantForTesting(
		const ShaderFamilyDescriptor& a_descriptor) noexcept
	{
		try {
			const auto plan =
				GetService().published.load(std::memory_order_acquire);
			const auto* target =
				plan ? FindPublishedTarget(*plan, a_descriptor.target) : nullptr;
			return target
				&& static_cast<bool>(
					FindOrPrepareNativeVariant(
						*plan,
						*target,
						MakeNativeVariantKey(a_descriptor)));
		} catch (...) {
			return false;
		}
	}

	NativeGraphicsShaderBinding
		ResolveNativeGraphicsShaderBindingForTesting(
			ShaderInjectionTarget a_target,
			std::string_view a_nativeName,
			std::uint32_t a_vertexShaderId,
			std::uint32_t a_pixelShaderId,
			RE::BSGraphics::VertexShader* a_nativeVertex,
			RE::BSGraphics::PixelShader* a_nativePixel) noexcept
	{
		return ResolveNativeGraphicsShaderBindingImpl(
			{
				.target = a_target,
				.nativeName = a_nativeName
			},
			a_vertexShaderId,
			a_pixelShaderId,
			a_nativeVertex,
			a_nativePixel);
	}

	NativeGraphicsShaderBinding
		ResolveNativeGraphicsShaderBindingForDescriptorTesting(
			const ShaderFamilyDescriptor& a_descriptor,
			std::uint32_t a_vertexShaderId,
			std::uint32_t a_pixelShaderId,
			RE::BSGraphics::VertexShader* a_nativeVertex,
			RE::BSGraphics::PixelShader* a_nativePixel) noexcept
	{
		return ResolveNativeGraphicsShaderBindingImpl(
			{
				.target = a_descriptor.target,
				.nativeName = a_descriptor.nativeName,
				.nativeClassName = a_descriptor.nativeClassName,
				.nativeSourceGroup = a_descriptor.nativeSourceGroup,
				.nativeMacros = a_descriptor.nativeMacros
			},
			a_vertexShaderId,
			a_pixelShaderId,
			a_nativeVertex,
			a_nativePixel);
	}

	RE::BSGraphics::VertexShader*
		CacheNativeVertexReplacementWrapperForTesting(
			RE::BSGraphics::VertexShader* a_nativeVertex,
			ID3D11VertexShader* a_replacement) noexcept
	{
		return CacheNativeReplacementWrapper(
			a_nativeVertex, a_replacement);
	}

	NativeVariantCacheStatsForTesting
		GetNativeVariantCacheStatsForTesting() noexcept
	{
		NativeVariantCacheStatsForTesting result;
		auto& service = GetService();
		std::scoped_lock lock(service.nativeVariantMutex);
		result.entries = service.nativeVariants.size();
		for (const auto& [key, variant] : service.nativeVariants) {
			(void)key;
			if (!variant)
				continue;
			switch (variant->state.load(std::memory_order_acquire)) {
			case NativeVariant::State::kUnsupported:
				++result.unsupported;
				break;
			case NativeVariant::State::kCompilation:
				++result.compilation;
				break;
			case NativeVariant::State::kResolving:
				break;
			}
		}
		return result;
	}

	void ObserveNativeComputeShaderForTesting(
		ShaderInjectionTarget a_target,
		std::uint32_t a_descriptor,
		std::string_view a_nativeName,
		ID3D11ComputeShader* a_shader) noexcept
	{
		try {
			RecordNativeComputeShader(
				MakeNativeVariantKey({
					.target = a_target,
					.stage = ShaderStage::kCompute,
					.descriptor = a_descriptor,
					.nativeName = a_nativeName
				}),
				a_shader,
				false);
		} catch (...) {
		}
	}

	void ObserveNativeShaderForTesting(
		RE::BSShader* a_shader,
		const RE::BSIStream* a_stream,
		bool a_modernLayout) noexcept
	{
		ObserveNativeShaderImpl(
			a_shader,
			native::ShaderArchiveStreamHasPayload(a_stream),
			true,
			a_modernLayout);
	}

	void ObserveNativeComputeOwnerLoadForTesting(
		const void* a_owner,
		ShaderInjectionTarget a_target,
		const RE::BSIStream* a_stream) noexcept
	{
		if (!a_owner)
			return;
		const auto* ownerName =
			native::StandaloneComputeOwnerName(a_owner);
		const std::string_view nativeName = ownerName ? ownerName : "";
		ObserveNativeComputeOwner(
			a_owner,
			a_target,
			nativeName,
			native::ShaderArchiveStreamHasPayload(a_stream));
	}
#endif

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

		auto& service = GetService();
		std::shared_ptr<NativeVariant> activeVariant;
		{
			std::scoped_lock lock(service.nativeVariantMutex);
			const auto identity =
				service.nativeShaderIdentities.find(boundShader);
			if (identity != service.nativeShaderIdentities.end()) {
				const auto variant =
					service.nativeVariants.find(identity->second);
				if (variant != service.nativeVariants.end())
					activeVariant = variant->second;
			}
		}
		if (activeVariant) {
			const ActiveVariantScope scope(activeVariant.get());
			DispatchShaderInjections(activeVariant->target, a_context);
		}
		boundShader->Release();
	}

	bool ActiveShaderInjectionVariantHasDefine(
		ShaderInjectionTarget a_target,
		std::string_view a_define) noexcept
	{
		const auto* defines = GetActiveShaderInjectionVariantDefines(a_target);
		return defines && defines->contains(a_define);
	}

	const ShaderInjectionDefines* GetActiveShaderInjectionVariantDefines(
		ShaderInjectionTarget a_target) noexcept
	{
		return t_activeTarget == a_target ? t_activeDefines : nullptr;
	}

	ShaderInjectionOutcomeSnapshot GetShaderInjectionOutcomeSnapshot(
		ShaderInjectionTarget a_target) noexcept
	{
		ShaderInjectionOutcomeSnapshot snapshot;
		if (!IsValidTarget(a_target))
			return snapshot;
		auto& service = GetService();
		const auto& runtime = service.runtime[ToIndex(a_target)];
		SwapCountersGuard counterGuard(service);
		snapshot.matches = runtime.matches.load(std::memory_order_relaxed);
		snapshot.substitutions =
			runtime.substitutions.load(std::memory_order_relaxed);
		return snapshot;
	}

	ShaderInjectionTargetSnapshot GetShaderInjectionTargetSnapshot(
		ShaderInjectionTarget a_target)
	{
		ShaderInjectionTargetSnapshot snapshot;
		if (!IsValidTarget(a_target))
			return snapshot;

		auto& service = GetService();
		{
			std::scoped_lock lock(service.mutex);
			const auto& metadata = kTargets[ToIndex(a_target)];
			const auto& runtime = service.runtime[ToIndex(a_target)];
			const auto plan =
				service.published.load(std::memory_order_acquire);
			snapshot.id = a_target;
			snapshot.name = metadata.name;
			snapshot.requested =
				runtime.requested.load(std::memory_order_relaxed);
			snapshot.published =
				plan && FindPublishedTarget(*plan, a_target);
			snapshot.slotCollision =
				runtime.slotCollision.load(std::memory_order_relaxed);
			snapshot.developerOverride = runtime.developerOverride;
			snapshot.requestReasons =
				static_cast<ShaderInjectionRequestReason>(
					runtime.requestReasons.load(
						std::memory_order_relaxed));
			snapshot.contributors =
				runtime.contributors.load(std::memory_order_relaxed);
			snapshot.defines = runtime.defines;
			snapshot.publicationError = runtime.publicationError;
			{
				SwapCountersGuard counterGuard(service);
				snapshot.matches =
					runtime.matches.load(std::memory_order_relaxed);
				snapshot.substitutions =
					runtime.substitutions.load(std::memory_order_relaxed);
				snapshot.compileFailures =
					runtime.compileFailures.load(
						std::memory_order_relaxed);
				snapshot.passthroughNotReady =
					runtime.passthroughNotReady.load(
						std::memory_order_relaxed);
				snapshot.passthroughDisabled =
					runtime.passthroughDisabled.load(
						std::memory_order_relaxed);
			}
			snapshot.dispatches =
				runtime.dispatches.load(std::memory_order_relaxed);
		}
		const auto observations =
			CollectNativeVariantObservations(a_target);
		snapshot.variantsObserved = observations.observed;
		snapshot.variantsPending = observations.pending;
		snapshot.variantsReady = observations.ready;
		snapshot.variantsFailed = observations.failed;
		snapshot.variantsUnsupported = observations.unsupported;
		return snapshot;
	}

	ShaderInjectionSummary GetShaderInjectionSummary() noexcept
	{
		ShaderInjectionSummary summary;
		auto& service = GetService();
		const auto plan =
			service.published.load(std::memory_order_acquire);
		summary.published = plan ? plan->targets.size() : 0;
		{
			SwapCountersGuard counterGuard(service);
			for (const auto& runtime : service.runtime) {
				if (runtime.requested.load(std::memory_order_relaxed))
					++summary.requested;
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
				summary.compileFailures +=
					runtime.compileFailures.load(
						std::memory_order_relaxed);
				summary.passthroughNotReady +=
					runtime.passthroughNotReady.load(
						std::memory_order_relaxed);
				summary.passthroughDisabled +=
					runtime.passthroughDisabled.load(
						std::memory_order_relaxed);
				summary.dispatches += runtime.dispatches.load(std::memory_order_relaxed);
			}
		}
		const auto observations = CollectNativeVariantObservations();
		summary.variantsObserved = observations.observed;
		summary.variantsPending = observations.pending;
		summary.variantsReady = observations.ready;
		summary.variantsFailed = observations.failed;
		summary.variantsUnsupported = observations.unsupported;
		summary.computeBridge = GetComputeDispatchBridgeStatus();
		return summary;
	}

}

#include "Render/ShaderInjection.h"

#include "Log.h"
#include "LogThrottle.h"
#include "Render/Engine.h"
#include "Render/NativeShaderFamily.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderFamilyDescriptor.h"
#include "Render/ShaderVariantCompilation.h"
#include "Render/SharedData.h"
#include "Settings/FeatureConfig.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <exception>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace cs::engine
{
	namespace
	{
		thread_local ScopedPixelShaderInjectionBindings* t_pixelBindings = nullptr;
		thread_local std::uint16_t t_samplerMask = UINT16_MAX;
		thread_local ShaderInjectionDrawMetrics t_drawMetrics;
		thread_local bool t_drawFrameStarted = false;
		std::mutex g_drawMetricsMutex;
		ShaderInjectionDrawMetrics g_completedDrawMetrics;

		void CapturePixelBindings(ID3D11DeviceContext* a_context, ShaderResourceType a_type, UINT a_start, UINT a_count) noexcept
		{
			if (!t_pixelBindings)
				return;
			for (UINT slot = a_start; slot < a_start + a_count; ++slot) {
				t_pixelBindings->Capture(a_context, a_type, slot);
			}
		}
	}

	void BindInjectionShaderResources(ID3D11DeviceContext* a_context, UINT a_start, UINT a_count, ID3D11ShaderResourceView* const* a_values) noexcept
	{
		CapturePixelBindings(a_context, ShaderResourceType::kShaderResource, a_start, a_count);
		a_context->PSSetShaderResources(a_start, a_count, a_values);
		RecordShaderInjectionD3DBinds();
	}

	void BindInjectionSamplers(ID3D11DeviceContext* a_context, UINT a_start, UINT a_count, ID3D11SamplerState* const* a_values) noexcept
	{
		for (UINT slot = a_start; slot < a_start + a_count; ++slot) {
			if ((t_samplerMask & (1u << slot)) == 0)
				continue;
			if (t_pixelBindings) {
				t_pixelBindings->BindSampler(a_context, slot, a_values[slot - a_start]);
			} else {
				a_context->PSSetSamplers(slot, 1, &a_values[slot - a_start]);
				RecordShaderInjectionD3DBinds();
			}
		}
	}

	void BindInjectionConstantBuffers(ID3D11DeviceContext* a_context, UINT a_start, UINT a_count, ID3D11Buffer* const* a_values) noexcept
	{
		CapturePixelBindings(a_context, ShaderResourceType::kConstantBuffer, a_start, a_count);
		a_context->PSSetConstantBuffers(a_start, a_count, a_values);
		RecordShaderInjectionD3DBinds();
	}

	void CaptureShaderInjectionOutputs(ID3D11DeviceContext* a_context) noexcept
	{
		CapturePixelBindings(a_context, ShaderResourceType::kRenderTarget, 0, 1);
	}

	void BeginShaderInjectionFrame(std::uint32_t a_frame) noexcept
	{
		if (t_drawFrameStarted && t_drawMetrics.frame == a_frame)
			return;
		{
			std::scoped_lock lock(g_drawMetricsMutex);
			t_drawMetrics.frameBindings = GetFrameBindingMetrics();
			g_completedDrawMetrics = t_drawMetrics;
		}
		t_drawMetrics = { .frame = a_frame };
		ResetFrameBindings();
		t_drawFrameStarted = true;
	}

	void RecordShaderInjectionD3DBinds(std::uint32_t a_count) noexcept
	{
		if (t_pixelBindings)
			t_drawMetrics.d3dBinds += a_count;
	}

	ScopedShaderInjectionBindings::ScopedShaderInjectionBindings(ShaderStage a_stage) noexcept :
		_stage(a_stage),
		_previous(a_stage == ShaderStage::kPixel ? std::exchange(t_pixelBindings, this) : nullptr),
		_started(std::chrono::steady_clock::now())
	{}

	ScopedShaderInjectionBindings::~ScopedShaderInjectionBindings() noexcept
	{
		const auto finish = [this] {
			if (_stage == ShaderStage::kPixel) {
				t_pixelBindings = _previous;
				if (!_previous) {
					++t_drawMetrics.scopes;
					t_drawMetrics.scopeNanoseconds += static_cast<std::uint64_t>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(
							std::chrono::steady_clock::now() - _started)
							.count());
				}
			}
		};
		const auto restores = _bufferCount + _resourceCount + _samplerCount + (_outputCaptured ? 2u : 0u);
		if (_stage == ShaderStage::kPixel) {
			t_drawMetrics.restores += restores;
			t_drawMetrics.d3dBinds += restores;
		}
		if (!_context) {
			finish();
			return;
		}
		if (_outputCaptured) {
			_context->OMSetRenderTargets(8, _targets, _depth);
			_context->OMSetBlendState(_blend, _blendFactor, _sampleMask);
			for (auto* target : _targets)
				if (target)
					target->Release();
			if (_depth)
				_depth->Release();
			if (_blend)
				_blend->Release();
		}
		for (const auto& buffer : std::span(_buffers).first(_bufferCount)) {
			if (_stage == ShaderStage::kCompute)
				_context->CSSetConstantBuffers(buffer.slot, 1, &buffer.value);
			else
				_context->PSSetConstantBuffers(buffer.slot, 1, &buffer.value);
			if (buffer.value)
				buffer.value->Release();
		}
		for (const auto& resource : std::span(_resources).first(_resourceCount)) {
			if (_stage == ShaderStage::kCompute)
				_context->CSSetShaderResources(resource.slot, 1, &resource.value);
			else
				_context->PSSetShaderResources(resource.slot, 1, &resource.value);
			if (resource.value)
				resource.value->Release();
		}
		for (const auto& sampler : std::span(_samplers).first(_samplerCount)) {
			if (_stage == ShaderStage::kCompute)
				_context->CSSetSamplers(sampler.slot, 1, &sampler.value);
			else
				_context->PSSetSamplers(sampler.slot, 1, &sampler.value);
			if (sampler.value)
				sampler.value->Release();
		}
		finish();
	}

	void ScopedShaderInjectionBindings::Capture(
		ID3D11DeviceContext* a_context, ShaderResourceType a_type, std::uint32_t a_slot) noexcept
	{
		if (!a_context)
			return;
		if (!_context)
			_context = a_context;
		{
			// Engine-facts: PS shadow state owns t0-t15/s0-s15 and b0-b2/b12/b13 only.
			if (_stage == ShaderStage::kPixel &&
				((a_type == ShaderResourceType::kShaderResource && a_slot >= 16) ||
					(a_type == ShaderResourceType::kConstantBuffer && a_slot > 2 && a_slot != 12 && a_slot != 13)))
				return;
			if (a_type == ShaderResourceType::kRenderTarget && !_outputCaptured) {
				_context->OMGetRenderTargets(8, _targets, &_depth);
				_context->OMGetBlendState(&_blend, _blendFactor, &_sampleMask);
				_outputCaptured = true;
				if (_stage == ShaderStage::kPixel)
					t_drawMetrics.captures += 2;
			}
			if (a_type == ShaderResourceType::kShaderResource &&
				std::ranges::none_of(std::span(_resources).first(_resourceCount), [&](const auto& r) { return r.slot == a_slot; })) {
				auto& resource = _resources[_resourceCount++];
				resource = { a_slot, nullptr };
				if (_stage == ShaderStage::kPixel)
					++t_drawMetrics.captures;
				if (_stage == ShaderStage::kCompute)
					_context->CSGetShaderResources(a_slot, 1, &resource.value);
				else
					_context->PSGetShaderResources(a_slot, 1, &resource.value);
			} else if (a_type == ShaderResourceType::kSampler &&
					   std::ranges::none_of(std::span(_samplers).first(_samplerCount), [&](const auto& s) { return s.slot == a_slot; })) {
				auto& sampler = _samplers[_samplerCount++];
				sampler = { a_slot, nullptr, nullptr };
				if (_stage == ShaderStage::kPixel)
					++t_drawMetrics.captures;
				if (_stage == ShaderStage::kCompute)
					_context->CSGetSamplers(a_slot, 1, &sampler.value);
				else
					_context->PSGetSamplers(a_slot, 1, &sampler.value);
				sampler.current = sampler.value;
			} else if (a_type == ShaderResourceType::kConstantBuffer &&
					   std::ranges::none_of(std::span(_buffers).first(_bufferCount), [&](const auto& b) { return b.slot == a_slot; })) {
				auto& buffer = _buffers[_bufferCount++];
				buffer = { a_slot, nullptr };
				if (_stage == ShaderStage::kPixel)
					++t_drawMetrics.captures;
				if (_stage == ShaderStage::kCompute)
					_context->CSGetConstantBuffers(a_slot, 1, &buffer.value);
				else
					_context->PSGetConstantBuffers(a_slot, 1, &buffer.value);
			}
		}
	}

	void ScopedShaderInjectionBindings::BindSampler(
		ID3D11DeviceContext* a_context, std::uint32_t a_slot, ID3D11SamplerState* a_sampler) noexcept
	{
		auto samplers = std::span(_samplers).first(_samplerCount);
		const auto found = std::ranges::find(samplers, a_slot, &Sampler::slot);
		auto* binding = found == samplers.end() ? nullptr : &*found;
		if (!binding) {
			ID3D11SamplerState* original = nullptr;
			a_context->PSGetSamplers(a_slot, 1, &original);
			if (original == a_sampler) {
				if (original)
					original->Release();
				return;
			}
			_context = a_context;
			binding = &_samplers[_samplerCount++];
			*binding = { a_slot, original, original };
			++t_drawMetrics.captures;
		}
		if (binding->current == a_sampler)
			return;
		// AE SetDirtyStates (0x18247D0) skips clean slots; overrides must not outlive this draw.
		a_context->PSSetSamplers(a_slot, 1, &a_sampler);
		binding->current = a_sampler;
		RecordShaderInjectionD3DBinds();
	}

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

		constexpr ShaderStageMask kValidShaderStages =
			ShaderStageBit(ShaderStage::kVertex) | ShaderStageBit(ShaderStage::kPixel) | ShaderStageBit(ShaderStage::kCompute);

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
			std::atomic<bool> requested{ false };
			std::atomic<std::size_t> contributors{ 0 };
			std::atomic<std::uint64_t> computeBindCalls{ 0 };
			std::atomic<std::uint64_t> matches{ 0 };
			std::atomic<std::uint64_t> substitutions{ 0 };
			std::atomic<std::uint64_t> passthroughCompileFail{ 0 };
			std::atomic<std::uint64_t> passthroughNotReady{ 0 };
			std::atomic<std::uint64_t> passthroughDisabled{ 0 };
			std::atomic<std::uint64_t> dispatches{ 0 };
			DeveloperShaderOverride developerOverride = DeveloperShaderOverride::kAuto;
			ShaderInjectionDefines defines;
			std::string publicationError;
		};

		struct FrozenTarget
		{
			const ShaderInjectionTargetMetadata* metadata = nullptr;
			ShaderInjectionDefines defines;
			std::vector<ShaderReplacementRegistration> contributions;
			struct Bind
			{
				ShaderStageMask stages = 0;
				ShaderInjectionBindCallback callback;
			};
			std::vector<Bind> binds;
			std::size_t contributors = 0;
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
					value = value * 131U + std::hash<std::string>{}(name);
					value = value * 131U + std::hash<std::string>{}(macroValue);
				}
				return value;
			}
		};

		struct NativeVariant
		{
			mutable std::mutex mutex;
			std::shared_ptr<ShaderVariantCompilationHandle> compilation;
			ShaderInjectionDefines effectiveDefines;
			ShaderInjectionTarget target = ShaderInjectionTarget::kCount;
			ShaderStage stage = ShaderStage::kPixel;
			std::atomic<bool> failureObserved{ false };
			std::atomic<bool> pendingObserved{ false };
			std::atomic<std::uint16_t> samplerMask{ UINT16_MAX };
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
				value = value * 131U + std::hash<const void*>{}(
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
		struct Service
		{
			Service()
			{
				for (auto& target : baselineOwnership)
					target.store(true, std::memory_order_relaxed);
			}

			std::mutex mutex;
			Lifecycle lifecycle = Lifecycle::kCollecting;
			std::atomic_bool enabled = true;
			bool developerForceOffEnabled = false;
			std::wstring developerSourceRoot;
			std::array<std::atomic_bool,
				static_cast<std::size_t>(ShaderInjectionTarget::kCount)>
				baselineOwnership{};
			std::array<DeveloperShaderOverride,
				static_cast<std::size_t>(ShaderInjectionTarget::kCount)>
				developerOverrides{};
			std::vector<ShaderReplacementRegistration> registrations;
			std::array<TargetRuntimeState,
				static_cast<std::size_t>(ShaderInjectionTarget::kCount)>
				runtime;
			std::atomic<std::shared_ptr<const PublishedPlan>> published;
			std::mutex nativeVariantMutex;
			std::map<std::pair<std::filesystem::path, std::string>, bool>
				shaderSourceAvailability;
			std::unordered_map<NativeVariantKey, std::shared_ptr<NativeVariant>,
				NativeVariantKeyHash>
				nativeVariants;
			std::array<bool,
				static_cast<std::size_t>(ShaderInjectionTarget::kCount)>
				unsupportedNativeVariantReported{};
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

		bool ShaderEnabled(ShaderInjectionTarget a_target)
		{
			auto& service = GetService();
			return IsValidTarget(a_target) &&
			       service.enabled.load(std::memory_order_relaxed) &&
			       service.baselineOwnership[ToIndex(a_target)].load(std::memory_order_relaxed);
		}

		const char* NativeShaderFilename(const RE::BSShader* a_shader)
		{
			return native::FxpFilename(a_shader);
		}

		std::optional<ShaderInjectionTarget> ResolveNativeShaderTarget(
			const RE::BSShader& a_shader)
		{
			const auto* name = NativeShaderFilename(&a_shader);
			return ResolveGraphicsShaderTarget(
				static_cast<RE::BSShaderManager::ShaderEnum>(native::ShaderType(&a_shader)),
				name ? name : "");
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
		ResolveNativeShaderFamilyContext(RE::BSShader* a_shader)
		{
			if (!a_shader)
				return std::nullopt;
			const auto target = ResolveNativeShaderTarget(*a_shader);
			if (!target)
				return std::nullopt;
			const auto nativeName = NativeShaderFilename(a_shader);

			NativeShaderFamilyContext result{
				.target = *target,
				.nativeName = nativeName ? nativeName : ""
			};
			if (*target != ShaderInjectionTarget::kImageSpace)
				return result;

			const char* sourceGroup = nullptr;
			const char* className = nullptr;
			sourceGroup = native::ImageSpaceShaderPrefix(a_shader);
			className = native::ImageSpaceShaderClassName(a_shader);
			const auto emitted = native::GetImageSpaceMacros(a_shader);
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
			return MakeNativeVariantKey({ .target = a_family.target,
				.stage = a_stage,
				.descriptor = a_descriptor,
				.nativeName = a_family.nativeName,
				.nativeClassName = a_family.nativeClassName,
				.nativeSourceGroup = a_family.nativeSourceGroup,
				.forceEarlyDepthStencil = a_forceEarlyDepthStencil,
				.nativeMacros = a_family.nativeMacros });
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
			const auto* metadata = GetShaderInjectionTarget(a_target);
			return metadata ? metadata->supportedStages : 0;
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

		void LogLateMutation(std::string_view a_operation)
		{
			L->warn("{} rejected after shader-injection freeze; restart required.", a_operation);
		}

		ShaderInjectionDefines GetDefines(const ShaderReplacementRegistration& a_registration)
		{
			const auto* feature = a_registration.feature;
			if (!feature || !feature->IsLoaded() || !feature->HasShaderDefine(a_registration.targetId))
				return {};
			ShaderInjectionDefines defines{ { std::string(feature->GetShaderDefineName()), "1" } };
			for (const auto& [name, value] : feature->GetShaderDefineOptions(a_registration.targetId))
				defines.emplace(name, value);
			return defines;
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
				const auto targetIndex = ToIndex(metadata.id);
				auto developerOverride = a_developerOverrides[targetIndex];
				if (developerOverride == DeveloperShaderOverride::kForceOff && !a_developerForceOffEnabled)
					developerOverride = DeveloperShaderOverride::kAuto;

				for (std::size_t registrationIndex = 0;
					registrationIndex < a_registrations.size();
					++registrationIndex) {
					const auto& registration = a_registrations[registrationIndex];
					if (registration.targetId != metadata.id)
						continue;
					if (registration.feature && !registration.feature->IsLoaded()) {
						continue;
					}

					++target.contributors;
					const auto defines = GetDefines(registration);
					target.defines.insert(defines.begin(), defines.end());
					target.contributions.push_back(registration);
					if (registration.bind) {
						target.binds.push_back({ registration.stages,
							registration.bind });
					}
				}

				const bool requested =
					developerOverride != DeveloperShaderOverride::kForceOff;

				for (const auto& contribution : target.contributions) {
					const bool stageMatched =
						(contribution.stages & SupportedStages(metadata.id)) != 0;
					if (!stageMatched) {
						L->warn(
							"Contributor '{}' for '{}' targets no registered shader stages; defines ignored.",
							contribution.contributor.empty() ? "<unnamed>" : contribution.contributor,
							metadata.name);
					}
				}

				auto& runtime = GetService().runtime[targetIndex];
				runtime.requested.store(requested, std::memory_order_relaxed);
				runtime.contributors.store(target.contributors, std::memory_order_relaxed);
				runtime.developerOverride = developerOverride;
				runtime.defines = target.defines;

				if (requested)
					frozen.push_back(std::move(target));
			}
			return frozen;
		}

		std::filesystem::path ResolveShaderRoot(
			DeveloperShaderOverride a_developerOverride,
			const std::wstring& a_developerSourceRoot)
		{
			const bool useDeveloperRoot =
				a_developerOverride == DeveloperShaderOverride::kForceOn && !a_developerSourceRoot.empty();
			return useDeveloperRoot ? a_developerSourceRoot : kDefaultShaderRoot;
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

		std::shared_ptr<NativeVariant> FindOrPrepareNativeVariant(
			const PublishedPlan& a_plan,
			const PublishedTarget& a_target,
			const NativeVariantKey& a_key)
		{
			auto& service = GetService();
			const auto shaderRoot = ResolveShaderRoot(
				service.runtime[ToIndex(a_key.target)].developerOverride,
				a_plan.developerSourceRoot);
			std::shared_ptr<NativeVariant> candidate;
			{
				std::scoped_lock lock(service.nativeVariantMutex);
				const auto entry = service.nativeVariants.find(a_key);
				if (entry != service.nativeVariants.end())
					return entry->second;
				const auto [source, inserted] = service.shaderSourceAvailability.try_emplace(
					std::make_pair(shaderRoot, a_key.nativeName), false);
				if (inserted)
					source->second = IsShaderSourceAvailable(shaderRoot, a_key.nativeName);
				if (!source->second)
					return nullptr;
				candidate = std::make_shared<NativeVariant>();
				candidate->target = a_key.target;
				candidate->stage = a_key.stage;
				service.nativeVariants.emplace(a_key, candidate);
			}

			const auto descriptor = DescribeNativeVariant(a_key);
			auto family = BuildShaderFamilyCompilationDescriptor(descriptor);
			if (!family) {
				bool reportUnsupported = false;
				if (IsValidTarget(descriptor.target)) {
					std::scoped_lock lock(service.nativeVariantMutex);
					const auto entry = service.nativeVariants.find(a_key);
					// An invalidated candidate must not consume the new generation's notice.
					if (entry != service.nativeVariants.end() && entry->second == candidate) {
						reportUnsupported = !std::exchange(
							service.unsupportedNativeVariantReported[ToIndex(descriptor.target)],
							true);
					}
				}
				if (reportUnsupported) {
					L->info(
						"{}: native variants without a reconstruction stay stock (first: {}, stage {}, descriptor {:#010x})",
						kTargets[ToIndex(descriptor.target)].name,
						descriptor.nativeName,
						static_cast<unsigned>(descriptor.stage),
						descriptor.descriptor);
				}
				return candidate;
			}

			std::string error;
			const auto* metadata =
				GetShaderInjectionTarget(descriptor.target);
			if (!metadata) {
				return candidate;
			}
			auto effective = BuildEffectiveShaderCompileRequest(
				*metadata,
				descriptor.stage,
				*family,
				a_target.contributions,
				&error);
			if (!effective) {
				L->error(
					"Native descriptor compile request rejected for '{}/{}/{:#010x}': {}",
					metadata->name,
					StageName(descriptor.stage),
					descriptor.descriptor,
					error);
				return candidate;
			}

			ShaderVariantCompilationRequest request;
			request.device = a_plan.device;
			request.sourcePath = shaderRoot / effective->sourcePath;
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

			auto compilation =
				a_plan.compilationCache->Request(std::move(request));
			if (!compilation) {
				L->error(
					"Native descriptor compile failed for '{}/{}/{:#010x}': {}",
					metadata->name,
					StageName(descriptor.stage),
					descriptor.descriptor,
					"compilation cache rejected request");
				service.runtime[ToIndex(descriptor.target)]
					.passthroughCompileFail.fetch_add(
						1, std::memory_order_relaxed);
				return candidate;
			}

			{
				std::scoped_lock lock(candidate->mutex);
				candidate->compilation = std::move(compilation);
				candidate->effectiveDefines =
					std::move(effective->defines);
			}
			return candidate;
		}

		bool QueueNativeVariant(const NativeVariantKey& a_key)
		{
			if (!IsValidTarget(a_key.target))
				return false;
			const auto plan =
				GetService().published.load(std::memory_order_acquire);
			if (!plan)
				return false;
			const auto* target =
				FindPublishedTarget(*plan, a_key.target);
			if (!target)
				return false;
			return static_cast<bool>(
				FindOrPrepareNativeVariant(*plan, *target, a_key));
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
				std::ignore = QueueNativeVariant(a_key);
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
				if (compilation->GetState() == ShaderVariantCompilationState::kFailed) {
					if (!variant->failureObserved.exchange(
							true, std::memory_order_relaxed)) {
						runtime.passthroughCompileFail.fetch_add(
							1, std::memory_order_relaxed);
					}
				} else if (!variant->pendingObserved.exchange(
							   true, std::memory_order_relaxed)) {
					runtime.passthroughNotReady.fetch_add(
						1, std::memory_order_relaxed);
				}
				return nullptr;
			}

			variant->samplerMask.store(compilation->GetSamplerMask(), std::memory_order_relaxed);
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
				std::is_same_v<TWrapper, RE::BSGraphics::VertexShader> || std::is_same_v<
																			  TWrapper,
																			  RE::BSGraphics::ComputeShader>) {
				trailingBytecodeSize = a_nativeWrapper->byteCodeSize;
			}
			if (trailingBytecodeSize > std::numeric_limits<std::size_t>::max() - sizeof(TWrapper)) {
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
						.shader = std::move(shaderLifetime) });
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
			VerifyFrameBindings(a_context, a_stage, a_target.id);
			if (a_stage == ShaderStage::kPixel)
				VerifyFrameBindings(a_context, ShaderStage::kVertex, a_target.id);
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
				(target->contributedStages & ShaderStageBit(ShaderStage::kCompute)) != 0;
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
				if (!render::IsSharedDataReady()) {
					g_computePhaseRejections.fetch_add(
						1, std::memory_order_relaxed);
					a_context->Dispatch(
						a_threadGroupCountX,
						a_threadGroupCountY,
						a_threadGroupCountZ);
					return;
				}
				const ActiveVariantScope variantScope(variant.get());
				// FO4 rebuilds the native low-slot inputs for each tiled dispatch.
				ScopedShaderInjectionBindings contributionBindings(ShaderStage::kCompute);
				if (!target->binds.empty())
					contributionBindings.Capture(a_context, ShaderResourceType::kShaderResource, 8);
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
				static_cast<std::int64_t>(a_target) - static_cast<std::int64_t>(
														  a_source + sizeof(REL::ASM::JMP5));
			return displacement >= std::numeric_limits<std::int32_t>::min() && displacement <= std::numeric_limits<std::int32_t>::max();
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
				return installedTail == a_validatedTail && g_computeDispatchContext.load(std::memory_order_acquire) == a_immediateContext && std::memcmp(reinterpret_cast<const void*>(installedTail), g_computeDispatchBridgePatch.data(), g_computeDispatchBridgePatch.size()) == 0;
			}

			if (std::memcmp(
					reinterpret_cast<const void*>(a_validatedTail),
					kRunComputeShaderDispatchTail.data(),
					kRunComputeShaderDispatchTail.size()) != 0) {
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
											patch.size()) == 0;
				if (!patchOwned) {
					const bool restored = REL::WriteSafe(
											  a_validatedTail,
											  kRunComputeShaderDispatchTail.data(),
											  kRunComputeShaderDispatchTail.size()) &&
					                      std::memcmp(
											  reinterpret_cast<const void*>(
												  a_validatedTail),
											  kRunComputeShaderDispatchTail.data(),
											  kRunComputeShaderDispatchTail.size()) == 0;
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
			(a_registration.stages & ShaderStageBit(ShaderStage::kPixel)) != 0;
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
			if (a_candidate.stages == 0 || (a_candidate.stages & ~kValidShaderStages) != 0) {
				L->error(
					"Replacement registration '{}' for '{}' rejected: invalid shader stage mask 0x{:X}.",
					a_candidate.contributor,
					metadata.name,
					a_candidate.stages);
				return false;
			}
			constexpr auto graphicsStages = ShaderStageBit(ShaderStage::kVertex) | ShaderStageBit(ShaderStage::kPixel);
			if (a_candidate.requiresGraphicsPair && a_candidate.stages != graphicsStages) {
				L->error("Replacement registration '{}' rejected: paired graphics stages require a vertex/pixel contribution.",
					a_candidate.contributor);
				return false;
			}
			return true;
		};

		{
			std::scoped_lock lock(service.mutex);
			if (!admissible(a_registration))
				return false;
		}

		// Active substrate uses b4-b7 and canonical scene depth at t17.
		render::EnsureSharedDataUpdateInstalled();
		// Consumer sampling and forced bindings share the engine draw anchor.
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
				"Shader ownership rejected: unknown target.");
			return false;
		}

		auto& service = GetService();
		service.baselineOwnership[ToIndex(a_target)].store(a_enabled, std::memory_order_relaxed);
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
		std::scoped_lock variantLock(service.nativeVariantMutex);
		service.shaderSourceAvailability.clear();
		return true;
	}

	bool SetShaderInjectionEnabled(bool a_enabled)
	{
		auto& service = GetService();
		service.enabled.store(a_enabled, std::memory_order_relaxed);
		return true;
	}

	bool PrepareShaderInjectionVariants(std::span<const ShaderFamilyDescriptor> a_variants,
		std::string& a_error)
	{
		const auto plan = GetService().published.load(std::memory_order_acquire);
		if (!plan) {
			a_error = "Shader variants requested before injection publication";
			return false;
		}
		std::vector<std::shared_ptr<ShaderVariantCompilationHandle>> handles;
		for (const auto& descriptor : a_variants) {
			const auto* target = FindPublishedTarget(*plan, descriptor.target);
			const auto variant = target ?
			                         FindOrPrepareNativeVariant(*plan, *target, MakeNativeVariantKey(descriptor)) :
			                         nullptr;
			if (!variant) {
				a_error = "Required shader variant has no published route";
				return false;
			}
			std::scoped_lock lock(variant->mutex);
			if (!variant->compilation) {
				a_error = "Required shader variant has no compilation";
				return false;
			}
			handles.push_back(variant->compilation);
		}
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
		for (const auto& handle : handles) {
			while (handle->GetState() == ShaderVariantCompilationState::kPending &&
				   std::chrono::steady_clock::now() < deadline)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			if (handle->GetState() != ShaderVariantCompilationState::kReady) {
				a_error = handle->GetState() == ShaderVariantCompilationState::kFailed ?
				              handle->GetError() :
				              "Required shader compilation timed out";
				return false;
			}
		}
		a_error.clear();
		return true;
	}

	void ApplyShaderOwnershipConfig(const feature_config::ShaderOwnershipConfig& a_config)
	{
		SetShaderInjectionEnabled(a_config.enableShaders);
		for (const auto& target : GetShaderInjectionTargets())
			SetBaselineShaderOwnership(target.id, a_config.targets[target.id]);
	}

	bool ValidateShaderInjectionRoutes(
		std::string_view a_contributor,
		std::string& a_error)
	{
		auto& service = GetService();
		std::scoped_lock lock(service.mutex);
		if (service.lifecycle != Lifecycle::kPublished) {
			a_error = std::string(a_contributor) + " routes were validated before injection publication";
			return false;
		}
		const auto plan =
			service.published.load(std::memory_order_acquire);
		if (!plan || !plan->device) {
			a_error = std::string(a_contributor) + " routes were not published because the D3D11 device is unavailable";
			return false;
		}

		std::vector<const ShaderReplacementRegistration*> matched;
		bool foundRegistration = false;
		for (const auto& registration : service.registrations) {
			if (registration.contributor != a_contributor)
				continue;
			foundRegistration = true;

			const auto* metadata =
				GetShaderInjectionTarget(registration.targetId);
			if (!metadata) {
				a_error = "contributor '" + std::string(a_contributor) + "' registered an unknown shader-injection target";
				return false;
			}
			if ((registration.stages & metadata->supportedStages) == 0) {
				a_error = "'" + std::string(metadata->name) + "' has no supported shader stage for contributor '" + std::string(a_contributor) + "'";
				return false;
			}

			const auto& runtime =
				service.runtime[ToIndex(registration.targetId)];
			const auto* published =
				FindPublishedTarget(*plan, registration.targetId);
			if (!published) {
				a_error = "'" + std::string(metadata->name) + "' did not publish the registered route for contributor '" + std::string(a_contributor) + "'";
				if (!runtime.publicationError.empty()) {
					a_error += ": ";
					a_error += runtime.publicationError;
				}
				return false;
			}

			const auto contribution = std::ranges::find_if(
				published->contributions,
				[&](const ShaderReplacementRegistration& a_candidate) {
					return a_candidate.contributor == a_contributor && a_candidate.targetId == registration.targetId && a_candidate.stages == registration.stages && a_candidate.feature == registration.feature && !std::ranges::contains(matched, std::addressof(a_candidate));
				});
			if (contribution == published->contributions.end()) {
				a_error = "'" + std::string(metadata->name) + "' lost a registered route for contributor '" + std::string(a_contributor) + "'";
				return false;
			}
			matched.push_back(std::addressof(*contribution));
		}
		if (!foundRegistration) {
			a_error = "no shader routes were registered for contributor '" + std::string(a_contributor) + "'";
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
					std::memory_order_acquire) == a_immediateContext;
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
			if (!function || !textBegin || tail < textBegin || tail > textEnd || textEnd - tail < kRunComputeShaderDispatchTail.size()) {
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
		if (!tail || !g_computeDispatchContext.load(
						 std::memory_order_acquire)) {
			return false;
		}
		return std::memcmp(
				   reinterpret_cast<const void*>(tail),
				   g_computeDispatchBridgePatch.data(),
				   g_computeDispatchBridgePatch.size()) == 0;
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
			static_cast<std::size_t>(ShaderInjectionTarget::kCount)>
			developerOverrides{};
		std::wstring developerSourceRoot;
		bool developerForceOffEnabled = false;

		{
			std::scoped_lock lock(service.mutex);
			if (service.lifecycle != Lifecycle::kCollecting)
				return;
			service.lifecycle = Lifecycle::kFrozen;
			developerForceOffEnabled = service.developerForceOffEnabled;
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
		auto frozenTargets = FreezeTargets(
			registrations,
			developerForceOffEnabled,
			developerOverrides);

		if (!a_device) {
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
				if (frozenTarget.metadata->id == ShaderInjectionTarget::kDfTiledLighting && !ComputeDispatchBridgeInstalled()) {
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
					.contributions = frozenTarget.contributions });
				++publishedTargets;
			}
		}
		service.published.store(plan, std::memory_order_release);
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
			"Shader injection freeze: targets requested={} published={}; native variants compile asynchronously from observed descriptors.",
			summary.requested,
			summary.published);
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
		service.shaderSourceAvailability.clear();
		service.unsupportedNativeVariantReported.fill(false);
		service.nativeShaderIdentities.clear();
	}

	void DispatchShaderInjections(
		ShaderInjectionTarget a_target,
		ID3D11DeviceContext* a_context,
		std::uint16_t a_samplerMask) noexcept
	{
		if (!a_context || !IsValidTarget(a_target))
			return;

		const auto plan = GetService().published.load(std::memory_order_acquire);
		const auto* target = plan ? FindPublishedTarget(*plan, a_target) : nullptr;
		if (!target)
			return;
		const auto previousMask = std::exchange(t_samplerMask, a_samplerMask);
		DispatchPublishedTarget(*target, ShaderStage::kPixel, a_context);
		t_samplerMask = previousMask;
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
			return metadata != service.nativeShaderMetadata.end() && metadata->second.forceEarlyDepthStencil;
		}

		void ObserveNativeShaderImpl(
			RE::BSShader* a_shader,
			bool a_hasPayload) noexcept
		{
			if (!a_shader || !a_hasPayload)
				return;
			try {
				const auto family =
					ResolveNativeShaderFamilyContext(a_shader);
				if (!family)
					return;
				const auto supportedStages =
					SupportedStages(family->target);
				if ((supportedStages & ShaderStageBit(ShaderStage::kVertex)) != 0) {
					for (auto* entry : native::VertexShaders(a_shader)) {
						if (!entry || !entry->shader)
							continue;
						std::ignore = QueueNativeVariant(
							MakeNativeVariantKey(
								*family,
								ShaderStage::kVertex,
								entry->id));
					}
				}
				if ((supportedStages & ShaderStageBit(ShaderStage::kPixel)) != 0) {
					for (auto* entry : native::PixelShaders(a_shader)) {
						if (!entry || !entry->shader)
							continue;
						std::ignore = QueueNativeVariant(
							MakeNativeVariantKey(
								*family,
								ShaderStage::kPixel,
								entry->id,
								NativePixelForcesEarlyDepthStencil(
									entry)));
					}
				}
				if ((supportedStages & ShaderStageBit(ShaderStage::kCompute)) != 0) {
					for (auto* entry : native::ComputeShaders(a_shader)) {
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
			if (!ShaderEnabled(a_family.target))
				return result;

			bool requiresGraphicsPair = false;
			try {
				const auto plan =
					GetService().published.load(std::memory_order_acquire);
				const auto* target =
					plan ? FindPublishedTarget(*plan, a_family.target) : nullptr;
				if (!target)
					return result;
				requiresGraphicsPair = std::ranges::any_of(target->contributions, [](const auto& contribution) {
					return contribution.requiresGraphicsPair;
				});

				auto& runtime =
					GetService().runtime[ToIndex(a_family.target)];
				if (a_nativeVertex && a_nativeVertex->id == a_vertexShaderId) {
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
					}
				}

				if (a_nativePixel && a_nativePixel->id == a_pixelShaderId) {
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
			// Host-added interpolators cannot pair a replacement stage with its native counterpart.
			if (requiresGraphicsPair && (result.vertex == a_nativeVertex || result.pixel == a_nativePixel)) {
				result = { a_nativeVertex, a_nativePixel };
			}
			GetService().runtime[ToIndex(a_family.target)].substitutions.fetch_add(
				(result.vertex != a_nativeVertex) + (result.pixel != a_nativePixel), std::memory_order_relaxed);
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
			GetService().runtime[ToIndex(owner->target)].computeBindCalls.fetch_add(1, std::memory_order_relaxed);
			if (!ShaderEnabled(owner->target))
				return a_nativeCompute;
			const auto* target =
				FindPublishedTarget(*plan, owner->target);
			if (!target)
				return a_nativeCompute;

			const bool hasFeatureBindings =
				(target->contributedStages & ShaderStageBit(ShaderStage::kCompute)) != 0;
			if (hasFeatureBindings && (owner->target != ShaderInjectionTarget::kDfTiledLighting || !render::IsDeferredLightsActive() || !ComputeDispatchBridgeInstalled())) {
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
		ObserveNativeShaderImpl(a_shader, a_hasPayload);
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
					a_hasPayload && a_target == ShaderInjectionTarget::kDfTiledLighting && a_nativeName == "DFTiledLighting" && (entry->id == 0 || entry->id == 3);
				RecordNativeComputeShader(
					MakeNativeVariantKey({ .target = a_target,
						.stage = ShaderStage::kCompute,
						.descriptor = entry->id,
						.nativeName = a_nativeName }),
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
		if (a_stage != ShaderStage::kPixel || !a_bytecode || a_bytecodeLength == 0 || !a_shader) {
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
			(reflection->GetRequiresFlags() & D3D_SHADER_REQUIRES_EARLY_DEPTH_STENCIL) != 0;
		auto& service = GetService();
		std::scoped_lock lock(service.nativeVariantMutex);
		service.nativeShaderMetadata.insert_or_assign(a_shader, metadata);
	}

#ifdef FO4CS_SHADER_INJECTION_TESTING
	void ObserveNativeComputeShaderForTesting(
		ShaderInjectionTarget a_target,
		std::uint32_t a_descriptor,
		std::string_view a_nativeName,
		ID3D11ComputeShader* a_shader) noexcept
	{
		try {
			RecordNativeComputeShader(
				MakeNativeVariantKey({ .target = a_target,
					.stage = ShaderStage::kCompute,
					.descriptor = a_descriptor,
					.nativeName = a_nativeName }),
				a_shader,
				false);
		} catch (...) {
		}
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
			DispatchShaderInjections(activeVariant->target, a_context, activeVariant->samplerMask.load(std::memory_order_relaxed));
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
			snapshot.enabled = ShaderEnabled(a_target);
			snapshot.name = metadata.name;
			snapshot.requested =
				runtime.requested.load(std::memory_order_relaxed);
			snapshot.published =
				plan && FindPublishedTarget(*plan, a_target);
			snapshot.developerOverride = runtime.developerOverride;
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
			snapshot.dispatches =
				runtime.dispatches.load(std::memory_order_relaxed);
			snapshot.computeBindCalls = runtime.computeBindCalls.load(std::memory_order_relaxed);
		}
		if ((SupportedStages(a_target) & ShaderStageBit(ShaderStage::kCompute)) != 0) {
			std::scoped_lock lock(service.nativeVariantMutex);
			snapshot.observedComputeShaders = static_cast<std::size_t>(std::ranges::count_if(
				service.nativeComputeOwners, [&](const auto& entry) { return entry.second.target == a_target; }));
		}
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
		}
		summary.computeBridge = GetComputeDispatchBridgeStatus();
		{
			std::scoped_lock lock(g_drawMetricsMutex);
			summary.draw = g_completedDrawMetrics;
		}
		return summary;
	}

}

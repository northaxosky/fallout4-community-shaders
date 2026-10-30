#pragma once

#include "Render/FrameBindings.h"
#include "Render/GameRuntime.h"
#include "Render/ShaderDefineProvider.h"
#include "Render/ShaderInjectionTargets.h"
#include "Render/SharedData.h"
#include "Utils/ShaderSamplerBindings.h"

#include <chrono>
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
struct ID3D11DeviceChild;
struct ID3D11DeviceContext;
struct ID3D11ComputeShader;
struct ID3D11PixelShader;
struct ID3D11ShaderResourceView;
struct ID3D11SamplerState;
struct ID3D11Buffer;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
struct ID3D11BlendState;
namespace RE
{
	class BSComputeShader;
	class BSShader;
	namespace BSGraphics
	{
		class ComputeShader;
		class PixelShader;
		class RendererShadowState;
		class VertexShader;
	}
}

namespace cs::feature_config
{
	struct ShaderOwnershipConfig;
}

namespace cs::engine
{
	struct ShaderFamilyDescriptor;
	// Startup barrier for features that mutate inputs consumed by required replacements.
	bool PrepareShaderInjectionVariants(std::span<const ShaderFamilyDescriptor> a_variants,
		std::string& a_error);
	enum class ShaderResourceType : std::uint8_t
	{
		kConstantBuffer,
		kSampler,
		kShaderResource,
		kUnorderedAccess,
		kBlendState,
		kRenderTarget
	};

	struct ShaderInjectionDrawMetrics
	{
		std::uint32_t frame = 0;
		std::uint64_t scopes = 0;
		std::uint64_t captures = 0;
		std::uint64_t restores = 0;
		std::uint64_t d3dBinds = 0;
		FrameBindingMetrics frameBindings;
	};

	// Render thread: publish the completed frame, including forward draws after composite.
	void BeginShaderInjectionFrame(std::uint32_t a_frame) noexcept;
	void RecordShaderInjectionD3DBinds(std::uint32_t a_count = 1) noexcept;
	// Bind to the active scope's stage (pixel without a scope); capture overwritten engine slots.
	void BindInjectionShaderResources(ID3D11DeviceContext*, UINT, UINT, ID3D11ShaderResourceView* const*) noexcept;
	void BindInjectionSamplers(ID3D11DeviceContext*, UINT, UINT, ID3D11SamplerState* const*) noexcept;
	void BindInjectionConstantBuffers(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*) noexcept;
	// The active draw scope restores the blend state when its draw returns.
	void CaptureShaderInjectionBlendState(ID3D11DeviceContext*) noexcept;
	// The active draw scope restores the render targets and depth view.
	void CaptureShaderInjectionRenderTargets(ID3D11DeviceContext*) noexcept;
	// Overrides a frame-bound PS slot for one draw; the draw scope restores it.
	void OverrideInjectionFrameShaderResource(ID3D11DeviceContext*, UINT, ID3D11ShaderResourceView*) noexcept;
	// Plugin GPU work or engine resets may change bindings the caches trust.
	void InvalidateShaderInjectionBindings() noexcept;

	// Brackets one engine SetDirtyStates flush and the injection after it.
	class ScopedEngineDrawFlush
	{
	public:
		// Construct before the flush; it picks the overrides the engine restores.
		ScopedEngineDrawFlush(bool a_outputStateOnly, bool a_compute, bool a_dispatches) noexcept;
		~ScopedEngineDrawFlush() noexcept;
		ScopedEngineDrawFlush(const ScopedEngineDrawFlush&) = delete;
		ScopedEngineDrawFlush& operator=(const ScopedEngineDrawFlush&) = delete;

	private:
		RE::BSGraphics::RendererShadowState* _state = nullptr;
		std::uint16_t _released = 0;
		bool _active = false;
	};

	class ScopedShaderInjectionBindings
	{
	public:
		explicit ScopedShaderInjectionBindings(ShaderStage a_stage = ShaderStage::kPixel) noexcept;
		~ScopedShaderInjectionBindings() noexcept;
		ScopedShaderInjectionBindings(const ScopedShaderInjectionBindings&) = delete;
		ScopedShaderInjectionBindings& operator=(const ScopedShaderInjectionBindings&) = delete;
		ShaderStage GetStage() const noexcept { return _stage; }
		// a_frameOwned captures a per-frame host slot above the engine range.
		void Capture(ID3D11DeviceContext* a_context, ShaderResourceType a_type, std::uint32_t a_slot, bool a_frameOwned = false) noexcept;
		void BindSampler(ID3D11DeviceContext* a_context, std::uint32_t a_slot, ID3D11SamplerState* a_sampler) noexcept;

	private:
		struct Resource
		{
			std::uint32_t slot;
			ID3D11ShaderResourceView* value;
		};
		struct Sampler
		{
			std::uint32_t slot;
			ID3D11SamplerState* value;
			ID3D11SamplerState* current;
		};
		struct Buffer
		{
			std::uint32_t slot;
			ID3D11Buffer* value;
		};
		ShaderStage _stage;
		ScopedShaderInjectionBindings* _previous;
		ID3D11DeviceContext* _context = nullptr;
		std::array<Resource, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> _resources;
		std::array<Sampler, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> _samplers;
		std::array<Buffer, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> _buffers;
		std::uint32_t _resourceCount = 0;
		std::uint32_t _samplerCount = 0;
		std::uint32_t _bufferCount = 0;
		ID3D11BlendState* _blend = nullptr;
		float _blendFactor[4]{};
		std::uint32_t _sampleMask = 0;
		bool _blendCaptured = false;
		ID3D11RenderTargetView* _targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
		ID3D11DepthStencilView* _depth = nullptr;
		bool _targetsCaptured = false;
	};
	using ScopedPixelShaderInjectionBindings = ScopedShaderInjectionBindings;

	using ShaderInjectionDefines = std::map<std::string, std::string, std::less<>>;
	using ShaderInjectionBindCallback = std::function<void(ID3D11DeviceContext*)>;

	struct ShaderReplacementRegistration
	{
		ShaderInjectionTarget targetId = ShaderInjectionTarget::kCount;
		ShaderStageMask stages = ShaderStageBit(ShaderStage::kPixel);
		std::string contributor;
		const ShaderDefineProvider* feature = nullptr;
		ShaderInjectionBindCallback bind;
		// For binds the shader needs while off (declared RTs, depth reads).
		bool bindWhileDisabled = false;
		bool requiresGraphicsPair = false;
	};

	struct ShaderVariantCompilationDescriptor
	{
		std::wstring sourcePath;
		std::string entryPoint;
		std::string profile;
		ShaderInjectionDefines defines;
	};

	struct ShaderInjectionTargetSnapshot
	{
		ShaderInjectionTarget id = ShaderInjectionTarget::kCount;
		std::string name;
		bool enabled = false;
		bool published = false;
		std::size_t contributors = 0;
		std::size_t observedComputeShaders = 0;
		std::uint64_t computeBindCalls = 0;
		ShaderInjectionDefines defines;
		std::string publicationError;
		std::uint64_t matches = 0;
		std::uint64_t substitutions = 0;
		std::uint64_t passthroughCompileFail = 0;
		std::uint64_t passthroughNotReady = 0;
		std::uint64_t dispatches = 0;
	};

	struct ShaderInjectionOutcomeSnapshot
	{
		std::uint64_t matches = 0;
		std::uint64_t substitutions = 0;
	};

	struct ComputeDispatchBridgeStatus
	{
		bool installed = false;
		std::uint64_t bridgeCalls = 0;
		std::uint64_t matchingDispatches = 0;
		std::uint64_t contextRejections = 0;
		std::uint64_t phaseRejections = 0;
		std::uint64_t shaderRejections = 0;
	};

	struct ShaderInjectionSummary
	{
		std::size_t published = 0;
		std::uint64_t matches = 0;
		std::uint64_t substitutions = 0;
		std::uint64_t passthroughCompileFail = 0;
		std::uint64_t passthroughNotReady = 0;
		std::uint64_t dispatches = 0;
		ComputeDispatchBridgeStatus computeBridge;
		ShaderInjectionDrawMetrics draw;
	};

	template <class Defines>
	std::string DescribeShaderInjectionDefines(const Defines& a_defines)
	{
		std::string names;
		for (const auto& [name, value] : a_defines) {
			if (!names.empty())
				names += ",";
			names += name;
			names += "=";
			names += value;
		}
		return names.empty() ? std::string("none") : names;
	}

	std::optional<ShaderInjectionDefines> GatherShaderInjectionDefines(
		ShaderInjectionTarget a_target,
		ShaderStageMask a_stages,
		std::span<const ShaderReplacementRegistration> a_contributions,
		std::string* a_error = nullptr);

	std::optional<ShaderVariantCompilationDescriptor>
	BuildEffectiveShaderCompileRequest(
		const ShaderInjectionTargetMetadata& a_target,
		ShaderStage a_stage,
		const ShaderVariantCompilationDescriptor& a_family,
		std::span<const ShaderReplacementRegistration> a_contributions,
		GameRuntime a_runtime,
		std::string* a_error = nullptr);

	bool RegisterReplacement(ShaderReplacementRegistration a_registration);
	bool RegisterReplacementIfEnabled(
		bool a_enabled,
		ShaderReplacementRegistration a_registration);
	bool SetBaselineShaderOwnership(
		ShaderInjectionTarget a_target,
		bool a_enabled);
	bool SetShaderInjectionEnabled(bool a_enabled);
	bool SetShaderInjectionRuntime(GameRuntime a_runtime);
	void ApplyShaderOwnershipConfig(const feature_config::ShaderOwnershipConfig& a_config);
	bool ValidateShaderInjectionRoutes(
		std::string_view a_contributor,
		std::string& a_error);

	void FreezeAndCompileShaderInjections(ID3D11Device* a_device);
	// Queues every stock variant of contributed targets ahead of observed binds.
	std::size_t PrewarmShaderInjectionVariants();
	bool EnsureComputeDispatchBridgeInstalled(
		ID3D11DeviceContext* a_immediateContext) noexcept;
	[[nodiscard]] bool ComputeDispatchBridgeInstalled() noexcept;
	[[nodiscard]] ComputeDispatchBridgeStatus
	GetComputeDispatchBridgeStatus() noexcept;
	struct NativeGraphicsShaderBinding
	{
		RE::BSGraphics::VertexShader* vertex = nullptr;
		RE::BSGraphics::PixelShader* pixel = nullptr;
	};
	void DispatchShaderInjections(
		ShaderInjectionTarget a_target,
		ID3D11DeviceContext* a_context,
		util::ShaderSamplerUse a_samplers = {}) noexcept;
	void DispatchInjectionsForBoundPixelShader(
		ID3D11DeviceContext* a_context) noexcept;
	NativeGraphicsShaderBinding ResolveNativeGraphicsShaderBinding(
		RE::BSShader* a_shader,
		std::uint32_t a_vertexShaderId,
		std::uint32_t a_pixelShaderId,
		RE::BSGraphics::VertexShader* a_nativeVertex,
		RE::BSGraphics::PixelShader* a_nativePixel) noexcept;
	RE::BSGraphics::ComputeShader* ResolveNativeComputeShaderBinding(
		RE::BSGraphics::ComputeShader* a_nativeCompute) noexcept;
	void ObserveNativeShader(
		RE::BSShader* a_shader,
		bool a_streamIsGood) noexcept;
	void ObserveNativeComputeOwner(
		const RE::BSComputeShader* a_owner,
		ShaderInjectionTarget a_target,
		std::string_view a_nativeName,
		bool a_streamIsGood) noexcept;
	void InstallNativeShaderCreationHooks(ID3D11Device* a_device);

	class ScopedNativeShaderObservationBypass
	{
	public:
		ScopedNativeShaderObservationBypass() noexcept;
		~ScopedNativeShaderObservationBypass() noexcept;

		ScopedNativeShaderObservationBypass(const ScopedNativeShaderObservationBypass&) = delete;
		ScopedNativeShaderObservationBypass(ScopedNativeShaderObservationBypass&&) = delete;
		ScopedNativeShaderObservationBypass& operator=(const ScopedNativeShaderObservationBypass&) = delete;
		ScopedNativeShaderObservationBypass& operator=(ScopedNativeShaderObservationBypass&&) = delete;
	};

	void InvalidateNativeShaderVariantCompilations() noexcept;
	// Drops queued compiles and joins the workers; later requests fail fast.
	void StopNativeShaderVariantCompilations() noexcept;
	// Define changes retire target lookups and native identities, retaining immutable compiled variants.
	void InvalidateNativeShaderVariantCompilations(std::span<const ShaderInjectionTarget> a_targets) noexcept;

	const ShaderInjectionDefines* GetActiveShaderInjectionVariantDefines(
		ShaderInjectionTarget a_target) noexcept;
	bool ActiveShaderInjectionVariantHasDefine(
		ShaderInjectionTarget a_target,
		std::string_view a_define) noexcept;
	bool ActiveShaderInjectionVariantHasAnyDefine(
		ShaderInjectionTarget a_target,
		std::span<const std::string_view> a_defines) noexcept;
	ShaderInjectionOutcomeSnapshot GetShaderInjectionOutcomeSnapshot(
		ShaderInjectionTarget a_target) noexcept;
	ShaderInjectionTargetSnapshot GetShaderInjectionTargetSnapshot(ShaderInjectionTarget a_target);
	ShaderInjectionSummary GetShaderInjectionSummary() noexcept;
}

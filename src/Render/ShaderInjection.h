#pragma once

#include "Render/FrameBindings.h"
#include "Render/GameRuntime.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/ShaderDefineProvider.h"
#include "Render/ShaderInjectionTargets.h"
#include "Render/SharedData.h"

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
	class BSShader;
	namespace BSGraphics
	{
		class ComputeShader;
		class PixelShader;
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
		kRenderTarget
	};

	struct ShaderInjectionDrawMetrics
	{
		std::uint32_t frame = 0;
		std::uint64_t scopes = 0;
		std::uint64_t scopeNanoseconds = 0;
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
	void CaptureShaderInjectionOutputs(ID3D11DeviceContext*) noexcept;

	class ScopedShaderInjectionBindings
	{
	public:
		explicit ScopedShaderInjectionBindings(ShaderStage a_stage = ShaderStage::kPixel) noexcept;
		~ScopedShaderInjectionBindings() noexcept;
		ScopedShaderInjectionBindings(const ScopedShaderInjectionBindings&) = delete;
		ScopedShaderInjectionBindings& operator=(const ScopedShaderInjectionBindings&) = delete;
		ShaderStage GetStage() const noexcept { return _stage; }
		void Capture(ID3D11DeviceContext* a_context, ShaderResourceType a_type, std::uint32_t a_slot) noexcept;
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
		ID3D11RenderTargetView* _targets[8]{};
		ID3D11DepthStencilView* _depth = nullptr;
		ID3D11BlendState* _blend = nullptr;
		float _blendFactor[4]{};
		std::uint32_t _sampleMask = 0;
		bool _outputCaptured = false;
		std::chrono::steady_clock::time_point _started;
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
		bool requiresGraphicsPair = false;
	};

	struct ShaderVariantCompilationDescriptor
	{
		std::wstring sourcePath;
		std::string entryPoint;
		std::string profile;
		ShaderInjectionDefines defines;
	};

	enum class DeveloperShaderOverride : std::uint8_t
	{
		kAuto,
		kForceOn,
		kForceOff
	};

	struct ShaderInjectionTargetSnapshot
	{
		ShaderInjectionTarget id = ShaderInjectionTarget::kCount;
		std::string name;
		bool requested = false;
		bool enabled = false;
		bool published = false;
		DeveloperShaderOverride developerOverride = DeveloperShaderOverride::kAuto;
		std::size_t contributors = 0;
		std::size_t observedComputeShaders = 0;
		std::uint64_t computeBindCalls = 0;
		ShaderInjectionDefines defines;
		std::string publicationError;
		std::uint64_t matches = 0;
		std::uint64_t substitutions = 0;
		std::uint64_t passthroughCompileFail = 0;
		std::uint64_t passthroughNotReady = 0;
		std::uint64_t passthroughDisabled = 0;
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
		std::size_t requested = 0;
		std::size_t published = 0;
		std::uint64_t matches = 0;
		std::uint64_t substitutions = 0;
		std::uint64_t passthroughCompileFail = 0;
		std::uint64_t passthroughNotReady = 0;
		std::uint64_t passthroughDisabled = 0;
		std::uint64_t dispatches = 0;
		ComputeDispatchBridgeStatus computeBridge;
		ShaderInjectionDrawMetrics draw;
	};

	std::string DescribeShaderInjectionDefines(
		const ShaderInjectionDefines& a_defines);

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
	bool SetDeveloperShaderForceOffEnabled(bool a_enabled);
	bool SetDeveloperShaderOverride(ShaderInjectionTarget a_target, DeveloperShaderOverride a_override);
	bool SetDeveloperShaderSourceRoot(std::wstring a_sourceRoot);
	bool SetShaderInjectionEnabled(bool a_enabled);
	bool SetShaderInjectionRuntime(GameRuntime a_runtime);
	void ApplyShaderOwnershipConfig(const feature_config::ShaderOwnershipConfig& a_config);
	bool ValidateShaderInjectionRoutes(
		std::string_view a_contributor,
		std::string& a_error);

	void FreezeAndCompileShaderInjections(ID3D11Device* a_device);
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
#ifdef FO4CS_SHADER_INJECTION_TESTING
	bool InstallComputeDispatchBridgeForTesting(
		ID3D11DeviceContext* a_context,
		std::uintptr_t a_validatedTail) noexcept;
	void ObserveNativeComputeShaderForTesting(
		ShaderInjectionTarget a_target,
		std::uint32_t a_descriptor,
		std::string_view a_nativeName,
		ID3D11ComputeShader* a_shader) noexcept;
#endif
	void DispatchShaderInjections(
		ShaderInjectionTarget a_target,
		ID3D11DeviceContext* a_context,
		std::uint16_t a_samplerMask = UINT16_MAX) noexcept;
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
		bool a_hasPayload) noexcept;
	void ObserveNativeComputeOwner(
		const void* a_owner,
		ShaderInjectionTarget a_target,
		std::string_view a_nativeName,
		bool a_hasPayload) noexcept;
	void ObserveNativeShaderBytecode(
		ShaderStage a_stage,
		const void* a_bytecode,
		std::size_t a_bytecodeLength,
		ID3D11DeviceChild* a_shader) noexcept;
	void InvalidateNativeShaderVariantCompilations() noexcept;
	// Define changes retire target lookups and native identities, retaining immutable compiled variants.
	void InvalidateNativeShaderVariantCompilations(std::span<const ShaderInjectionTarget> a_targets) noexcept;

	const ShaderInjectionDefines* GetActiveShaderInjectionVariantDefines(
		ShaderInjectionTarget a_target) noexcept;
	bool ActiveShaderInjectionVariantHasDefine(
		ShaderInjectionTarget a_target,
		std::string_view a_define) noexcept;
	ShaderInjectionOutcomeSnapshot GetShaderInjectionOutcomeSnapshot(
		ShaderInjectionTarget a_target) noexcept;
	ShaderInjectionTargetSnapshot GetShaderInjectionTargetSnapshot(ShaderInjectionTarget a_target);
	ShaderInjectionSummary GetShaderInjectionSummary() noexcept;
}

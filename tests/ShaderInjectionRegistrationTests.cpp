#include "Log.h"
#include "Render/PixelShaderResourceSnapshot.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "Render/ShaderVariantCompilation.h"
#include "Render/SharedData.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <winrt/base.h>

namespace
{
	std::uint32_t g_preDrawInstallRequests = 0;
	bool g_preDrawInstallFails = false;
	std::uint32_t g_sharedDataInstallRequests = 0;
	std::uint32_t g_bsdfCompositeBindDispatches = 0;

	class TestCompilationHandle final :
		public cs::engine::ShaderVariantCompilationHandle
	{
	public:
		TestCompilationHandle(
			cs::engine::ShaderStage a_stage,
			winrt::com_ptr<ID3D11DeviceChild> a_shader) :
			_stage(a_stage),
			_shader(std::move(a_shader))
		{}

		cs::engine::ShaderVariantCompilationState
			GetState() const noexcept override
		{
			return cs::engine::ShaderVariantCompilationState::kReady;
		}

		winrt::com_ptr<ID3D11DeviceChild>
			AcquireOrRequest() noexcept override
		{
			return _shader;
		}

		ID3D11DeviceChild* PeekShader() const noexcept override
		{
			return _shader.get();
		}

		cs::engine::ShaderStage GetStage() const noexcept override
		{
			return _stage;
		}

	private:
		cs::engine::ShaderStage _stage;
		winrt::com_ptr<ID3D11DeviceChild> _shader;
	};

	class TestCompilationPolicy final :
		public cs::engine::ShaderVariantCompilationPolicy
	{
	public:
		explicit TestCompilationPolicy(bool a_alternatePixel = false) :
			_alternatePixel(a_alternatePixel)
		{}

		cs::engine::ShaderVariantCompilationResult Prepare(
			cs::engine::ShaderVariantCompilationRequest a_request) override
		{
			using namespace cs::engine;

			ShaderVariantCompilationResult result;
			if (!a_request.device) {
				result.error = "no D3D11 device";
				return result;
			}

			auto& cached = _shaders[
				{ a_request.sourcePath, a_request.stage }];
			if (!cached.handle) {
				constexpr std::string_view pixelSource =
					"float4 main() : SV_Target { return 1.0; }";
				constexpr std::string_view alternatePixelSource =
					"float4 main() : SV_Target { return 0.25; }";
				constexpr std::string_view vertexSource =
					"float4 main(uint id : SV_VertexID) : SV_Position { "
					"return float4(id == 2 ? 3.0 : -1.0, "
					"id == 1 ? 3.0 : -1.0, 0.0, 1.0); }";
				const auto source = a_request.stage == ShaderStage::kPixel ?
					(_alternatePixel ? alternatePixelSource : pixelSource) :
					vertexSource;
				const auto profile = a_request.stage == ShaderStage::kPixel ?
					"ps_5_0" :
					"vs_5_0";

				winrt::com_ptr<ID3DBlob> blob;
				winrt::com_ptr<ID3DBlob> errors;
				const HRESULT compileResult = D3DCompile(
					source.data(),
					source.size(),
					nullptr,
					nullptr,
					nullptr,
					"main",
					profile,
					0,
					0,
					blob.put(),
					errors.put());
				if (FAILED(compileResult) || !blob) {
					result.error = errors ?
						std::string(
							static_cast<const char*>(errors->GetBufferPointer()),
							errors->GetBufferSize()) :
						"test shader compilation failed";
					return result;
				}

				winrt::com_ptr<ID3D11DeviceChild> shader;
				HRESULT createResult = E_FAIL;
				if (a_request.stage == ShaderStage::kPixel) {
					winrt::com_ptr<ID3D11PixelShader> pixelShader;
					createResult = a_request.device->CreatePixelShader(
						blob->GetBufferPointer(),
						blob->GetBufferSize(),
						nullptr,
						pixelShader.put());
					if (pixelShader)
						shader.attach(pixelShader.detach());
				} else {
					winrt::com_ptr<ID3D11VertexShader> vertexShader;
					createResult = a_request.device->CreateVertexShader(
						blob->GetBufferPointer(),
						blob->GetBufferSize(),
						nullptr,
						vertexShader.put());
					if (vertexShader)
						shader.attach(vertexShader.detach());
				}
				if (FAILED(createResult) || !shader) {
					result.error = "test shader creation failed";
					return result;
				}

				cached.bytecodeSize =
					static_cast<std::size_t>(blob->GetBufferSize());
				cached.handle = std::make_shared<TestCompilationHandle>(
					a_request.stage,
					std::move(shader));
			}

			result.state = ShaderVariantCompilationState::kReady;
			result.handle = cached.handle;
			result.bytecodeSize = cached.bytecodeSize;
			result.compiledSha1 = std::string(
				40,
				a_request.stage == ShaderStage::kPixel ? '1' : '2');
			return result;
		}

	private:
		struct CachedShader
		{
			std::shared_ptr<TestCompilationHandle> handle;
			std::size_t bytecodeSize = 0;
		};

		std::map<
			std::pair<std::filesystem::path, cs::engine::ShaderStage>,
			CachedShader> _shaders;
		bool _alternatePixel = false;
	};
}

namespace cs::log
{
	spdlog::logger* Get(const char*)
	{
		return spdlog::default_logger_raw();
	}
}

namespace cs::render
{
	void EnsureSharedDataUpdateInstalled()
	{
		++g_sharedDataInstallRequests;
	}

	bool IsSharedDataReady() noexcept
	{
		return true;
	}

	void BindSharedData(ID3D11DeviceContext*) noexcept
	{}
}

namespace cs::engine
{
	std::shared_ptr<ShaderVariantCompilationPolicy>
		CreateCachingShaderVariantCompilationPolicy()
	{
		return std::make_shared<TestCompilationPolicy>();
	}

	bool RegisterPixelShaderSwapResolver(ShaderSwapResolver)
	{
		return true;
	}

	bool RegisterPixelShaderSwapResolver(
		PixelShaderSwapResolverRegistration)
	{
		return true;
	}

	bool PixelShaderSwapBrokerHooksInstalled() noexcept
	{
		return true;
	}

	bool EnsurePreSunLightDrawInstalled()
	{
		++g_preDrawInstallRequests;
		return !g_preDrawInstallFails;
	}
}

namespace
{
	using namespace cs::engine;

	ShaderReplacementVariantRegistration MakeRegistration(
		std::string a_name,
		std::uint32_t a_key,
		std::string a_sha1)
	{
		ShaderReplacementVariantRegistration registration;
		registration.targetId =
			ShaderInjectionTarget::kDeferredComposite;
		registration.name = std::move(a_name);
		registration.variantKeys.push_back({
			"RegistrationTestShader",
			ShaderStage::kPixel,
			ShaderVariantId{ a_key }
		});
		registration.expectedStockSha1 = std::move(a_sha1);
		registration.compilation.sourcePath = L"registration-test.hlsl";
		registration.compilation.entryPoint = "main";
		registration.compilation.profile = "ps_5_0";
		return registration;
	}

	bool Check(
		bool a_condition,
		std::string_view a_failure)
	{
		if (a_condition)
			return true;
		std::cerr << "FAIL: " << a_failure << '\n';
		return false;
	}

	bool TestStageScopedContributions()
	{
		const auto* target = GetShaderInjectionTarget(
			ShaderInjectionTarget::kBsdfComposite);
		if (!Check(target != nullptr, "stage-scope target metadata is missing"))
			return false;

		std::vector<ShaderReplacementRegistration> contributions;
		contributions.push_back({
			.targetId = target->id,
			.contributor = "pixel-default",
			.defines = { { "PIXEL_DEFAULT", "1" } }
		});
		contributions.push_back({
			.targetId = target->id,
			.contributor = "pixel-second",
			.defines = { { "PIXEL_SECOND", "1" } }
		});
		contributions.push_back({
			.targetId = target->id,
			.stages = ShaderStageBit(ShaderStage::kVertex),
			.contributor = "vertex",
			.defines = { { "VERTEX_ONLY", "1" } }
		});
		contributions.push_back({
			.targetId = target->id,
			.stages = ShaderStageBit(ShaderStage::kVertex)
				| ShaderStageBit(ShaderStage::kPixel),
			.contributor = "both",
			.defines = { { "BOTH_STAGES", "1" } }
		});

		ShaderReplacementVariantRegistration pixelVariant;
		pixelVariant.targetId = target->id;
		pixelVariant.stage = ShaderStage::kPixel;
		pixelVariant.compilation.sourcePath = L"stage-scope.hlsl";
		pixelVariant.compilation.entryPoint = "main";
		pixelVariant.compilation.profile = "ps_5_0";
		ShaderReplacementVariantRegistration vertexVariant = pixelVariant;
		vertexVariant.stage = ShaderStage::kVertex;
		vertexVariant.compilation.profile = "vs_5_0";

		const auto pixelRequest =
			BuildEffectiveShaderCompileRequest(
				*target,
				pixelVariant,
				contributions);
		const auto vertexRequest =
			BuildEffectiveShaderCompileRequest(
				*target,
				vertexVariant,
				contributions);
		bool ok = Check(
			pixelRequest.has_value(),
			"pixel effective compile request failed");
		ok &= Check(
			vertexRequest.has_value(),
			"vertex effective compile request failed");
		if (!pixelRequest || !vertexRequest)
			return false;

		ok &= Check(
			!vertexRequest->defines.contains("PIXEL_DEFAULT"),
			"pixel-scoped define leaked into vertex request");
		ok &= Check(
			!vertexRequest->defines.contains("PIXEL_SECOND"),
			"second pixel-scoped define leaked into vertex request");
		ok &= Check(
			vertexRequest->defines.contains("VERTEX_ONLY"),
			"vertex-scoped define is missing from vertex request");
		ok &= Check(
			!pixelRequest->defines.contains("VERTEX_ONLY"),
			"vertex-scoped define leaked into pixel request");
		ok &= Check(
			vertexRequest->defines.contains("BOTH_STAGES")
				&& pixelRequest->defines.contains("BOTH_STAGES"),
			"both-stage define is missing from an effective request");
		ok &= Check(
			pixelRequest->defines.contains("PIXEL_DEFAULT")
				&& pixelRequest->defines.contains("PIXEL_SECOND"),
			"pixel-stage contributors did not union");
		return ok;
	}

	ShaderReplacementVariantRegistration MakeStageVariant(
		ShaderInjectionTarget a_target,
		ShaderStage a_stage)
	{
		ShaderReplacementVariantRegistration variant;
		variant.targetId = a_target;
		variant.stage = a_stage;
		variant.compilation.sourcePath = L"substrate-define.hlsl";
		variant.compilation.entryPoint = "main";
		variant.compilation.profile =
			a_stage == ShaderStage::kVertex ? "vs_5_0" : "ps_5_0";
		return variant;
	}

	std::optional<std::string> SubstrateDefine(
		const ShaderInjectionTargetMetadata& a_target,
		const ShaderReplacementVariantRegistration& a_variant,
		std::span<const ShaderReplacementRegistration> a_contributions)
	{
		const auto request = BuildEffectiveShaderCompileRequest(
			a_target,
			a_variant,
			a_contributions);
		if (!request)
			return std::nullopt;
		const auto define = request->defines.find(
			shader_injection_defines::kSubstrate);
		if (define == request->defines.end())
			return std::nullopt;
		return define->second;
	}

	bool TestAutomaticSubstrateDefine()
	{
		const auto* target = GetShaderInjectionTarget(
			ShaderInjectionTarget::kBsdfComposite);
		if (!Check(
				target != nullptr,
				"substrate-define target metadata is missing")) {
			return false;
		}

		const auto pixelVariant =
			MakeStageVariant(target->id, ShaderStage::kPixel);
		const auto vertexVariant =
			MakeStageVariant(target->id, ShaderStage::kVertex);

		bool ok = Check(
			!SubstrateDefine(*target, pixelVariant, {}).has_value(),
			"substrate define appeared without a feature contribution");

		const std::array vertexOnly{
			ShaderReplacementRegistration{
				.targetId = target->id,
				.stages = ShaderStageBit(ShaderStage::kVertex),
				.contributor = "vertex-only",
				.defines = { { "VERTEX_ONLY", "1" } }
			}
		};
		ok &= Check(
			!SubstrateDefine(*target, pixelVariant, vertexOnly).has_value(),
			"substrate define appeared for a non-matching stage");
		ok &= Check(
			SubstrateDefine(*target, vertexVariant, vertexOnly) == "1",
			"substrate define is missing from the contributed vertex stage");

		std::array bindOnly{
			ShaderReplacementRegistration{
				.targetId = target->id,
				.contributor = "bind-only"
			}
		};
		bindOnly.front().bind = [](ID3D11DeviceContext*) {};
		ok &= Check(
			SubstrateDefine(*target, pixelVariant, bindOnly) == "1",
			"bind-only contribution did not request the substrate");

		const std::array pixelContribution{
			ShaderReplacementRegistration{
				.targetId = target->id,
				.contributor = "pixel-contribution",
				.defines = { { "PIXEL_CONTRIBUTION", "1" } }
			}
		};
		ok &= Check(
			SubstrateDefine(*target, pixelVariant, pixelContribution) == "1",
			"normal contribution did not request the substrate");
		ok &= Check(
			!SubstrateDefine(*target, vertexVariant, pixelContribution)
				.has_value(),
			"pixel contribution leaked the substrate into the vertex stage");

		const std::array otherTarget{
			ShaderReplacementRegistration{
				.targetId = ShaderInjectionTarget::kBsdfLight,
				.contributor = "other-target",
				.defines = { { "OTHER_TARGET", "1" } }
			}
		};
		ok &= Check(
			!SubstrateDefine(*target, pixelVariant, otherTarget).has_value(),
			"substrate define leaked across targets");

		const std::array conflicting{
			ShaderReplacementRegistration{
				.targetId = target->id,
				.contributor = "substrate-conflict",
				.defines = {
					{ shader_injection_defines::kSubstrate, "0" }
				}
			}
		};
		std::string conflictError;
		ok &= Check(
			!BuildEffectiveShaderCompileRequest(
				 *target,
				 pixelVariant,
				 conflicting,
				 &conflictError)
				 .has_value()
				&& conflictError.find(
					   shader_injection_defines::kSubstrate)
					!= std::string::npos,
			"conflicting substrate define was not diagnosed");
		return ok;
	}

	struct EffectiveDefinePartition
	{
		std::vector<std::size_t> shape;
		std::size_t variants = 0;
		bool anySentinel = false;
		bool allSentinel = true;
	};

	std::optional<EffectiveDefinePartition> BuildVertexDefinePartition(
		const ShaderInjectionTargetMetadata& a_target,
		std::span<const ShaderReplacementVariantRegistration> a_variants,
		std::span<const ShaderReplacementRegistration> a_contributions,
		std::string_view a_sentinel)
	{
		std::vector<std::pair<ShaderInjectionDefines, std::size_t>> groups;
		EffectiveDefinePartition partition;
		for (const auto& variant : a_variants) {
			if (variant.targetId != a_target.id
				|| variant.stage != ShaderStage::kVertex) {
				continue;
			}
			const auto request = BuildEffectiveShaderCompileRequest(
				a_target,
				variant,
				a_contributions);
			if (!request)
				return std::nullopt;

			++partition.variants;
			const bool hasSentinel =
				request->defines.contains(a_sentinel);
			partition.anySentinel =
				partition.anySentinel || hasSentinel;
			partition.allSentinel =
				partition.allSentinel && hasSentinel;
			auto group = std::ranges::find_if(
				groups,
				[&request](const auto& a_group) {
					return a_group.first == request->defines;
				});
			if (group == groups.end()) {
				groups.emplace_back(request->defines, 1);
			} else {
				++group->second;
			}
		}
		partition.shape.reserve(groups.size());
		for (const auto& group : groups)
			partition.shape.push_back(group.second);
		std::ranges::sort(partition.shape);
		return partition;
	}

	bool TestVertexCompileClassPartition()
	{
		const auto* target = GetShaderInjectionTarget(
			ShaderInjectionTarget::kBsWater);
		if (!Check(
				target != nullptr,
				"BSWater partition target metadata is missing")) {
			return false;
		}

		const auto variants = GetDefaultShaderReplacementVariants();
		const auto baseline = BuildVertexDefinePartition(
			*target,
			variants,
			{},
			"PIXEL_PARTITION_SENTINEL");
		const std::array pixelContribution{
			ShaderReplacementRegistration{
				.targetId = target->id,
				.contributor = "pixel-partition",
				.defines = {
					{ "PIXEL_PARTITION_SENTINEL", "1" },
					{ "VC", "1" }
				}
			}
		};
		const auto pixelScoped = BuildVertexDefinePartition(
			*target,
			variants,
			pixelContribution,
			"PIXEL_PARTITION_SENTINEL");
		auto vertexContribution = pixelContribution;
		vertexContribution.front().stages =
			ShaderStageBit(ShaderStage::kVertex);
		const auto vertexScoped = BuildVertexDefinePartition(
			*target,
			variants,
			vertexContribution,
			"PIXEL_PARTITION_SENTINEL");

		bool ok = Check(
			baseline.has_value()
				&& pixelScoped.has_value()
				&& vertexScoped.has_value(),
			"BSWater vertex partition could not be built");
		if (!baseline || !pixelScoped || !vertexScoped)
			return false;

		auto expectedVertexShape =
			std::vector<std::size_t>(14, 1);
		expectedVertexShape.push_back(2);
		ok &= Check(
			baseline->variants == 16
				&& baseline->shape
					== std::vector<std::size_t>(16, 1),
			"BSWater baseline vertex partition shape changed");
		ok &= Check(
			pixelScoped->shape == baseline->shape,
			"pixel-scoped contribution changed BSWater vertex compile-class partition");
		ok &= Check(
			!pixelScoped->anySentinel,
			"pixel partition sentinel leaked into a vertex request");
		ok &= Check(
			vertexScoped->shape == expectedVertexShape,
			"vertex-scoped contribution produced the wrong BSWater vertex compile-class partition");
		ok &= Check(
			vertexScoped->allSentinel,
			"vertex partition sentinel is missing from a vertex request");
		return ok;
	}

	int TestBaselineOwnershipWithoutContributors()
	{
		constexpr std::array ownableTargets{
			ShaderInjectionTarget::kDeferredPrepass,
			ShaderInjectionTarget::kBsSky,
			ShaderInjectionTarget::kBsWater,
			ShaderInjectionTarget::kBsLighting,
			ShaderInjectionTarget::kBsdfLight,
			ShaderInjectionTarget::kBsdfComposite
		};

		bool ok = true;
		for (const auto target : ownableTargets) {
			ok &= Check(
				SetBaselineShaderOwnership(target, true),
				"ownable baseline target was rejected");
		}
		ok &= Check(
			!SetBaselineShaderOwnership(
				ShaderInjectionTarget::kDeferredComposite,
				true),
			"deferred composite was accepted for baseline ownership");
		ok &= Check(
			!SetBaselineShaderOwnership(
				ShaderInjectionTarget::kVlsSliceScatter,
				true),
			"VLS slice scatter was accepted for baseline ownership");

		FreezeAndCompileShaderInjections(nullptr);
		for (const auto target : ownableTargets) {
			const auto snapshot =
				GetShaderInjectionTargetSnapshot(target);
			ok &= Check(
				snapshot.requested,
				"baseline ownership did not request target");
			ok &= Check(
				snapshot.contributors == 0,
				"baseline-only target gained a feature contributor");
			ok &= Check(
				snapshot.requestReasons
					== ShaderInjectionRequestReason::
						kBaselineOwnership,
				"baseline-only target has the wrong request reason");
			ok &= Check(
				!snapshot.compileAttempted,
				"null-device freeze attempted compilation");
		}

		const auto summary = GetShaderInjectionSummary();
		ok &= Check(
			summary.requested == ownableTargets.size(),
			"baseline request count mismatch");
		ok &= Check(
			summary.requestedByBaselineOwnership
				== ownableTargets.size(),
			"baseline request attribution count mismatch");
		ok &= Check(
			summary.requestedByFeatureContributor == 0,
			"baseline-only freeze reported feature requests");
		ok &= Check(
			summary.requestedByDeveloperForceOn == 0,
			"baseline-only freeze reported developer requests");
		if (!ok)
			return 1;
		std::cout
			<< "PASS: baseline shader ownership requests targets without contributors\n";
		return 0;
	}

	bool IsLowerHexSha1(std::string_view a_value)
	{
		return a_value.size() == 40
			&& std::ranges::all_of(a_value, [](char a_character) {
				return (a_character >= '0' && a_character <= '9')
					|| (a_character >= 'a' && a_character <= 'f');
			});
	}

	bool RequiresStockHash(ShaderInjectionTarget a_target)
	{
		switch (a_target) {
		case ShaderInjectionTarget::kDeferredPrepass:
		case ShaderInjectionTarget::kBsSky:
		case ShaderInjectionTarget::kBsWater:
		case ShaderInjectionTarget::kBsLighting:
		case ShaderInjectionTarget::kBsdfLight:
		case ShaderInjectionTarget::kBsdfComposite:
			return true;
		default:
			return false;
		}
	}

	bool CreateWarpDevice(
		winrt::com_ptr<ID3D11Device>& a_device,
		winrt::com_ptr<ID3D11DeviceContext>& a_context)
	{
		constexpr D3D_FEATURE_LEVEL featureLevels[]{
			D3D_FEATURE_LEVEL_11_0
		};
		const HRESULT result = D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_WARP,
			nullptr,
			0,
			featureLevels,
			static_cast<UINT>(std::size(featureLevels)),
			D3D11_SDK_VERSION,
			a_device.put(),
			nullptr,
			a_context.put());
		return Check(
			SUCCEEDED(result) && a_device && a_context,
			"could not create a D3D11 WARP device");
	}

	winrt::com_ptr<ID3D11ShaderResourceView> CreateTestSrv(
		ID3D11Device* a_device)
	{
		D3D11_TEXTURE2D_DESC textureDesc{};
		textureDesc.Width = 1;
		textureDesc.Height = 1;
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.Usage = D3D11_USAGE_DEFAULT;
		textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		winrt::com_ptr<ID3D11Texture2D> texture;
		if (FAILED(a_device->CreateTexture2D(
				&textureDesc,
				nullptr,
				texture.put()))) {
			return {};
		}

		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		if (FAILED(a_device->CreateShaderResourceView(
				texture.get(),
				nullptr,
				srv.put()))) {
			return {};
		}
		return srv;
	}

	ULONG ReferenceCount(IUnknown* a_object)
	{
		a_object->AddRef();
		return a_object->Release();
	}

	bool TestPixelShaderResourceSnapshot(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context)
	{
		constexpr std::uint32_t startSlot = 26;
		auto first = CreateTestSrv(a_device);
		auto second = CreateTestSrv(a_device);
		bool ok = Check(
			first && second && first.get() != second.get(),
			"could not create distinct snapshot SRVs");
		if (!ok)
			return false;

		ID3D11ShaderResourceView* initial[4]{
			first.get(),
			nullptr,
			second.get(),
			nullptr
		};
		a_context->PSSetShaderResources(startSlot, 4, initial);
		const ULONG firstBaseline = ReferenceCount(first.get());
		const ULONG secondBaseline = ReferenceCount(second.get());

		cs::render::PixelShaderResourceSnapshot<4> snapshot;
		ok &= Check(
			snapshot.Save(a_context, startSlot),
			"pixel SRV snapshot was not saved");
		ok &= Check(
			!snapshot.Save(a_context, startSlot),
			"overlapping pixel SRV snapshot replaced active state");
		ok &= Check(
			ReferenceCount(first.get()) == firstBaseline + 1
				&& ReferenceCount(second.get()) == secondBaseline + 1,
			"pixel SRV snapshot did not own PSGet references");

		ID3D11ShaderResourceView* cleared[4]{};
		a_context->PSSetShaderResources(startSlot, 4, cleared);
		const ULONG firstAfterClear = ReferenceCount(first.get());
		const ULONG secondAfterClear = ReferenceCount(second.get());
		ok &= Check(
			snapshot.Restore(a_context) && snapshot.IsSaved(),
			"nested pixel SRV restore consumed the outer snapshot");
		ID3D11ShaderResourceView* nested[4]{};
		a_context->PSGetShaderResources(startSlot, 4, nested);
		ok &= Check(
			std::ranges::all_of(
				nested,
				[](ID3D11ShaderResourceView* a_resource) {
					return a_resource == nullptr;
				}),
			"nested pixel SRV restore wrote the outer state early");
		ok &= Check(
			ReferenceCount(first.get()) == firstAfterClear
				&& ReferenceCount(second.get()) == secondAfterClear,
			"nested pixel SRV restore released the outer snapshot");
		ok &= Check(
			snapshot.Restore(a_context) && !snapshot.IsSaved(),
			"outer pixel SRV snapshot was not restored");

		ID3D11ShaderResourceView* restored[4]{};
		a_context->PSGetShaderResources(startSlot, 4, restored);
		ok &= Check(
			restored[0] == first.get()
				&& restored[1] == nullptr
				&& restored[2] == second.get()
				&& restored[3] == nullptr,
			"pixel SRV snapshot did not restore null and non-null slots");
		for (auto* resource : restored) {
			if (resource)
				resource->Release();
		}
		ok &= Check(
			ReferenceCount(first.get()) == firstBaseline
				&& ReferenceCount(second.get()) == secondBaseline,
			"pixel SRV snapshot leaked PSGet references");

		a_context->PSSetShaderResources(startSlot, 4, cleared);
		return ok;
	}

	// slot and define claims must be admitted or rejected at registration time
	bool TestClaimLedger()
	{
		constexpr std::uint32_t kNormalSlot = 25;
		const auto claim = [](std::uint32_t a_slot) {
			return ShaderSlotClaim{
				.stage = ShaderStage::kPixel,
				.resourceType = ShaderResourceType::kShaderResource,
				.slot = a_slot
			};
		};

		ShaderReplacementRegistration notReadyClaimant;
		notReadyClaimant.targetId = ShaderInjectionTarget::kBsdfComposite;
		notReadyClaimant.contributor = "ledger-not-ready-claimant";
		notReadyClaimant.defines = { { "LEDGER_TEST", "1" } };
		notReadyClaimant.isReady = [] { return false; };
		notReadyClaimant.slotClaims = { claim(kNormalSlot) };
		bool ok = Check(
			RegisterReplacement(std::move(notReadyClaimant)),
			"first t25 claimant was rejected");

		ShaderReplacementRegistration secondClaimant;
		secondClaimant.targetId = ShaderInjectionTarget::kBsdfComposite;
		secondClaimant.contributor = "ledger-second-claimant";
		secondClaimant.slotClaims = { claim(kNormalSlot) };
		ok &= Check(
			!RegisterReplacement(std::move(secondClaimant)),
			"a second t25 claim on kBsdfComposite was accepted");

		ShaderReplacementRegistration otherTargetClaimant;
		otherTargetClaimant.targetId = ShaderInjectionTarget::kBsdfLight;
		otherTargetClaimant.contributor = "ledger-other-target-claimant";
		otherTargetClaimant.slotClaims = { claim(kNormalSlot) };
		ok &= Check(
			RegisterReplacement(std::move(otherTargetClaimant)),
			"the same slot on another target was rejected");

		ShaderReplacementRegistration conflictingDefine;
		conflictingDefine.targetId = ShaderInjectionTarget::kBsdfComposite;
		conflictingDefine.contributor = "ledger-conflicting-define";
		conflictingDefine.defines = { { "LEDGER_TEST", "2" } };
		ok &= Check(
			!RegisterReplacement(std::move(conflictingDefine)),
			"a conflicting define value was accepted");

		ShaderReplacementRegistration agreeingDefine;
		agreeingDefine.targetId = ShaderInjectionTarget::kBsdfComposite;
		agreeingDefine.contributor = "ledger-agreeing-define";
		agreeingDefine.defines = { { "LEDGER_TEST", "1" } };
		ok &= Check(
			RegisterReplacement(std::move(agreeingDefine)),
			"an agreeing define value was rejected");

		for (const auto slot :
			{ cs::render::kSharedDataSlot, cs::render::kFeatureDataSlot }) {
			ShaderReplacementRegistration substrateClaimant;
			substrateClaimant.targetId = ShaderInjectionTarget::kBsdfComposite;
			substrateClaimant.contributor = "ledger-substrate-claimant";
			substrateClaimant.slotClaims = { {
				.stage = ShaderStage::kPixel,
				.resourceType = ShaderResourceType::kConstantBuffer,
				.slot = slot
			} };
			ok &= Check(
				!RegisterReplacement(std::move(substrateClaimant)),
				"a substrate constant-buffer claim was accepted");
		}

		// a rejected draw anchor must leave no registration, slot or define behind
		constexpr std::uint32_t kAnchorSlot = 31;
		g_preDrawInstallFails = true;
		ShaderReplacementRegistration rejectedAnchor;
		rejectedAnchor.targetId = ShaderInjectionTarget::kBsdfComposite;
		rejectedAnchor.contributor = "ledger-rejected-anchor";
		rejectedAnchor.defines = { { "LEDGER_ANCHOR", "1" } };
		rejectedAnchor.bind = [](ID3D11DeviceContext*) {};
		rejectedAnchor.slotClaims = { claim(kAnchorSlot) };
		ok &= Check(
			!RegisterReplacement(std::move(rejectedAnchor)),
			"a registration whose draw anchor failed was accepted");
		g_preDrawInstallFails = false;

		ShaderReplacementRegistration anchorSlotReuse;
		anchorSlotReuse.targetId = ShaderInjectionTarget::kBsdfComposite;
		anchorSlotReuse.contributor = "ledger-anchor-slot-reuse";
		anchorSlotReuse.defines = { { "LEDGER_ANCHOR", "2" } };
		anchorSlotReuse.slotClaims = { claim(kAnchorSlot) };
		ok &= Check(
			RegisterReplacement(std::move(anchorSlotReuse)),
			"a rejected anchor left its slot or define claim committed");
		return ok;
	}

	bool TestBoundShaderInjectionDispatch(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context)
	{
		FreezeAndCompileShaderInjections(a_device);
		auto* injected = GetInjectedPixelShader(
			ShaderInjectionTarget::kBsdfComposite);
		bool ok = Check(
			injected != nullptr,
			"BSDFComposite injected pixel shader was not published");
		if (!injected)
			return false;

		a_context->PSSetShader(injected, nullptr, 0);
		DispatchInjectionsForBoundPixelShader(a_context);
		auto snapshot = GetShaderInjectionTargetSnapshot(
			ShaderInjectionTarget::kBsdfComposite);
		ok &= Check(
			g_bsdfCompositeBindDispatches == 1
				&& snapshot.dispatches == 1,
			"bound BSDFComposite shader did not dispatch its contributor");

		TestCompilationPolicy independentPolicy(true);
		ShaderVariantCompilationRequest request;
		request.device.copy_from(a_device);
		request.stage = ShaderStage::kPixel;
		const auto independent = independentPolicy.Prepare(
			std::move(request));
		auto* otherShader = independent.handle ?
			static_cast<ID3D11PixelShader*>(
				independent.handle->PeekShader()) :
			nullptr;
		ok &= Check(
			otherShader && otherShader != injected,
			"non-injected pixel shader fixture was not distinct");
		if (otherShader) {
			a_context->PSSetShader(otherShader, nullptr, 0);
			DispatchInjectionsForBoundPixelShader(a_context);
		}
		snapshot = GetShaderInjectionTargetSnapshot(
			ShaderInjectionTarget::kBsdfComposite);
		ok &= Check(
			g_bsdfCompositeBindDispatches == 1
				&& snapshot.dispatches == 1,
			"non-injected pixel shader dispatched a contributor");
		a_context->PSSetShader(nullptr, nullptr, 0);
		return ok;
	}

}

int main(int a_argc, char* a_argv[])
{
	if (a_argc == 2
		&& std::string_view(a_argv[1])
			== "--baseline-ownership") {
		return TestBaselineOwnershipWithoutContributors();
	}
	if (a_argc != 1) {
		std::cerr << "FAIL: invalid arguments\n";
		return 1;
	}

	bool ok = TestStageScopedContributions();
	ok &= TestVertexCompileClassPartition();
	ok &= TestAutomaticSubstrateDefine();

	ShaderReplacementRegistration emptyStageMask;
	emptyStageMask.targetId =
		ShaderInjectionTarget::kDeferredPrepass;
	emptyStageMask.stages = 0;
	emptyStageMask.contributor = "empty-stage-mask";
	emptyStageMask.bind = [](ID3D11DeviceContext*) {};
	ok &= Check(
		!RegisterReplacement(std::move(emptyStageMask)),
		"empty contribution stage mask was accepted");
	ok &= Check(
		g_preDrawInstallRequests == 0,
		"empty contribution stage mask installed the pre-draw hook");

	ShaderReplacementRegistration invalidStageMask;
	invalidStageMask.targetId =
		ShaderInjectionTarget::kDeferredPrepass;
	invalidStageMask.stages =
		ShaderStageBit(ShaderStage::kCount);
	invalidStageMask.contributor = "invalid-stage-mask";
	ok &= Check(
		!RegisterReplacement(std::move(invalidStageMask)),
		"out-of-range contribution stage mask was accepted");

	ShaderReplacementRegistration disabledWetnessAmbient;
	disabledWetnessAmbient.targetId = ShaderInjectionTarget::kBsdfComposite;
	disabledWetnessAmbient.contributor = "WetnessEffects";
	disabledWetnessAmbient.bind = [](ID3D11DeviceContext*) {};
	ok &= Check(
		RegisterReplacementIfEnabled(
			false,
			std::move(disabledWetnessAmbient)),
		"disabled WetnessEffects ambient registration failed");

	ShaderReplacementRegistration disabledSsgiAmbient;
	disabledSsgiAmbient.targetId = ShaderInjectionTarget::kBsdfComposite;
	disabledSsgiAmbient.contributor = "ScreenSpaceGI";
	disabledSsgiAmbient.bind = [](ID3D11DeviceContext*) {};
	ShaderReplacementVariantRegistration mismatchedProfile;
	mismatchedProfile.targetId = ShaderInjectionTarget::kDeferredComposite;
	mismatchedProfile.name = "mismatched-profile";
	mismatchedProfile.stage = ShaderStage::kVertex;
	mismatchedProfile.compilation.sourcePath = L"registration-test.hlsl";
	mismatchedProfile.compilation.entryPoint = "main";
	mismatchedProfile.compilation.profile = "ps_5_0";
	ok &= Check(
		!RegisterReplacementVariant(std::move(mismatchedProfile)),
		"vertex registration accepted a pixel profile");

	auto vertexKeyRegistration = MakeRegistration(
		"vertex-key-without-resolver",
		1,
		"1111111111111111111111111111111111111111");
	vertexKeyRegistration.stage = ShaderStage::kVertex;
	vertexKeyRegistration.variantKeys.front().stage =
		ShaderStage::kVertex;
	vertexKeyRegistration.compilation.profile = "vs_5_0";
	ok &= Check(
		!RegisterReplacementVariant(std::move(vertexKeyRegistration)),
		"vertex registration accepted an inert variant key");

	const auto staticFamilies = GetDefaultShaderReplacementVariants();
	std::map<ShaderInjectionTarget, std::size_t, std::less<>> familyCounts;
	std::map<ShaderInjectionTarget, std::size_t, std::less<>>
		vertexFamilyCounts;
	std::set<std::string, std::less<>> stockHashes;
	for (const auto& registration : staticFamilies) {
		const auto expectedProfile =
			registration.stage == ShaderStage::kVertex
			? "vs_5_0"
			: "ps_5_0";
		ok &= Check(
			registration.compilation.profile == expectedProfile,
			"registration profile does not match its shader stage");
		ok &= Check(
			std::ranges::all_of(
				registration.variantKeys,
				[&registration](const ShaderVariantKey& a_key) {
					return a_key.stage == registration.stage;
				}),
			"registration key does not match its shader stage");
		if (RequiresStockHash(registration.targetId)) {
			ok &= Check(
				IsLowerHexSha1(registration.expectedStockSha1),
				"baseline-ownable registration lacks a lowercase 40-hex stock hash");
		} else if (!registration.expectedStockSha1.empty()) {
			ok &= Check(
				IsLowerHexSha1(registration.expectedStockSha1),
				"stock hash is not lowercase 40-hex");
		}
		if (!registration.expectedStockSha1.empty()) {
			ok &= Check(
				stockHashes.insert(registration.expectedStockSha1).second,
				"stock hash is claimed by more than one registration");
		}
		switch (registration.targetId) {
		case ShaderInjectionTarget::kBsSky:
		case ShaderInjectionTarget::kBsWater:
		case ShaderInjectionTarget::kBsLighting:
		case ShaderInjectionTarget::kBsdfLight:
		case ShaderInjectionTarget::kBsdfComposite:
			if (registration.stage == ShaderStage::kPixel)
				++familyCounts[registration.targetId];
			else
				++vertexFamilyCounts[registration.targetId];
			break;
		default:
			break;
		}
	}
	ok &= Check(
		familyCounts[ShaderInjectionTarget::kBsSky] == 9,
		"BSSky registration count mismatch");
	ok &= Check(
		familyCounts[ShaderInjectionTarget::kBsWater] == 38,
		"BSWater registration count mismatch");
	ok &= Check(
		familyCounts[ShaderInjectionTarget::kBsLighting] == 12,
		"BSLighting registration count mismatch");
	ok &= Check(
		familyCounts[ShaderInjectionTarget::kBsdfLight] == 166,
		"BSDFLight pixel registration count mismatch");
	ok &= Check(
		familyCounts[ShaderInjectionTarget::kBsdfComposite] == 70,
		"BSDFComposite pixel registration count mismatch");
	ok &= Check(
		vertexFamilyCounts[ShaderInjectionTarget::kBsSky] == 7,
		"BSSky vertex representative count mismatch");
	ok &= Check(
		vertexFamilyCounts[ShaderInjectionTarget::kBsWater] == 16,
		"BSWater vertex representative count mismatch");
	ok &= Check(
		vertexFamilyCounts[ShaderInjectionTarget::kBsLighting] == 8,
		"BSLighting vertex representative count mismatch");
	ok &= Check(
		vertexFamilyCounts[ShaderInjectionTarget::kBsdfLight] == 1,
		"BSDFLight vertex representative count mismatch");
	ok &= Check(
		vertexFamilyCounts[ShaderInjectionTarget::kBsdfComposite] == 4,
		"BSDFComposite vertex representative count mismatch");
	ok &= Check(
		staticFamilies.size() == 334,
		"default shader replacement variant count mismatch");
	ok &= Check(
		stockHashes.size() == 332,
		"default shader replacement variant non-empty stock hash count mismatch");

	constexpr std::array<std::pair<ShaderInjectionTarget, std::wstring_view>, 5>
		kFamilySources{ {
			{ ShaderInjectionTarget::kBsSky, L"BSSkyShader.hlsl" },
			{ ShaderInjectionTarget::kBsWater, L"BSWaterShader.hlsl" },
			{ ShaderInjectionTarget::kBsLighting, L"BSLightingShader.hlsl" },
			{ ShaderInjectionTarget::kBsdfLight, L"BSDFLightShader.hlsl" },
			{ ShaderInjectionTarget::kBsdfComposite, L"BSDFCompositeShader.hlsl" }
		} };
	for (const auto& [target, sourcePath] : kFamilySources) {
		const auto* metadata = GetShaderInjectionTarget(target);
		ok &= Check(
			metadata != nullptr,
			"static family target metadata is missing");
		if (metadata == nullptr)
			continue;
		ok &= Check(
			metadata->sourcePath == sourcePath,
			"static family source path mismatch");
		ok &= Check(
			metadata->entryPoint == "main",
			"static family entry point mismatch");
		ok &= Check(
			metadata->profile == "ps_5_0",
			"static family profile mismatch");
	}
	ok &= Check(
		RegisterReplacementIfEnabled(
			false,
			std::move(disabledSsgiAmbient)),
		"disabled ScreenSpaceGI ambient registration failed");
	ok &= Check(
		g_preDrawInstallRequests == 0,
		"disabled ambient registrations installed the pre-draw hook");
	ok &= Check(
		g_sharedDataInstallRequests == 0,
		"rejected registrations installed the substrate update");

	ShaderReplacementRegistration noBindRegistration;
	noBindRegistration.targetId = ShaderInjectionTarget::kDeferredPrepass;
	noBindRegistration.contributor = "registration-no-bind";
	ok &= Check(
		RegisterReplacement(std::move(noBindRegistration)),
		"registration without a bind was rejected");
	ok &= Check(
		g_preDrawInstallRequests == 0,
		"registration without a bind installed the pre-draw hook");
	ok &= Check(
		g_sharedDataInstallRequests == 1,
		"accepted registration did not install the substrate update");

	ShaderReplacementRegistration bindRegistration;
	bindRegistration.targetId = ShaderInjectionTarget::kBsdfComposite;
	bindRegistration.contributor = "registration-with-bind";
	bindRegistration.bind = [](ID3D11DeviceContext*) {
		++g_bsdfCompositeBindDispatches;
	};
	ok &= Check(
		RegisterReplacement(std::move(bindRegistration)),
		"registration with a bind was rejected");
	ok &= Check(
		g_preDrawInstallRequests == 1,
		"registration with a bind did not install the pre-draw hook");

	ok &= TestClaimLedger();

	constexpr auto baseSha =
		"1111111111111111111111111111111111111111";
	ok &= Check(
		RegisterReplacementVariant(
			MakeRegistration("registration-base", 0xABC001, baseSha)),
		"valid baseline registration was rejected");
	ok &= Check(
		!RegisterReplacementVariant(
			MakeRegistration(
				"duplicate-key",
				0xABC001,
				"2222222222222222222222222222222222222222")),
		"duplicate scoped key was accepted");
	ok &= Check(
		!RegisterReplacementVariant(
			MakeRegistration(
				"registration-base",
				0xABC002,
				"3333333333333333333333333333333333333333")),
		"duplicate target/name was accepted");
	ok &= Check(
		!RegisterReplacementVariant(
			MakeRegistration("duplicate-sha", 0xABC003, baseSha)),
		"duplicate expected stock SHA1 was accepted");
	if (ok) {
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		ok &= CreateWarpDevice(device, context);
		if (device && context) {
			ok &= TestPixelShaderResourceSnapshot(
				device.get(),
				context.get());
			ok &= TestBoundShaderInjectionDispatch(
				device.get(),
				context.get());
		}
	}
	if (!ok)
		return 1;
	std::cout
		<< "PASS: shader injection registration, binding, and dispatch guards\n";
	return 0;
}

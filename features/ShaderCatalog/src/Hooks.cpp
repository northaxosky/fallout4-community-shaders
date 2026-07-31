#include "Hooks.h"

#include "CatalogDB.h"
#include "AttributionPolicy.h"
#include "Log.h"
#include "PixelShaderTracker.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/ShaderSubclassContext.h"
#include "Render/ShaderVariantRuntimeResolver.h"

#include <atomic>
#include <memory>
#include <new>
#include <utility>

namespace cs::features::catalog::hooks
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.shadercatalog");

		std::atomic<bool> g_psSetShaderHookInstalled{ false };
		std::atomic<bool> g_pixelObserverRegistered{ false };
		std::atomic<bool> g_subclassAttributionEnabled{ false };
		std::atomic<std::uint64_t> g_scopedBinds{ 0 };
		std::atomic<std::uint64_t> g_matchedBinds{ 0 };
		std::atomic<std::uint64_t> g_missedBinds{ 0 };

		struct PixelToken
		{
			PreparedObservation prepared;
			RouteCaptureAdmission routeAdmission;
		};

		bool BeginPixelShaderAdmission() noexcept
		{
			return CatalogDB::Get().TryBeginProducerAdmission();
		}

		void EndPixelShaderAdmission() noexcept
		{
			CatalogDB::Get().EndProducerAdmission();
		}

		PreparedObservation Prepare(
			char a_stage,
			const void* a_bytecode,
			SIZE_T a_bytecodeLength,
			ULONG a_stackFramesToSkip = 2) noexcept
		{
			return PrepareObservation(
				a_stage,
				a_bytecode,
				static_cast<std::size_t>(a_bytecodeLength),
				CatalogDB::Get().NextSequence(),
				a_stackFramesToSkip);
		}

		template <class Shader>
		void Complete(
			PreparedObservation a_prepared,
			HRESULT a_result,
			Shader** a_output,
			const CatalogDB::ProducerLease& a_lease) noexcept
		{
			ObservationOutcome observation;
			observation.prepared = std::move(a_prepared);
			observation.hresult = static_cast<std::int32_t>(a_result);
			observation.outputRequested = a_output != nullptr;
			observation.outputNonNull = a_output && *a_output;
			const bool usable = SUCCEEDED(a_result)
				&& observation.outputRequested
				&& observation.outputNonNull;
			observation.finalIsStock = usable;
			observation.finalIsNull =
				observation.outputRequested && !observation.outputNonNull;
			CatalogDB::Get().EnqueueObservation(
				std::move(observation), &a_lease);
		}

		void* PreparePixelShaderBytecodeDetailed(
			const cs::engine::PixelShaderCreationDescriptor&
				a_descriptor) noexcept
		{
			RouteCreateInput routeInput{
				.routePresent = a_descriptor.route.has_value(),
				.classLinkagePresent =
					a_descriptor.classLinkagePresent
			};
			if (a_descriptor.route) {
				routeInput.subclass =
					a_descriptor.route->subclass;
				routeInput.rawTechnique =
					a_descriptor.route->rawTechnique;
				if (a_descriptor.route->pluginResolvedPsid) {
					routeInput.pluginResolvedPsid =
						a_descriptor.route
							->pluginResolvedPsid->Value();
				}
				if (a_descriptor.route->engineLookup) {
					const auto& engineLookup =
						*a_descriptor.route->engineLookup;
					routeInput.engineLookup =
						RouteEngineLookupInputEvent{
							.targetId = cs::engine::
								EnginePixelShaderLookupTargetName(
									engineLookup.target),
							.functionInput =
								engineLookup.functionInput,
							.returnedPsid =
								engineLookup.returnedPsid.Value(),
							.callSequence =
								engineLookup.callSequence,
							.threadId = engineLookup.threadId
						};
				}
				routeInput.tiledLighting =
					a_descriptor.route->tiledLighting;
			}
			auto routeAdmission =
				CatalogDB::Get().BeginRouteCreate(routeInput);
			auto* token = new (std::nothrow) PixelToken;
			if (!token)
				CatalogDB::Get().RecordAllocationFailure();
			else {
				token->routeAdmission =
					std::move(routeAdmission);
				token->prepared = Prepare(
					'p',
					a_descriptor.bytecode,
					a_descriptor.bytecodeLength,
					3);
			}
			return token;
		}

		void ObserveOriginalPixelShader(
			void*,
			const cs::sha1::Sha1Result& a_sha,
			ID3D11PixelShader* a_shader) noexcept
		{
			const auto result =
				shader_tracker::TrackPixelShader(a_shader, a_sha);
			if (result == shader_tracker::TrackResult::kAllocationFailure)
				CatalogDB::Get().RecordAllocationFailure();
			else if (result == shader_tracker::TrackResult::kAmbiguousOrigin)
				CatalogDB::Get().RecordHookObserverGap();
		}

		void CompletePixelShader(
			void* a_token,
			const cs::engine::PixelShaderSwapCompletion& a_completion) noexcept
		{
			std::unique_ptr<PixelToken> token(
				static_cast<PixelToken*>(a_token));
			if (!token)
				return;
			auto& prepared = token->prepared;
			RouteCreateCommitResult routeCommit;
			if (token->routeAdmission) {
				RouteCreateOutcome routeOutcome{
					.byteLength = prepared.submittedSize,
					.hresult = static_cast<std::uint32_t>(
						a_completion.originalResult),
					.creationSucceeded =
						SUCCEEDED(a_completion.originalResult),
					.outputNonNull =
						a_completion.stockOutput != nullptr,
					.originalInputUnchanged =
						a_completion.originalInputUnchanged,
					.resolverInvoked =
						a_completion.resolverInvoked
				};
				if (prepared.digest) {
					routeOutcome.sha1 = HexLower(
						prepared.digest->sha1.data(),
						prepared.digest->sha1.size());
					routeOutcome.sha256 = HexLower(
						prepared.digest->sha256.data(),
						prepared.digest->sha256.size());
				}
				if (routeOutcome.creationSucceeded
					&& routeOutcome.outputNonNull) {
					routeOutcome.finalObjectStock =
						a_completion.finalIsStock;
				}
				routeCommit =
					CatalogDB::Get().CompleteRouteCreate(
						std::move(token->routeAdmission),
						routeOutcome);
				if (routeCommit.enqueued
					&& routeCommit.usableStockObject
					&& routeCommit.record
					&& a_completion.stockOutput
					&& prepared.digest) {
					Sha1Result sha{};
					sha.bytes = prepared.digest->sha1;
					const auto routeTracked =
						shader_tracker::TrackRouteLineage(
							a_completion.stockOutput,
							sha,
							routeCommit.record);
					if (routeTracked
						== shader_tracker::RouteTrackResult::
							kAllocationFailure) {
						CatalogDB::Get()
							.RecordAllocationFailure();
					} else if (
						routeTracked
							== shader_tracker::RouteTrackResult::
								kAmbiguous
						|| routeTracked
							== shader_tracker::RouteTrackResult::
								kDuplicate) {
						CatalogDB::Get()
							.RecordHookObserverGap();
					}
				}
			}

			if (a_completion.finalIsReplacement
				&& a_completion.finalOutput
				&& prepared.digest) {
				Sha1Result sha{};
				sha.bytes = prepared.digest->sha1;
				const auto result = shader_tracker::TrackPixelShader(
					a_completion.finalOutput, sha, true);
				if (result == shader_tracker::TrackResult::kAllocationFailure)
					CatalogDB::Get().RecordAllocationFailure();
				else if (result
					== shader_tracker::TrackResult::kAmbiguousOrigin)
					CatalogDB::Get().RecordHookObserverGap();
			}

			std::optional<Sha1Result> attributionSha;
			const auto attribution =
				cs::engine::shader_context::Current();
			if (g_subclassAttributionEnabled.load(std::memory_order_acquire)
				&& prepared.digest && attribution.active
				&& attribution.subclassName) {
				Sha1Result sha{};
				sha.bytes = prepared.digest->sha1;
				attributionSha = sha;
			}

			ObservationOutcome observation;
			observation.prepared = std::move(prepared);
			observation.hresult = a_completion.originalResult;
			observation.outputRequested = a_completion.outputRequested;
			observation.outputNonNull =
				a_completion.stockOutput != nullptr;
			observation.resolverInvoked =
				a_completion.resolverInvoked;
			observation.resolverReportedReplacement =
				a_completion.resolverReportedReplacement;
			observation.finalIsStock = a_completion.finalIsStock;
			observation.finalIsReplacement =
				a_completion.finalIsReplacement;
			observation.finalIsNull = a_completion.finalIsNull;
			CatalogDB::Get().EnqueueObservationAdmitted(
				std::move(observation));

			if (attributionSha) {
				CatalogDB::Get().EnqueueAttributionAdmitted(
					*attributionSha,
					attribution.subclassName,
					attribution.techniqueBits,
					AttributionKind::kCreationContext,
					CreationAttributionObjectKind(
						a_completion.finalIsStock,
						a_completion.finalIsReplacement));
			}
		}
	}

	HRESULT STDMETHODCALLTYPE CreateVertexShaderHook::thunk(
		ID3D11Device* a_this,
		const void* a_bytecode,
		SIZE_T a_bytecode_len,
		ID3D11ClassLinkage* a_linkage,
		ID3D11VertexShader** a_out)
	{
		auto lease = CatalogDB::Get().TryAcquireProducerLease();
		auto prepared = lease
			? Prepare('v', a_bytecode, a_bytecode_len)
			: PreparedObservation{};
		const HRESULT result = func(
			a_this, a_bytecode, a_bytecode_len, a_linkage, a_out);
		if (lease)
			Complete(std::move(prepared), result, a_out, lease);
		return result;
	}

	void STDMETHODCALLTYPE PSSetShaderHook::thunk(
		ID3D11DeviceContext* a_this,
		ID3D11PixelShader* a_shader,
		ID3D11ClassInstance* const* a_classInstances,
		UINT a_numClassInstances)
	{
		auto lease = CatalogDB::Get().TryAcquireProducerLease();
		auto routeBind = CatalogDB::Get().BeginRouteBind();
		func(a_this, a_shader, a_classInstances, a_numClassInstances);

		if (routeBind && a_shader) {
			const auto routeRecord =
				shader_tracker::TryReserveRouteBind(a_shader);
			if (routeRecord) {
				std::optional<RouteBindSnapshot> bindRoute;
				const auto current =
					cs::engine::shader_context::CurrentOrSticky();
				if (current.active
					&& current.techniqueKnown
					&& current.subclassName) {
					const auto resolved =
						cs::engine::ResolvePixelShaderRuntimeRoute(
							current.subclassName,
							current.techniqueBits);
					if (resolved) {
						bindRoute = RouteBindSnapshot{
							std::string(resolved->subclass),
							"ps",
							resolved->rawTechnique,
							resolved->tiledLighting
						};
					}
				}
				if (!CatalogDB::Get().RecordRouteBind(
						std::move(routeBind),
						routeRecord,
						std::move(bindRoute))) {
					CatalogDB::Get()
						.ReleaseRouteBindReservation(routeRecord);
				}
			}
		}

		if (!g_subclassAttributionEnabled.load(
				std::memory_order_acquire)) {
			cs::engine::shader_context::ClearSticky();
			return;
		}
		if (!lease) {
			cs::engine::shader_context::ClearSticky();
			return;
		}
		const auto current =
			cs::engine::shader_context::CurrentOrSticky();
		if (!current.active || !current.subclassName || !a_shader) {
			cs::engine::shader_context::ClearSticky();
			return;
		}

		g_scopedBinds.fetch_add(1, std::memory_order_relaxed);
		shader_tracker::Lookup lookup;
		if (shader_tracker::TryGetPixelShader(a_shader, lookup)) {
			if (lookup.ambiguousOrigin) {
				CatalogDB::Get().RecordHookObserverGap();
				cs::engine::shader_context::ClearSticky();
				return;
			}
			g_matchedBinds.fetch_add(1, std::memory_order_relaxed);
			CatalogDB::Get().EnqueueAttribution(
				lookup.sha,
				current.subclassName,
				current.techniqueBits,
				AttributionKind::kObservedBinding,
				lookup.alias
					? AttributionObjectKind::kReplacementUnknown
					: AttributionObjectKind::kStock,
				&lease);
		} else {
			const auto miss = g_missedBinds.fetch_add(
				1, std::memory_order_relaxed);
			if (miss < 8)
				L->debug(
					"PSSetShader in {} scope missed pixel-shader tracker",
					current.subclassName);
		}
		cs::engine::shader_context::ClearSticky();
	}

	void SetSubclassAttributionEnabled(bool a_enabled) noexcept
	{
		g_subclassAttributionEnabled.store(
			a_enabled, std::memory_order_release);
	}

	HRESULT STDMETHODCALLTYPE CreateGeometryShaderHook::thunk(
		ID3D11Device* a_this,
		const void* a_bytecode,
		SIZE_T a_bytecode_len,
		ID3D11ClassLinkage* a_linkage,
		ID3D11GeometryShader** a_out)
	{
		auto lease = CatalogDB::Get().TryAcquireProducerLease();
		auto prepared = lease
			? Prepare('g', a_bytecode, a_bytecode_len)
			: PreparedObservation{};
		const HRESULT result = func(
			a_this, a_bytecode, a_bytecode_len, a_linkage, a_out);
		if (lease)
			Complete(std::move(prepared), result, a_out, lease);
		return result;
	}

	HRESULT STDMETHODCALLTYPE CreateGeometryShaderWithStreamOutputHook::thunk(
		ID3D11Device* a_this,
		const void* a_bytecode,
		SIZE_T a_bytecode_len,
		const D3D11_SO_DECLARATION_ENTRY* a_declaration,
		UINT a_entry_count,
		const UINT* a_strides,
		UINT a_stride_count,
		UINT a_rasterized_stream,
		ID3D11ClassLinkage* a_linkage,
		ID3D11GeometryShader** a_out)
	{
		auto lease = CatalogDB::Get().TryAcquireProducerLease();
		auto prepared = lease
			? Prepare('g', a_bytecode, a_bytecode_len)
			: PreparedObservation{};
		if (lease) {
			prepared.streamOutput = PrepareStreamOutputIdentity(
				a_declaration,
				a_entry_count,
				a_strides,
				a_stride_count,
				a_rasterized_stream);
		}
		const HRESULT result = func(
			a_this,
			a_bytecode,
			a_bytecode_len,
			a_declaration,
			a_entry_count,
			a_strides,
			a_stride_count,
			a_rasterized_stream,
			a_linkage,
			a_out);
		if (lease)
			Complete(std::move(prepared), result, a_out, lease);
		return result;
	}

	HRESULT STDMETHODCALLTYPE CreateComputeShaderHook::thunk(
		ID3D11Device* a_this,
		const void* a_bytecode,
		SIZE_T a_bytecode_len,
		ID3D11ClassLinkage* a_linkage,
		ID3D11ComputeShader** a_out)
	{
		auto lease = CatalogDB::Get().TryAcquireProducerLease();
		auto prepared = lease
			? Prepare('c', a_bytecode, a_bytecode_len)
			: PreparedObservation{};
		const HRESULT result = func(
			a_this, a_bytecode, a_bytecode_len, a_linkage, a_out);
		if (lease)
			Complete(std::move(prepared), result, a_out, lease);
		return result;
	}

	HRESULT STDMETHODCALLTYPE CreateHullShaderHook::thunk(
		ID3D11Device* a_this,
		const void* a_bytecode,
		SIZE_T a_bytecode_len,
		ID3D11ClassLinkage* a_linkage,
		ID3D11HullShader** a_out)
	{
		auto lease = CatalogDB::Get().TryAcquireProducerLease();
		auto prepared = lease
			? Prepare('h', a_bytecode, a_bytecode_len)
			: PreparedObservation{};
		const HRESULT result = func(
			a_this, a_bytecode, a_bytecode_len, a_linkage, a_out);
		if (lease)
			Complete(std::move(prepared), result, a_out, lease);
		return result;
	}

	HRESULT STDMETHODCALLTYPE CreateDomainShaderHook::thunk(
		ID3D11Device* a_this,
		const void* a_bytecode,
		SIZE_T a_bytecode_len,
		ID3D11ClassLinkage* a_linkage,
		ID3D11DomainShader** a_out)
	{
		auto lease = CatalogDB::Get().TryAcquireProducerLease();
		auto prepared = lease
			? Prepare('d', a_bytecode, a_bytecode_len)
			: PreparedObservation{};
		const HRESULT result = func(
			a_this, a_bytecode, a_bytecode_len, a_linkage, a_out);
		if (lease)
			Complete(std::move(prepared), result, a_out, lease);
		return result;
	}

	bool InstallAll(ID3D11Device* a_device)
	{
		if (!CreateVertexShaderHook::func)
			stl::detour_vfunc<12, CreateVertexShaderHook>(a_device);
		if (!CreateGeometryShaderHook::func)
			stl::detour_vfunc<13, CreateGeometryShaderHook>(a_device);
		if (!CreateGeometryShaderWithStreamOutputHook::func)
			stl::detour_vfunc<14, CreateGeometryShaderWithStreamOutputHook>(a_device);
		bool observerRegistered =
			g_pixelObserverRegistered.load(std::memory_order_acquire);
		if (!observerRegistered) {
			observerRegistered =
				cs::engine::RegisterPixelShaderSwapObserver({
					.beginAdmission = &BeginPixelShaderAdmission,
					.endAdmission = &EndPixelShaderAdmission,
					.observeOriginal = &ObserveOriginalPixelShader,
					.complete = &CompletePixelShader,
					.prepareDetailed =
						&PreparePixelShaderBytecodeDetailed
				});
			if (observerRegistered) {
				g_pixelObserverRegistered.store(
					true, std::memory_order_release);
			}
		}
		if (!observerRegistered) {
			L->error("Pixel-shader catalog observer registration failed.");
		}
		cs::engine::SetPixelShaderSwapBrokerDevice(a_device);
		if (!CreateHullShaderHook::func)
			stl::detour_vfunc<16, CreateHullShaderHook>(a_device);
		if (!CreateDomainShaderHook::func)
			stl::detour_vfunc<17, CreateDomainShaderHook>(a_device);
		if (!CreateComputeShaderHook::func)
			stl::detour_vfunc<18, CreateComputeShaderHook>(a_device);

		ID3D11DeviceContext* context = nullptr;
		a_device->GetImmediateContext(&context);
		if (context && !PSSetShaderHook::func) {
			stl::detour_vfunc<9, PSSetShaderHook>(context);
		}
		if (context)
			context->Release();
		const HookCoverage coverage{
			CreateVertexShaderHook::func != nullptr,
			CreateGeometryShaderHook::func != nullptr,
			CreateGeometryShaderWithStreamOutputHook::func != nullptr,
			cs::engine::PixelShaderSwapBrokerHooksInstalled(),
			CreateHullShaderHook::func != nullptr,
			CreateDomainShaderHook::func != nullptr,
			CreateComputeShaderHook::func != nullptr,
			PSSetShaderHook::func != nullptr,
			observerRegistered
		};
		g_psSetShaderHookInstalled.store(
			coverage.pixelBinding, std::memory_order_release);
		if (coverage.Complete())
			CatalogDB::Get().MarkHookCoverageReady();
		else {
			CatalogDB::Get().RecordHookObserverGap();
			L->error("Shader catalog hook coverage is incomplete.");
		}
		return coverage.Complete();
	}

	RuntimeAttributionStats GetRuntimeAttributionStats()
	{
		return RuntimeAttributionStats{
			g_psSetShaderHookInstalled.load(std::memory_order_acquire),
			g_scopedBinds.load(std::memory_order_relaxed),
			g_matchedBinds.load(std::memory_order_relaxed),
			g_missedBinds.load(std::memory_order_relaxed)
		};
	}
}

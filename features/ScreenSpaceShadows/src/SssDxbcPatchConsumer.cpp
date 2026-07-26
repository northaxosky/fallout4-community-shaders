#include "SssDxbcPatchConsumer.h"

#include "Log.h"
#include "LogThrottle.h"
#include "Render/DxbcPatchArtifact.h"
#include "Render/PixelShaderSwapBroker.h"
#include "SssDxbcPatchRuntime.h"
#include "Utils/CSSha256.h"

#include <REX/FModule.h>

#include <Windows.h>

#include <array>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#pragma comment(lib, "version.lib")

namespace cs::features::sss_dxbc_patch
{
	namespace
	{
		constexpr std::size_t kArtifactLength = 53601;
		constexpr std::string_view kArtifactSha256 =
			"eaa508727cd08ed20b9d3c5db823eebe28e23b78f948e179dab84ae9217d8483";
		constexpr std::string_view kRelease =
			"fallout4-ae-1.11.221.0";
		constexpr std::string_view kEngineContractScope =
			"BSDFLightShaderMacros::Get+GetPixelShaderID@fallout4-ae-1.11.221.0";

		auto* L = cs::log::Get("cs.feature.screenspaceshadows.dxbcpatch");
		constexpr std::array kDispatchTargets{
			engine::ShaderInjectionTarget::
				kBsdfLightDeferredDirectional,
			engine::ShaderInjectionTarget::
				kBsdfLightDeferredDirectionalIbl
		};

		struct Service
		{
			std::mutex mutex;
			std::shared_ptr<const engine::dxbc_patch::Artifact> artifact;
			engine::dxbc_patch::RuntimeIdentity runtime;
			std::atomic_bool runtimeExact{ false };
			BindingReadinessLatch bindingReadiness;
			std::atomic_bool resolverActive{ false };
			AtomicCounters counters;
		};

		Service& GetService()
		{
			static Service service;
			return service;
		}

		bool ReadRuntimeVersion(
			const std::filesystem::path& a_path,
			std::string& a_version)
		{
			DWORD unused = 0;
			const DWORD size =
				::GetFileVersionInfoSizeW(a_path.c_str(), &unused);
			if (!size)
				return false;
			std::vector<unsigned char> buffer(size);
			if (!::GetFileVersionInfoW(
					a_path.c_str(),
					0,
					size,
					buffer.data())) {
				return false;
			}
			VS_FIXEDFILEINFO* info = nullptr;
			UINT infoLength = 0;
			if (!::VerQueryValueW(
					buffer.data(),
					L"\\",
					reinterpret_cast<void**>(&info),
					&infoLength)
				|| !info
				|| infoLength
					< static_cast<UINT>(sizeof(VS_FIXEDFILEINFO))) {
				return false;
			}
			char version[32]{};
			const auto length = std::snprintf(
				version,
				sizeof(version),
				"%u.%u.%u.%u",
				HIWORD(info->dwFileVersionMS),
				LOWORD(info->dwFileVersionMS),
				HIWORD(info->dwFileVersionLS),
				LOWORD(info->dwFileVersionLS));
			if (length <= 0
				|| static_cast<std::size_t>(length) >= sizeof(version)) {
				return false;
			}
			a_version.assign(version, static_cast<std::size_t>(length));
			return true;
		}

		std::optional<engine::dxbc_patch::RuntimeIdentity>
			DetectRuntimeIdentity()
		{
			if (!REX::FModule::IsRuntimeAE())
				return std::nullopt;
			const HMODULE module = ::GetModuleHandleW(L"Fallout4.exe");
			if (!module)
				return std::nullopt;
			std::vector<wchar_t> pathBuffer(32768);
			const DWORD pathLength = ::GetModuleFileNameW(
				module,
				pathBuffer.data(),
				static_cast<DWORD>(pathBuffer.size()));
			if (pathLength == 0
				|| static_cast<std::size_t>(pathLength)
					>= pathBuffer.size())
				return std::nullopt;
			const std::filesystem::path path(
				pathBuffer.data(),
				pathBuffer.data() + pathLength);

			engine::dxbc_patch::RuntimeIdentity result;
			if (!ReadRuntimeVersion(path, result.executableVersion)
				|| !sha256::Sha256ComputeFile(
					path,
					result.executableSha256,
					result.executableLength)) {
				return std::nullopt;
			}
			result.release = std::string(kRelease);
			result.engineContractScope =
				std::string(kEngineContractScope);
			return result;
		}

		HRESULT CreatePatchedPixelShader(
			ID3D11Device* a_device,
			const void* a_bytecode,
			SIZE_T a_bytecodeLength,
			ID3D11ClassLinkage* a_linkage,
			ID3D11PixelShader** a_output) noexcept
		{
			if (!a_device)
				return E_POINTER;
			return a_device->CreatePixelShader(
				a_bytecode,
				a_bytecodeLength,
				a_linkage,
				a_output);
		}

		void BindPixelShader(
			void* a_context,
			ID3D11PixelShader* a_shader) noexcept
		{
			static_cast<ID3D11DeviceContext*>(a_context)->PSSetShader(
				a_shader,
				nullptr,
				0);
		}

		engine::PixelShaderSwapResolverResult Resolve(
			const engine::PixelShaderSwapRequest& a_request) noexcept
		{
			auto& service = GetService();
			const auto artifact = service.artifact;
			if (!artifact)
				return engine::PixelShaderSwapResolverResult::kNoMatch;
			return ResolveRequest(
				*artifact,
				service.runtimeExact.load(std::memory_order_acquire),
				engine::ArePatchedShaderDispatchesPublished(
					kDispatchTargets),
				service.bindingReadiness.IsReady(),
				service.counters,
				a_request,
				&CreatePatchedPixelShader);
		}

		std::optional<engine::dxbc_patch::Target> MapTarget(
			engine::ShaderInjectionTarget a_target) noexcept
		{
			switch (a_target) {
			case engine::ShaderInjectionTarget::
				kBsdfLightDeferredDirectional:
				return engine::dxbc_patch::Target::kDirectional;
			case engine::ShaderInjectionTarget::
				kBsdfLightDeferredDirectionalIbl:
				return engine::dxbc_patch::Target::kDirectionalIbl;
			default:
				return std::nullopt;
			}
		}
	}

	bool Prepare(
		const std::filesystem::path& a_artifactPath,
		std::string& a_error)
	{
		a_error.clear();
		sha256::Sha256Result expectedDigest{};
		if (!sha256::Sha256FromHex(kArtifactSha256, expectedDigest)) {
			a_error = "compiled artifact digest is invalid";
			return false;
		}
		auto loaded = engine::dxbc_patch::LoadPinnedArtifact(
			a_artifactPath,
			kArtifactLength,
			expectedDigest);
		if (!loaded) {
			a_error = std::move(loaded.error);
			return false;
		}

		auto artifact = std::make_shared<engine::dxbc_patch::Artifact>(
			std::move(*loaded.artifact));
		const auto runtime = DetectRuntimeIdentity();
		auto& service = GetService();
		std::scoped_lock lock(service.mutex);
		if (service.artifact) {
			a_error = "DXBC patch consumer was already prepared";
			return false;
		}
		service.artifact = std::move(artifact);
		if (runtime)
			service.runtime = *runtime;
		const bool exact = runtime
			&& engine::dxbc_patch::RuntimeMatches(
				*service.artifact,
				*runtime);
		service.runtimeExact.store(exact, std::memory_order_release);
		L->info(
			"Loaded provisional SSS DXBC patch artifact: coverage={}/{}, runtime_exact={}, release_scope={}.",
			service.artifact->coverage.pass,
			service.artifact->coverage.candidates,
			exact,
			service.artifact->releaseScope);
		return true;
	}

	bool ActivateResolver()
	{
		auto& service = GetService();
		std::scoped_lock lock(service.mutex);
		if (!service.artifact
			|| service.resolverActive.load(
				std::memory_order_acquire))
			return false;
		const bool active =
			engine::RegisterPixelShaderSwapResolver({
				.resolver = &Resolve,
				.priority = engine::kBytecodePatchResolverPriority
			});
		service.resolverActive.store(
			active,
			std::memory_order_release);
		return active;
	}

	void SetBindingReady(bool a_ready) noexcept
	{
		auto& service = GetService();
		(void)service.bindingReadiness.Publish(
			a_ready && static_cast<bool>(service.artifact));
	}

	void BreakBindingInvariant() noexcept
	{
		GetService().bindingReadiness.Break();
	}

	bool BindingInvariantBroken() noexcept
	{
		return GetService().bindingReadiness.IsBroken();
	}

	bool MatchesPatchedShader(
		engine::ShaderInjectionTarget a_target,
		ID3D11PixelShader* a_shader) noexcept
	{
		const auto mapped = MapTarget(a_target);
		const auto artifact = GetService().artifact;
		return mapped
			&& artifact
			&& MatchesPatchedPixelShader(
				a_shader,
				*mapped,
				artifact->plans);
	}

	bool RestoreStockShader(
		ID3D11DeviceContext* a_context) noexcept
	{
		auto& service = GetService();
		const auto artifact = service.artifact;
		if (!a_context || !artifact)
			return false;

		ID3D11PixelShader* shader = nullptr;
		a_context->PSGetShader(&shader, nullptr, nullptr);
		if (!shader)
			return false;
		ID3D11Device* device = nullptr;
		a_context->GetDevice(&device);
		const auto status = RestoreStockPixelShader(
			shader,
			device,
			artifact->plans,
			&CreatePatchedPixelShader,
			a_context,
			&BindPixelShader);
		if (device)
			device->Release();
		shader->Release();
		return status == RestoreStockStatus::kRestored;
	}

	void RecordPatchedDraw(PatchedDrawTelemetry a_telemetry) noexcept
	{
		GetService().counters.Publish({
			.patchedDrawMatched = 1,
			.stockShaderFallback =
				static_cast<std::uint64_t>(
					a_telemetry.stockShaderFallback),
			.stockShaderFallbackFailed =
				static_cast<std::uint64_t>(
					a_telemetry.stockShaderFallbackFailed),
			.realMaskBinds =
				static_cast<std::uint64_t>(
					a_telemetry.realMaskBound),
			.whiteFallbackBinds =
				static_cast<std::uint64_t>(
					a_telemetry.whiteFallbackBound),
			.invariantViolations =
				static_cast<std::uint64_t>(
					a_telemetry.invariantViolated)
		});
	}

	TelemetrySnapshot GetTelemetry() noexcept
	{
		auto& service = GetService();
		const auto artifact = service.artifact;
		auto snapshot = SnapshotCounters(service.counters);
		snapshot.coveragePass = artifact ? artifact->coverage.pass : 0;
		snapshot.coverageCandidates =
			artifact ? artifact->coverage.candidates : 0;
		snapshot.artifactLoaded = static_cast<bool>(artifact);
		snapshot.runtimeExact = service.runtimeExact.load(
			std::memory_order_acquire);
		snapshot.resolverActive = service.resolverActive.load(
			std::memory_order_acquire);
		snapshot.brokerHookInstalled =
			engine::PixelShaderSwapBrokerHooksInstalled();
		snapshot.dispatchPublished =
			engine::ArePatchedShaderDispatchesPublished(
				kDispatchTargets);
		const auto binding =
			service.bindingReadiness.GetSnapshot();
		snapshot.bindingReady = binding.ready;
		snapshot.invariantBroken = binding.broken;
		const auto admission = EvaluatePatchAdmission(
			snapshot.artifactLoaded,
			snapshot.runtimeExact,
			snapshot.resolverActive,
			snapshot.brokerHookInstalled,
			snapshot.dispatchPublished,
			snapshot.bindingReady,
			snapshot.invariantBroken);
		snapshot.admissionReady = admission.ready;
		snapshot.stockFallback = admission.stockFallback;
		return snapshot;
	}

}

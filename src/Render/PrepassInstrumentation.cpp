#include "Render/PrepassInstrumentation.h"

#include "Log.h"
#include "PCH.h"
#include "Render/Annotation.h"
#include "Render/PrepassInstrumentationModel.h"
#include "Telemetry/Telemetry.h"

#include <atomic>
#include <format>
#include <string>

#include <winrt/base.h>

namespace cs::engine::prepass_instrumentation
{
	namespace
	{
		constexpr GUID kStockShaderIdentityGuid{
			0xc9820064,
			0x663b,
			0x4bd0,
			{ 0xa9, 0x58, 0xdb, 0xd2, 0xf4, 0x99, 0x1c, 0x23 }
		};

		struct StockShaderIdentity
		{
			sha1::Sha1Result sha1;
			std::uint32_t ambiguous = 0;
		};

		struct RuntimeCounters
		{
			std::atomic_uint64_t setupTechniques{ 0 };
			std::atomic_uint64_t completedTechniques{ 0 };
			std::atomic_uint64_t zeroDrawTechniques{ 0 };
			std::atomic_uint64_t multiDrawTechniques{ 0 };
			std::atomic_uint64_t draws{ 0 };
			std::atomic_uint64_t objectLodDraws{ 0 };
			std::atomic_uint64_t bitClearDraws{ 0 };
			std::atomic_uint64_t setupReentries{ 0 };
			std::atomic_uint64_t missingTechniqueDraws{ 0 };
			std::atomic_uint64_t missingVertexIdentities{ 0 };
			std::atomic_uint64_t missingPixelIdentities{ 0 };
			std::atomic_uint64_t ambiguousShaderIdentities{ 0 };
			std::atomic_uint64_t maxDrawsPerTechnique{ 0 };
			std::atomic_uint64_t lastObjectLodFrame{ 0 };
			std::atomic_bool hooksInstalled{ false };
			std::atomic_bool shaderTrackingInstalled{ false };
		};

		struct ThreadState
		{
			TechniqueState technique;
			RE::BSShader* shader = nullptr;
		};

		RuntimeCounters g_counters;
		std::atomic_uint64_t g_nextTechniqueSerial{ 0 };
		thread_local ThreadState g_threadState;
		auto* L = cs::log::Get("cs.render.prepass");

		bool Enabled() noexcept
		{
			return cs::log::PrepassTechniqueInstrumentationEnabled();
		}

		void UpdateMaximum(
			std::atomic_uint64_t& a_maximum,
			std::uint64_t a_value) noexcept
		{
			auto maximum = a_maximum.load(std::memory_order_relaxed);
			while (maximum < a_value
				&& !a_maximum.compare_exchange_weak(
					maximum,
					a_value,
					std::memory_order_relaxed)) {
			}
		}

		void CompleteTechnique() noexcept
		{
			const auto drawCount = EndTechnique(g_threadState.technique);
			UpdateMaximum(
				g_counters.maxDrawsPerTechnique,
				drawCount);
			g_counters.completedTechniques.fetch_add(
				1,
				std::memory_order_relaxed);
			if (drawCount == 0) {
				g_counters.zeroDrawTechniques.fetch_add(
					1,
					std::memory_order_relaxed);
			} else if (drawCount > 1) {
				g_counters.multiDrawTechniques.fetch_add(
					1,
					std::memory_order_relaxed);
			}
			g_threadState.shader = nullptr;
		}

		void ObserveShader(
			ShaderStage,
			const sha1::Sha1Result& a_stockSha1,
			ID3D11DeviceChild* a_finalOutput) noexcept
		{
			if (!Enabled() || !a_finalOutput)
				return;

			StockShaderIdentity identity{ a_stockSha1, 0 };
			UINT size = sizeof(StockShaderIdentity);
			StockShaderIdentity existing;
			if (SUCCEEDED(a_finalOutput->GetPrivateData(
					kStockShaderIdentityGuid,
					&size,
					&existing))
				&& size == sizeof(StockShaderIdentity)
				&& (existing.ambiguous != 0
					|| existing.sha1.bytes != a_stockSha1.bytes)) {
				identity.ambiguous = 1;
				g_counters.ambiguousShaderIdentities.fetch_add(
					1,
					std::memory_order_relaxed);
			}
			(void)a_finalOutput->SetPrivateData(
				kStockShaderIdentityGuid,
				sizeof(StockShaderIdentity),
				&identity);
		}

		struct IdentityText
		{
			std::string value = "<unknown>";
			bool found = false;
		};

		IdentityText ReadIdentity(ID3D11DeviceChild* a_shader) noexcept
		{
			if (!a_shader)
				return {};

			StockShaderIdentity identity;
			UINT size = sizeof(StockShaderIdentity);
			if (FAILED(a_shader->GetPrivateData(
					kStockShaderIdentityGuid,
					&size,
					&identity))
				|| size != sizeof(StockShaderIdentity)) {
				return {};
			}
			if (identity.ambiguous != 0) {
				return { "<ambiguous>", true };
			}
			return { sha1::Sha1ToHex(identity.sha1), true };
		}
	}

	bool InstallShaderTracking()
	{
		if (!Enabled())
			return false;
		const auto stages = ShaderStageBit(ShaderStage::kVertex)
			| ShaderStageBit(ShaderStage::kPixel);
		const bool installed = RegisterShaderSwapObserver(
			&ObserveShader,
			stages);
		g_counters.shaderTrackingInstalled.store(
			installed,
			std::memory_order_relaxed);
		if (!installed) {
			L->error(
				"Prepass stock shader identity tracking registration failed.");
		}
		return installed;
	}

	void SetHooksInstalled(bool a_installed) noexcept
	{
		g_counters.hooksInstalled.store(
			a_installed,
			std::memory_order_relaxed);
	}

	void OnSetupTechnique(
		RE::BSShader* a_shader,
		std::uint32_t a_rawTechnique,
		bool a_succeeded) noexcept
	{
		if (!Enabled())
			return;
		if (!a_succeeded) {
			if (g_threadState.shader == a_shader)
				CompleteTechnique();
			return;
		}

		const auto serial = g_nextTechniqueSerial.fetch_add(
			1,
			std::memory_order_relaxed) + 1;
		if (g_threadState.technique.active) {
			g_counters.setupReentries.fetch_add(
				1,
				std::memory_order_relaxed);
			CompleteTechnique();
		}
		(void)BeginTechnique(
			g_threadState.technique,
			a_rawTechnique,
			serial);
		g_threadState.shader = a_shader;
		g_counters.setupTechniques.fetch_add(
			1,
			std::memory_order_relaxed);
	}

	void OnRestoreTechnique(RE::BSShader* a_shader) noexcept
	{
		if (!Enabled() || g_threadState.shader != a_shader)
			return;
		CompleteTechnique();
	}

	void OnSetupGeometry(
		RE::BSShader* a_shader,
		RE::BSRenderPass* a_pass) noexcept
	{
		if (!Enabled())
			return;
		if (g_threadState.shader != a_shader) {
			g_counters.missingTechniqueDraws.fetch_add(
				1,
				std::memory_order_relaxed);
			return;
		}
		const auto correlation = RecordDraw(g_threadState.technique);
		if (!correlation) {
			g_counters.missingTechniqueDraws.fetch_add(
				1,
				std::memory_order_relaxed);
			return;
		}

		const auto drawSerial = g_counters.draws.fetch_add(
			1,
			std::memory_order_relaxed) + 1;
		if (correlation->objectLod) {
			g_counters.objectLodDraws.fetch_add(
				1,
				std::memory_order_relaxed);
			g_counters.lastObjectLodFrame.store(
				telemetry::CurrentFrame(),
				std::memory_order_relaxed);
		} else {
			g_counters.bitClearDraws.fetch_add(
				1,
				std::memory_order_relaxed);
		}

		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* context = rendererData
			? reinterpret_cast<ID3D11DeviceContext*>(rendererData->context)
			: nullptr;
		winrt::com_ptr<ID3D11VertexShader> vertexShader;
		winrt::com_ptr<ID3D11PixelShader> pixelShader;
		if (context) {
			context->VSGetShader(vertexShader.put(), nullptr, nullptr);
			context->PSGetShader(pixelShader.put(), nullptr, nullptr);
		}
		const auto vertexIdentity = ReadIdentity(vertexShader.get());
		const auto pixelIdentity = ReadIdentity(pixelShader.get());
		if (!vertexIdentity.found) {
			g_counters.missingVertexIdentities.fetch_add(
				1,
				std::memory_order_relaxed);
		}
		if (!pixelIdentity.found) {
			g_counters.missingPixelIdentities.fetch_add(
				1,
				std::memory_order_relaxed);
		}

		const auto frame = telemetry::CurrentFrame();
		const auto passAddress = reinterpret_cast<std::uintptr_t>(a_pass);
		const auto geometryAddress = a_pass
			? reinterpret_cast<std::uintptr_t>(a_pass->geometry)
			: 0;
		const auto passEnum = a_pass ? a_pass->passEnum : 0;
		const auto lodMode = a_pass
			? static_cast<std::int32_t>(a_pass->lodMode)
			: 0;
		const auto marker = std::format(
			"FO4CS.Prepass draw={} tech={} tech_draw={} raw=0x{:08X} lod={} vs={} ps={}",
			drawSerial,
			correlation->techniqueSerial,
			correlation->techniqueDraw,
			correlation->rawTechnique,
			correlation->objectLod ? 1 : 0,
			vertexIdentity.value,
			pixelIdentity.value);
		render::annotation::SetMarker(marker);

		L->debug(
			"frame={} draw={} technique={} technique_draw={} raw=0x{:08X} "
			"object_lod={} pass=0x{:X} geometry=0x{:X} pass_enum=0x{:08X} "
			"lod_mode={} vs_stock_sha1={} ps_stock_sha1={}",
			frame,
			drawSerial,
			correlation->techniqueSerial,
			correlation->techniqueDraw,
			correlation->rawTechnique,
			correlation->objectLod,
			passAddress,
			geometryAddress,
			passEnum,
			lodMode,
			vertexIdentity.value,
			pixelIdentity.value);
	}

	Snapshot GetSnapshot() noexcept
	{
		return {
			.setupTechniques =
				g_counters.setupTechniques.load(std::memory_order_relaxed),
			.completedTechniques =
				g_counters.completedTechniques.load(std::memory_order_relaxed),
			.zeroDrawTechniques =
				g_counters.zeroDrawTechniques.load(std::memory_order_relaxed),
			.multiDrawTechniques =
				g_counters.multiDrawTechniques.load(std::memory_order_relaxed),
			.draws = g_counters.draws.load(std::memory_order_relaxed),
			.objectLodDraws =
				g_counters.objectLodDraws.load(std::memory_order_relaxed),
			.bitClearDraws =
				g_counters.bitClearDraws.load(std::memory_order_relaxed),
			.setupReentries =
				g_counters.setupReentries.load(std::memory_order_relaxed),
			.missingTechniqueDraws =
				g_counters.missingTechniqueDraws.load(std::memory_order_relaxed),
			.missingVertexIdentities =
				g_counters.missingVertexIdentities.load(std::memory_order_relaxed),
			.missingPixelIdentities =
				g_counters.missingPixelIdentities.load(std::memory_order_relaxed),
			.ambiguousShaderIdentities =
				g_counters.ambiguousShaderIdentities.load(
					std::memory_order_relaxed),
			.maxDrawsPerTechnique =
				g_counters.maxDrawsPerTechnique.load(std::memory_order_relaxed),
			.lastObjectLodFrame =
				g_counters.lastObjectLodFrame.load(std::memory_order_relaxed),
			.enabled = Enabled(),
			.hooksInstalled =
				g_counters.hooksInstalled.load(std::memory_order_relaxed),
			.shaderTrackingInstalled =
				g_counters.shaderTrackingInstalled.load(
					std::memory_order_relaxed)
		};
	}
}

#include "Render/DxbcPatchArtifact.h"
#include "SssDxbcPatchConsumer.h"
#include "SssDxbcPatchRuntime.h"
#include "SssInjectionMode.h"
#include "SssMaskBinding.h"
#include "Utils/CSSha1.h"
#include "Utils/CSSha256.h"

#include <nlohmann/json.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using namespace cs;

	struct Failure : std::runtime_error
	{
		using std::runtime_error::runtime_error;
	};

	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition)
			throw Failure(std::string(a_message));
	}

	struct SnapshotRaceFixture
	{
		features::sss_dxbc_patch::SnapshotTestPoint point;
		std::atomic_bool readerPaused{ false };
		std::atomic_bool writerDone{ false };
	};

	SnapshotRaceFixture* g_snapshotRaceFixture = nullptr;

	void PauseSnapshot(
		features::sss_dxbc_patch::SnapshotTestPoint a_point) noexcept
	{
		if (!g_snapshotRaceFixture
			|| a_point != g_snapshotRaceFixture->point
			|| g_snapshotRaceFixture->readerPaused.exchange(
				true,
				std::memory_order_acq_rel)) {
			return;
		}
		g_snapshotRaceFixture->readerPaused.notify_one();
		g_snapshotRaceFixture->writerDone.wait(
			false,
			std::memory_order_acquire);
	}

	features::sss_dxbc_patch::TelemetrySnapshot
		SnapshotDuringConcurrentPublish(
			features::sss_dxbc_patch::SnapshotTestPoint a_point,
			const features::sss_dxbc_patch::CounterDelta& a_delta)
	{
		features::sss_dxbc_patch::AtomicCounters counters;
		SnapshotRaceFixture fixture{ .point = a_point };
		g_snapshotRaceFixture = &fixture;
		features::sss_dxbc_patch::SetSnapshotTestHook(&PauseSnapshot);
		std::thread writer([&] {
			fixture.readerPaused.wait(
				false,
				std::memory_order_acquire);
			counters.Publish(a_delta);
			fixture.writerDone.store(true, std::memory_order_release);
			fixture.writerDone.notify_one();
		});

		const auto snapshot =
			features::sss_dxbc_patch::SnapshotCounters(counters);
		writer.join();
		features::sss_dxbc_patch::SetSnapshotTestHook(nullptr);
		g_snapshotRaceFixture = nullptr;
		return snapshot;
	}

	std::string ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		Check(input.is_open(), "could not open test input");
		return {
			std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>()
		};
	}

	std::string Canonical(Json a_document)
	{
		auto result = a_document.dump();
		result.push_back('\n');
		return result;
	}

	void CheckRejected(
		std::string_view a_bytes,
		std::string_view a_message)
	{
		Check(!engine::dxbc_patch::ParseArtifact(a_bytes), a_message);
	}

	void WriteU16(
		std::span<std::byte> a_bytes,
		std::size_t a_offset,
		std::uint16_t a_value)
	{
		a_bytes[a_offset] = static_cast<std::byte>(a_value & 0xFF);
		a_bytes[a_offset + 1] =
			static_cast<std::byte>((a_value >> 8) & 0xFF);
	}

	void WriteU32(
		std::span<std::byte> a_bytes,
		std::size_t a_offset,
		std::uint32_t a_value)
	{
		for (unsigned shift = 0; shift < 32; shift += 8) {
			a_bytes[a_offset + shift / 8] =
				static_cast<std::byte>((a_value >> shift) & 0xFF);
		}
	}

	std::vector<std::byte> DwordBytes(std::uint32_t a_value)
	{
		std::vector<std::byte> result(4);
		WriteU32(result, 0, a_value);
		return result;
	}

	std::vector<std::byte> MakeSyntheticDxbc()
	{
		constexpr std::size_t kExecutableOffset = 36;
		constexpr std::uint32_t kDwordCount = 16;
		constexpr std::size_t kPayloadSize =
			kDwordCount * sizeof(std::uint32_t);
		std::vector<std::byte> bytes(
			kExecutableOffset + 8 + kPayloadSize);
		std::memcpy(bytes.data(), "DXBC", 4);
		WriteU16(bytes, 20, 1);
		WriteU16(bytes, 22, 0);
		WriteU32(bytes, 24, static_cast<std::uint32_t>(bytes.size()));
		WriteU32(bytes, 28, 1);
		WriteU32(bytes, 32, kExecutableOffset);
		std::memcpy(bytes.data() + kExecutableOffset, "SHEX", 4);
		WriteU32(bytes, kExecutableOffset + 4, kPayloadSize);
		WriteU32(bytes, kExecutableOffset + 8, 0x00000050);
		WriteU32(bytes, kExecutableOffset + 12, kDwordCount);
		for (std::uint32_t index = 2; index < kDwordCount; ++index) {
			WriteU32(
				bytes,
				kExecutableOffset + 8 + index * 4,
				0x01000000u | index);
		}
		Check(
			engine::dxbc_patch::RewriteDxbcChecksum(bytes),
			"synthetic DXBC checksum could not be written");
		Check(
			engine::dxbc_patch::ValidateDxbcChecksum(bytes),
			"synthetic DXBC checksum did not validate");
		return bytes;
	}

	engine::dxbc_patch::Plan MakeSyntheticPlan(
		std::span<const std::byte> a_stock)
	{
		engine::dxbc_patch::Plan plan;
		plan.recipeId = "test";
		plan.target = engine::dxbc_patch::Target::kDirectional;
		plan.stock.length = a_stock.size();
		sha1::Sha1InitOnce();
		plan.stock.sha1 = sha1::Sha1Compute(
			a_stock.data(),
			a_stock.size());
		plan.stock.sha256 = sha256::Sha256Compute(
			a_stock.data(),
			a_stock.size());
		plan.executableChunkOffset = 36;
		plan.declaration.executableDword = 4;
		plan.declaration.fileOffset = 36 + 8 + 4 * 4;
		plan.declaration.bytes = DwordBytes(0xAABBCCDD);
		plan.declaration.preimage.offset =
			plan.declaration.fileOffset - 8;
		plan.declaration.preimage.length = 8;
		plan.declaration.preimage.sha256 = sha256::Sha256Compute(
			a_stock.data() + plan.declaration.preimage.offset,
			plan.declaration.preimage.length);
		plan.code.executableDword = 12;
		plan.code.fileOffset = 36 + 8 + 12 * 4;
		plan.code.bytes = DwordBytes(0x11223344);
		plan.code.preimage.offset = plan.code.fileOffset - 8;
		plan.code.preimage.length = 8;
		plan.code.preimage.sha256 = sha256::Sha256Compute(
			a_stock.data() + plan.code.preimage.offset,
			plan.code.preimage.length);
		plan.expectedLength =
			a_stock.size()
			+ plan.declaration.bytes.size()
			+ plan.code.bytes.size();
		return plan;
	}

	engine::dxbc_patch::Plan FinalizeSyntheticPlan(
		std::span<const std::byte> a_stock)
	{
		auto plan = MakeSyntheticPlan(a_stock);
		auto first =
			engine::dxbc_patch::BuildPatchedDxbc(a_stock, plan);
		Check(
			first.status
				== engine::dxbc_patch::PatchBuildStatus::kOutputMismatch,
			"unfinalized synthetic plan did not reach output validation");
		Check(
			first.bytecode.size() == plan.expectedLength,
			"unfinalized synthetic patch did not expose exact bytes");
		for (std::size_t index = 0;
			index < plan.expectedChecksum.size();
			++index) {
			plan.expectedChecksum[index] =
				std::to_integer<std::uint8_t>(
					first.bytecode[4 + index]);
		}
		plan.expectedSha1 = sha1::Sha1Compute(
			first.bytecode.data(),
			first.bytecode.size());
		plan.expectedSha256 = sha256::Sha256Compute(
			first.bytecode.data(),
			first.bytecode.size());
		return plan;
	}

	engine::dxbc_patch::Artifact MakeSyntheticArtifact(
		std::span<const std::byte> a_stock,
		const engine::dxbc_patch::Plan& a_plan)
	{
		engine::dxbc_patch::Artifact artifact;
		artifact.status = "provisional";
		artifact.releaseScope = "architecture-proof-dev-only";
		artifact.coverage = {
			.candidates = 1,
			.identities = 1,
			.pass = 1
		};
		artifact.plans.push_back(a_plan);
		artifact.routes.push_back({
			.variant = {
				"BSDFLightShader",
				engine::ShaderStage::kPixel,
				engine::ShaderVariantId{ 0x01200202 }
			},
			.stock = {
				.length = a_stock.size(),
				.sha1 = a_plan.stock.sha1,
				.sha256 = a_plan.stock.sha256
			},
			.status = engine::dxbc_patch::CandidateStatus::kPass,
			.planIndex = 0
		});
		return artifact;
	}

	class FakePixelShader : public ID3D11PixelShader
	{
	public:
		bool rejectPrivateData = false;
		bool malformedPrivateData = false;
		ULONG references = 1;

		HRESULT STDMETHODCALLTYPE QueryInterface(
			REFIID,
			void**) override
		{
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return ++references;
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			if (references != 0)
				--references;
			return references;
		}

		void STDMETHODCALLTYPE GetDevice(
			ID3D11Device** a_device) override
		{
			if (a_device)
				*a_device = nullptr;
		}

		HRESULT STDMETHODCALLTYPE GetPrivateData(
			REFGUID a_guid,
			UINT* a_size,
			void* a_data) override
		{
			const std::vector<std::uint8_t>* privateData = nullptr;
			const bool identityGuid = InlineIsEqualGUID(
				a_guid,
				features::sss_dxbc_patch::
					kPatchedPixelShaderIdentityGuid);
			if (identityGuid) {
				privateData = &_identityData;
			} else if (InlineIsEqualGUID(
					a_guid,
					features::sss_dxbc_patch::
						kPatchedPixelShaderStockBytecodeGuid)) {
				privateData = &_stockBytecode;
			}
			if (!a_size || !privateData || privateData->empty()) {
				return DXGI_ERROR_NOT_FOUND;
			}
			const auto required =
				identityGuid && malformedPrivateData
				? static_cast<UINT>(privateData->size() - 1)
				: static_cast<UINT>(privateData->size());
			if (!a_data || *a_size < required) {
				*a_size = required;
				return DXGI_ERROR_MORE_DATA;
			}
			std::memcpy(a_data, privateData->data(), required);
			*a_size = required;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(
			REFGUID a_guid,
			UINT a_size,
			const void* a_data) override
		{
			if (rejectPrivateData)
				return E_FAIL;
			if (!a_data
				|| a_size == 0) {
				return E_INVALIDARG;
			}
			const auto* first =
				static_cast<const std::uint8_t*>(a_data);
			if (InlineIsEqualGUID(
					a_guid,
					features::sss_dxbc_patch::
						kPatchedPixelShaderIdentityGuid)) {
				_identityData.assign(first, first + a_size);
			} else if (InlineIsEqualGUID(
					a_guid,
					features::sss_dxbc_patch::
						kPatchedPixelShaderStockBytecodeGuid)) {
				_stockBytecode.assign(first, first + a_size);
			} else {
				return E_INVALIDARG;
			}
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
			REFGUID,
			const IUnknown*) override
		{
			return E_NOTIMPL;
		}

	private:
		std::vector<std::uint8_t> _identityData;
		std::vector<std::uint8_t> _stockBytecode;
	};

	struct CreateFixture
	{
		ID3D11Device* expectedDevice = nullptr;
		ID3D11ClassLinkage* expectedLinkage = nullptr;
		FakePixelShader* shader = nullptr;
		HRESULT result = S_OK;
		bool called = false;
		bool bypassActive = false;
		bool forwarded = false;
	};

	CreateFixture* g_createFixture = nullptr;

	struct ShaderBindFixture
	{
		ID3D11PixelShader* bound = nullptr;

		~ShaderBindFixture()
		{
			if (bound)
				bound->Release();
		}
	};

	HRESULT CreatePatchedShader(
		ID3D11Device* a_device,
		const void*,
		SIZE_T,
		ID3D11ClassLinkage* a_linkage,
		ID3D11PixelShader** a_output) noexcept
	{
		auto& fixture = *g_createFixture;
		fixture.called = true;
		fixture.bypassActive =
			engine::PixelShaderBrokerBypassActive();
		fixture.forwarded =
			a_device == fixture.expectedDevice
			&& a_linkage == fixture.expectedLinkage;
		if (a_output)
			*a_output = fixture.shader;
		return fixture.result;
	}

	void BindShader(
		void* a_context,
		ID3D11PixelShader* a_shader) noexcept
	{
		auto& fixture = *static_cast<ShaderBindFixture*>(a_context);
		if (a_shader)
			a_shader->AddRef();
		if (fixture.bound)
			fixture.bound->Release();
		fixture.bound = a_shader;
	}

	class FakeSrv : public ID3D11ShaderResourceView
	{
	public:
		ULONG references = 1;

		HRESULT STDMETHODCALLTYPE QueryInterface(
			REFIID,
			void**) override
		{
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return ++references;
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			if (references != 0)
				--references;
			return references;
		}

		void STDMETHODCALLTYPE GetDevice(
			ID3D11Device** a_device) override
		{
			if (a_device)
				*a_device = nullptr;
		}

		HRESULT STDMETHODCALLTYPE GetPrivateData(
			REFGUID,
			UINT*,
			void*) override
		{
			return DXGI_ERROR_NOT_FOUND;
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(
			REFGUID,
			UINT,
			const void*) override
		{
			return E_NOTIMPL;
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
			REFGUID,
			const IUnknown*) override
		{
			return E_NOTIMPL;
		}

		void STDMETHODCALLTYPE GetResource(
			ID3D11Resource** a_resource) override
		{
			if (a_resource)
				*a_resource = nullptr;
		}

		void STDMETHODCALLTYPE GetDesc(
			D3D11_SHADER_RESOURCE_VIEW_DESC* a_desc) override
		{
			if (a_desc)
				*a_desc = {};
		}
	};

	struct BindingFixture
	{
		ID3D11ShaderResourceView* bound = nullptr;
		bool rejectSet = false;

		~BindingFixture()
		{
			if (bound)
				bound->Release();
		}

		void Assign(ID3D11ShaderResourceView* a_view)
		{
			if (a_view)
				a_view->AddRef();
			if (bound)
				bound->Release();
			bound = a_view;
		}
	};

	void GetBinding(
		void* a_context,
		std::uint32_t,
		ID3D11ShaderResourceView** a_view) noexcept
	{
		auto& fixture = *static_cast<BindingFixture*>(a_context);
		*a_view = fixture.bound;
		if (*a_view)
			(*a_view)->AddRef();
	}

	void SetBinding(
		void* a_context,
		std::uint32_t,
		ID3D11ShaderResourceView* a_view) noexcept
	{
		auto& fixture = *static_cast<BindingFixture*>(a_context);
		fixture.Assign(fixture.rejectSet ? nullptr : a_view);
	}

	features::sss_mask_binding::Api BindingApi(
		BindingFixture& a_fixture)
	{
		return {
			.context = &a_fixture,
			.get = &GetBinding,
			.set = &SetBinding
		};
	}

	void TestPackageArtifact(
		const std::filesystem::path& a_artifactPath,
		const std::filesystem::path& a_configPath)
	{
		const auto bytes = ReadFile(a_artifactPath);
		Check(bytes.size() == 53601, "package artifact size changed");
		const auto digest =
			sha256::Sha256Compute(bytes.data(), bytes.size());
		Check(
			sha256::Sha256ToHex(digest)
				== "eaa508727cd08ed20b9d3c5db823eebe28e23b78f948e179dab84ae9217d8483",
			"package artifact digest changed");
		const auto parsed = engine::dxbc_patch::ParseArtifact(bytes);
		Check(static_cast<bool>(parsed), parsed.error);
		Check(
			parsed.artifact->coverage.pass == 2
				&& parsed.artifact->coverage.candidates == 64
				&& parsed.artifact->coverage.unproven == 62,
			"package artifact did not report provisional 2/64 coverage");

		sha256::Sha256Result expected{};
		Check(
			sha256::Sha256FromHex(
				"eaa508727cd08ed20b9d3c5db823eebe28e23b78f948e179dab84ae9217d8483",
				expected),
			"expected artifact digest fixture is invalid");
		Check(
			static_cast<bool>(
				engine::dxbc_patch::LoadPinnedArtifact(
					a_artifactPath,
					53601,
					expected)),
			"pinned package artifact did not load");
		expected.bytes[0] ^= 0xFF;
		Check(
			!engine::dxbc_patch::LoadPinnedArtifact(
				a_artifactPath,
				53601,
				expected),
			"wrong package digest was accepted");

		const auto config = ReadFile(a_configPath);
		Check(
			config.find("injection_mode = \"stock\"")
				!= std::string::npos,
			"shipped SSS injection mode is not explicitly stock");
		Check(
			features::SssInjectionModeName(
				features::SssInjectionMode::kStock)
				== "stock"
				&& features::ParseSssInjectionMode("dxbc_patch")
					== features::SssInjectionMode::kDxbcPatch
				&& !features::ParseSssInjectionMode("beta")
				&& features::SssPatchStatusName(true)
					== "provisional"
				&& features::SssPatchStatusName(false)
					== "unavailable",
			"SSS injection mode parser is not fail-closed");
		const auto missingArtifactStartup =
			features::DecideSssStartup(
				features::SssInjectionMode::kDxbcPatch,
				false);
		const auto stockStartup =
			features::DecideSssStartup(
				features::SssInjectionMode::kStock,
				false);
		Check(
			missingArtifactStartup.runLifecycle
				&& !missingArtifactStartup.injectionReady
				&& missingArtifactStartup.routeFallsBackToStock
				&& !stockStartup.runLifecycle,
			"artifact failure incorrectly disabled the Bend lifecycle");
	}

	void TestArtifactRejections(
		const std::filesystem::path& a_artifactPath)
	{
		const auto bytes = ReadFile(a_artifactPath);
		auto malformed = bytes;
		malformed.pop_back();
		CheckRejected(malformed, "malformed artifact was accepted");

		auto duplicate = bytes;
		const auto closing = duplicate.rfind('}');
		Check(closing != std::string::npos, "artifact fixture has no root close");
		duplicate.insert(closing, ",\"status\":\"provisional\"");
		CheckRejected(duplicate, "duplicate artifact field was accepted");

		const auto document = Json::parse(bytes);
		auto extra = document;
		extra["machine_path"] = "C:\\private\\native.dxbc";
		CheckRejected(Canonical(extra), "machine path field was accepted");

		auto nativeBytes = document;
		nativeBytes["plans"][0]["declaration_insertion"]
			["preimage"]["native_bytes"] = "00";
		CheckRejected(Canonical(nativeBytes), "native byte field was accepted");

		auto unsortedCandidates = document;
		std::swap(
			unsortedCandidates["candidates"][0],
			unsortedCandidates["candidates"][1]);
		CheckRejected(
			Canonical(unsortedCandidates),
			"unsorted candidates were accepted");

		auto unsortedRoutes = document;
		std::swap(
			unsortedRoutes["candidates"][0]["identities"][0],
			unsortedRoutes["candidates"][0]["identities"][1]);
		CheckRejected(
			Canonical(unsortedRoutes),
			"unsorted candidate routes were accepted");

		auto unsortedPlans = document;
		std::swap(
			unsortedPlans["plans"][0],
			unsortedPlans["plans"][1]);
		CheckRejected(
			Canonical(unsortedPlans),
			"unsorted plans were accepted");

		auto conflict = document;
		conflict["candidates"][1]["identities"][0]["opaque_psid"] =
			conflict["candidates"][0]["identities"][0]["opaque_psid"];
		CheckRejected(
			Canonical(conflict),
			"conflicting route tuple was accepted");

		auto outOfRange = document;
		outOfRange["plans"][0]["declaration_insertion"]
			["preimage"]["offset"] = 999999999;
		CheckRejected(
			Canonical(outOfRange),
			"out-of-range preimage was accepted");

		auto overlap = document;
		overlap["plans"][0]["code_insertion"]["preimage"] =
			overlap["plans"][0]["declaration_insertion"]["preimage"];
		CheckRejected(
			Canonical(overlap),
			"overlapping preimages were accepted");

		auto unknownStage = document;
		unknownStage["candidates"][0]["identities"][0]["stage"] =
			"compute";
		CheckRejected(
			Canonical(unknownStage),
			"unsupported stage was accepted");

		auto unknownRelease = document;
		unknownRelease["engine_scope"]["release"] =
			"fallout4-ng-1.10.984.0";
		CheckRejected(
			Canonical(unknownRelease),
			"unsupported runtime was accepted");
	}

	void TestExactIdentitySelection(
		const std::filesystem::path& a_artifactPath)
	{
		const auto parsed =
			engine::dxbc_patch::ParseArtifact(ReadFile(a_artifactPath));
		Check(static_cast<bool>(parsed), parsed.error);
		const auto& artifact = *parsed.artifact;
		const auto route = std::ranges::find_if(
			artifact.routes,
			[](const engine::dxbc_patch::CandidateRoute& a_route) {
				return a_route.status
					== engine::dxbc_patch::CandidateStatus::kPass;
			});
		Check(route != artifact.routes.end(), "artifact has no PASS route");
		Check(
			engine::dxbc_patch::FindCandidateRoute(
				artifact,
				engine::ViewShaderVariantKey(route->variant))
				== &*route,
			"exact route was not selected");

		auto wrongVariant = engine::ViewShaderVariantKey(route->variant);
		wrongVariant.subclass = "BSDFCompositeShader";
		Check(
			!engine::dxbc_patch::FindCandidateRoute(
				artifact,
				wrongVariant),
			"wrong subclass was admitted");
		wrongVariant = engine::ViewShaderVariantKey(route->variant);
		wrongVariant.stage = engine::ShaderStage::kVertex;
		Check(
			!engine::dxbc_patch::FindCandidateRoute(
				artifact,
				wrongVariant),
			"wrong stage was admitted");
		wrongVariant = engine::ViewShaderVariantKey(route->variant);
		wrongVariant.id = engine::ShaderVariantId{
			wrongVariant.id.Value() ^ 1
		};
		Check(
			!engine::dxbc_patch::FindCandidateRoute(
				artifact,
				wrongVariant),
			"wrong PSID or hash-only fallback was admitted");

		auto runtime = artifact.runtime;
		Check(
			engine::dxbc_patch::RuntimeMatches(artifact, runtime),
			"exact runtime was rejected");
		auto wrongRuntime = runtime;
		wrongRuntime.release = "fallout4-ng-1.10.984.0";
		Check(
			!engine::dxbc_patch::RuntimeMatches(
				artifact,
				wrongRuntime),
			"wrong release was admitted");
		wrongRuntime = runtime;
		wrongRuntime.executableVersion = "1.11.221.1";
		Check(
			!engine::dxbc_patch::RuntimeMatches(
				artifact,
				wrongRuntime),
			"wrong executable version was admitted");
		wrongRuntime = runtime;
		wrongRuntime.executableLength += 1;
		Check(
			!engine::dxbc_patch::RuntimeMatches(
				artifact,
				wrongRuntime),
			"wrong executable length was admitted");
		wrongRuntime = runtime;
		wrongRuntime.executableSha256.bytes[0] ^= 1;
		Check(
			!engine::dxbc_patch::RuntimeMatches(
				artifact,
				wrongRuntime),
			"wrong executable hash was admitted");
		wrongRuntime = runtime;
		wrongRuntime.engineContractScope += ".other";
		Check(
			!engine::dxbc_patch::RuntimeMatches(
				artifact,
				wrongRuntime),
			"wrong engine contract scope was admitted");

		Check(
			engine::dxbc_patch::StockMatches(
				route->stock,
				route->stock.length,
				route->stock.sha1,
				route->stock.sha256),
			"exact stock identity was rejected");
		auto wrongSha1 = route->stock.sha1;
		wrongSha1.bytes[0] ^= 1;
		Check(
			!engine::dxbc_patch::StockMatches(
				route->stock,
				route->stock.length,
				wrongSha1,
				route->stock.sha256),
			"wrong stock SHA-1 was admitted");
		auto wrongSha256 = route->stock.sha256;
		wrongSha256.bytes[0] ^= 1;
		Check(
			!engine::dxbc_patch::StockMatches(
				route->stock,
				route->stock.length,
				route->stock.sha1,
				wrongSha256),
			"wrong stock SHA-256 was admitted");
		Check(
			!engine::dxbc_patch::StockMatches(
				route->stock,
				route->stock.length + 1,
				route->stock.sha1,
				route->stock.sha256),
			"wrong stock length was admitted");
	}

	void TestPatchBuilder()
	{
		const auto stock = MakeSyntheticDxbc();
		const auto plan = FinalizeSyntheticPlan(stock);
		const auto built =
			engine::dxbc_patch::BuildPatchedDxbc(stock, plan);
		Check(
			built.status
					== engine::dxbc_patch::PatchBuildStatus::kSuccess
				&& built.bytecode.size() == plan.expectedLength,
			"valid bounded patch did not build");
		Check(
			engine::dxbc_patch::ValidateDxbcChecksum(built.bytecode),
			"patched DXBC checksum did not validate");

		auto preimageMismatch = plan;
		preimageMismatch.declaration.preimage.sha256.bytes[0] ^= 1;
		Check(
			engine::dxbc_patch::BuildPatchedDxbc(
				stock,
				preimageMismatch).status
				== engine::dxbc_patch::PatchBuildStatus::
					kPreimageMismatch,
			"preimage mismatch was not rejected");

		auto outputMismatch = plan;
		outputMismatch.expectedSha256.bytes[0] ^= 1;
		Check(
			engine::dxbc_patch::BuildPatchedDxbc(
				stock,
				outputMismatch).status
				== engine::dxbc_patch::PatchBuildStatus::
					kOutputMismatch,
			"patched hash mismatch was not rejected");

		auto checksumMismatch = plan;
		checksumMismatch.expectedChecksum[0] ^= 1;
		Check(
			engine::dxbc_patch::BuildPatchedDxbc(
				stock,
				checksumMismatch).status
				== engine::dxbc_patch::PatchBuildStatus::
					kOutputMismatch,
			"patched checksum mismatch was not rejected");

		auto malformedStock = stock;
		malformedStock[0] = std::byte{ 'N' };
		auto malformedPlan = plan;
		malformedPlan.stock.sha1 = sha1::Sha1Compute(
			malformedStock.data(),
			malformedStock.size());
		malformedPlan.stock.sha256 = sha256::Sha256Compute(
			malformedStock.data(),
			malformedStock.size());
		Check(
			engine::dxbc_patch::BuildPatchedDxbc(
				malformedStock,
				malformedPlan).status
				== engine::dxbc_patch::PatchBuildStatus::
					kMalformedDxbc,
			"malformed DXBC container was accepted");
	}

	void TestDxbcChecksumVectors()
	{
		const auto runVector = [](
			std::size_t a_length,
			const std::array<std::uint8_t, 16>& a_expected) {
				std::vector<std::byte> bytes(a_length);
				std::memcpy(bytes.data(), "DXBC", 4);
				for (std::size_t index = 20;
					index < bytes.size();
					++index) {
					bytes[index] = static_cast<std::byte>(
						(index * 37 + 11) & 0xFF);
				}
				const auto computed =
					engine::dxbc_patch::ComputeDxbcChecksum(bytes);
				Check(
					computed && *computed == a_expected,
					"independent DXBC checksum vector mismatched");
				for (std::size_t index = 0;
					index < a_expected.size();
					++index) {
					bytes[4 + index] =
						static_cast<std::byte>(a_expected[index]);
				}
				Check(
					engine::dxbc_patch::ValidateDxbcChecksum(bytes),
					"fixed DXBC checksum vector was not valid");
				bytes.back() ^= std::byte{ 1 };
				Check(
					!engine::dxbc_patch::ValidateDxbcChecksum(bytes),
					"one-bit DXBC checksum corruption was accepted");
			};
		runVector(
			52,
			{
				0xb8, 0x45, 0x30, 0x34,
				0x7b, 0x3c, 0x97, 0x2b,
				0x5a, 0x78, 0x2c, 0xb9,
				0x14, 0x14, 0x0c, 0x19
			});
		runVector(
			80,
			{
				0x6f, 0x52, 0x67, 0xd3,
				0x44, 0xa2, 0x04, 0x72,
				0x87, 0x52, 0x5f, 0xfa,
				0xc8, 0xd9, 0x69, 0x5f
			});
	}

	void TestPublicationTransaction()
	{
		const auto stockBytes = MakeSyntheticDxbc();
		const auto plan = FinalizeSyntheticPlan(stockBytes);
		const std::vector plans{ plan };
		auto* device = reinterpret_cast<ID3D11Device*>(0x1010);
		ID3D11ClassLinkage* linkage = nullptr;

		FakePixelShader stock;
		FakePixelShader patched;
		ID3D11PixelShader* output = &stock;
		CreateFixture create{
			.expectedDevice = device,
			.expectedLinkage = linkage,
			.shader = &patched
		};
		g_createFixture = &create;
		const engine::PixelShaderSwapRequest request{
			.device = device,
			.linkage = linkage,
			.bytecode = stockBytes.data(),
			.bytecodeLength = stockBytes.size(),
			.stockOutput = &stock,
			.output = &output
		};
		const auto status =
			features::sss_dxbc_patch::PublishPatchedPixelShader(
				request,
				plan,
				0,
				stockBytes,
				&CreatePatchedShader);
		Check(
			status
					== features::sss_dxbc_patch::PublishStatus::kAccepted
				&& output == &patched
				&& stock.references == 0
				&& patched.references == 1
				&& create.called
				&& create.forwarded
				&& create.bypassActive,
			"accepted patched shader transaction was not exact");
		Check(
			features::sss_dxbc_patch::MatchesPatchedPixelShader(
				output,
				engine::dxbc_patch::Target::kDirectional,
				plans),
			"published identity did not match its target");
		Check(
			!features::sss_dxbc_patch::MatchesPatchedPixelShader(
				output,
				engine::dxbc_patch::Target::kDirectionalIbl,
				plans),
			"published identity matched the wrong target");
		FakeSrv realMask;
		FakeSrv whiteMask;
		FakeSrv foreignMask;
		BindingFixture resourceBinding;
		resourceBinding.Assign(&foreignMask);
		const auto foreignResult =
			features::sss_mask_binding::Bind(
				BindingApi(resourceBinding),
				6,
				&realMask,
				{ 1920, 1080 },
				true,
				&whiteMask,
				{ 1920, 1080 },
				{ 1920, 1080 });
		features::sss_dxbc_patch::BindingReadinessLatch invariant;
		Check(
			invariant.Publish(true)
				&& foreignResult.foreignBindingDetected
				&& !foreignResult.validBinding
				&& resourceBinding.bound == &foreignMask,
			"foreign t6 did not fail closed before stock restoration");
		invariant.Break();
		Check(
			!invariant.Publish(true)
				&& invariant.IsBroken()
				&& !invariant.IsReady(),
			"foreign t6 violation did not latch patch admission broken");
		FakePixelShader restoredStock;
		create = {
			.expectedDevice = device,
			.expectedLinkage = nullptr,
			.shader = &restoredStock
		};
		g_createFixture = &create;
		ShaderBindFixture bind;
		Check(
			features::sss_dxbc_patch::RestoreStockPixelShader(
				output,
				device,
				plans,
				&CreatePatchedShader,
				&bind,
				&BindShader)
					== features::sss_dxbc_patch::RestoreStockStatus::
						kRestored
				&& bind.bound == &restoredStock
				&& restoredStock.references == 1
				&& create.called
				&& create.forwarded
				&& create.bypassActive
				&& resourceBinding.bound == &foreignMask,
			"foreign t6 draw did not restore stock while preserving the SRV");
		FakePixelShader failedRestoredStock;
		create = {
			.expectedDevice = device,
			.expectedLinkage = nullptr,
			.shader = &failedRestoredStock,
			.result = E_FAIL
		};
		g_createFixture = &create;
		ShaderBindFixture failedBind;
		BindShader(&failedBind, output);
		Check(
			features::sss_dxbc_patch::RestoreStockPixelShader(
				output,
				device,
				plans,
				&CreatePatchedShader,
				&failedBind,
				&BindShader)
					== features::sss_dxbc_patch::RestoreStockStatus::
						kCreateRejected
				&& failedBind.bound == nullptr
				&& failedRestoredStock.references == 0,
			"failed stock restoration left the patched shader bound");
		output->Release();

		FakePixelShader stockAfterTagFailure;
		FakePixelShader rejectedTag;
		rejectedTag.rejectPrivateData = true;
		output = &stockAfterTagFailure;
		create = {
			.expectedDevice = device,
			.expectedLinkage = linkage,
			.shader = &rejectedTag
		};
		const engine::PixelShaderSwapRequest tagFailureRequest{
			.device = device,
			.linkage = linkage,
			.bytecode = stockBytes.data(),
			.bytecodeLength = stockBytes.size(),
			.stockOutput = &stockAfterTagFailure,
			.output = &output
		};
		Check(
			features::sss_dxbc_patch::PublishPatchedPixelShader(
				tagFailureRequest,
				plan,
				0,
				stockBytes,
				&CreatePatchedShader)
					== features::sss_dxbc_patch::PublishStatus::
						kIdentityRejected
				&& output == &stockAfterTagFailure
				&& stockAfterTagFailure.references == 1
				&& rejectedTag.references == 0,
			"private-data failure did not retain stock");

		FakePixelShader stockAfterCreateFailure;
		FakePixelShader rejectedCreate;
		output = &stockAfterCreateFailure;
		create = {
			.expectedDevice = device,
			.expectedLinkage = linkage,
			.shader = &rejectedCreate,
			.result = E_FAIL
		};
		const engine::PixelShaderSwapRequest createFailureRequest{
			.device = device,
			.linkage = linkage,
			.bytecode = stockBytes.data(),
			.bytecodeLength = stockBytes.size(),
			.stockOutput = &stockAfterCreateFailure,
			.output = &output
		};
		Check(
			features::sss_dxbc_patch::PublishPatchedPixelShader(
				createFailureRequest,
				plan,
				0,
				stockBytes,
				&CreatePatchedShader)
					== features::sss_dxbc_patch::PublishStatus::
						kCreateRejected
				&& output == &stockAfterCreateFailure
				&& stockAfterCreateFailure.references == 1
				&& rejectedCreate.references == 0,
			"D3D rejection did not retain stock");

		FakePixelShader untagged;
		Check(
			!features::sss_dxbc_patch::MatchesPatchedPixelShader(
				&untagged,
				engine::dxbc_patch::Target::kDirectional,
				plans),
			"untagged recycled object matched a prior publication");
		auto malformedIdentity =
			features::sss_dxbc_patch::MakeIdentity(plan, 0);
		malformedIdentity.magic ^= 1;
		Check(
			SUCCEEDED(untagged.SetPrivateData(
				features::sss_dxbc_patch::
					kPatchedPixelShaderIdentityGuid,
				static_cast<UINT>(sizeof(malformedIdentity)),
				&malformedIdentity))
				&& !features::sss_dxbc_patch::
					MatchesPatchedPixelShader(
						&untagged,
						engine::dxbc_patch::Target::kDirectional,
						plans),
			"malformed private identity was accepted");
		untagged.malformedPrivateData = true;
		Check(
			!features::sss_dxbc_patch::MatchesPatchedPixelShader(
				&untagged,
				engine::dxbc_patch::Target::kDirectional,
				plans),
			"wrong-sized private identity was accepted");
	}

	void TestIntegratedResolver()
	{
		const auto stockBytes = MakeSyntheticDxbc();
		const auto plan = FinalizeSyntheticPlan(stockBytes);
		const auto artifact = MakeSyntheticArtifact(stockBytes, plan);
		auto* device = reinterpret_cast<ID3D11Device*>(
			const_cast<std::byte*>(stockBytes.data()));
		ID3D11ClassLinkage* linkage = nullptr;
		const auto variant =
			engine::ViewShaderVariantKey(artifact.routes[0].variant);

		{
			FakePixelShader stock;
			FakePixelShader patched;
			ID3D11PixelShader* output = &stock;
			CreateFixture create{
				.expectedDevice = device,
				.expectedLinkage = linkage,
				.shader = &patched
			};
			g_createFixture = &create;
			const engine::PixelShaderSwapRequest request{
				.device = device,
				.linkage = linkage,
				.bytecode = stockBytes.data(),
				.bytecodeLength = stockBytes.size(),
				.variant = variant,
				.stockSha1 = plan.stock.sha1,
				.stockOutput = &stock,
				.output = &output
			};
			features::sss_dxbc_patch::AtomicCounters counters;
			Check(
				features::sss_dxbc_patch::ResolveRequest(
					artifact,
					true,
					true,
					true,
					counters,
					request,
					&CreatePatchedShader)
						== engine::PixelShaderSwapResolverResult::kReplaced
					&& output == &patched,
				"integrated exact route did not publish a patch");
			const auto snapshot =
				features::sss_dxbc_patch::SnapshotCounters(counters);
			Check(
				snapshot.candidateSeen == 1
					&& snapshot.exactRouteAdmitted == 1
					&& snapshot.patchBuilt == 1
					&& snapshot.createPsAccepted == 1
					&& snapshot.createPsRejected == 0
					&& snapshot.identityPublished == 1
					&& features::sss_dxbc_patch::
						TelemetryRelationshipsHold(snapshot),
				"integrated accepted counters are inconsistent");
			output->Release();
		}
		{
			FakePixelShader stock;
			ID3D11PixelShader* output = &stock;
			CreateFixture create{
				.expectedDevice = device,
				.expectedLinkage = linkage
			};
			g_createFixture = &create;
			auto wrongSha1 = plan.stock.sha1;
			wrongSha1.bytes[0] ^= 1;
			const engine::PixelShaderSwapRequest request{
				.device = device,
				.linkage = linkage,
				.bytecode = stockBytes.data(),
				.bytecodeLength = stockBytes.size(),
				.variant = variant,
				.stockSha1 = wrongSha1,
				.stockOutput = &stock,
				.output = &output
			};
			features::sss_dxbc_patch::AtomicCounters counters;
			Check(
				features::sss_dxbc_patch::ResolveRequest(
					artifact,
					true,
					true,
					true,
					counters,
					request,
					&CreatePatchedShader)
						== engine::PixelShaderSwapResolverResult::kKeepStock
					&& output == &stock
					&& stock.references == 1
					&& !create.called
					&& features::sss_dxbc_patch::
						SnapshotCounters(counters).hashMismatch == 1,
				"integrated stock hash mismatch did not retain stock");
		}
		{
			auto preimageArtifact = artifact;
			preimageArtifact.plans[0]
				.declaration.preimage.sha256.bytes[0] ^= 1;
			FakePixelShader stock;
			ID3D11PixelShader* output = &stock;
			const engine::PixelShaderSwapRequest request{
				.device = device,
				.linkage = linkage,
				.bytecode = stockBytes.data(),
				.bytecodeLength = stockBytes.size(),
				.variant = variant,
				.stockSha1 = plan.stock.sha1,
				.stockOutput = &stock,
				.output = &output
			};
			features::sss_dxbc_patch::AtomicCounters counters;
			Check(
				features::sss_dxbc_patch::ResolveRequest(
					preimageArtifact,
					true,
					true,
					true,
					counters,
					request,
					&CreatePatchedShader)
						== engine::PixelShaderSwapResolverResult::kKeepStock
					&& output == &stock
					&& features::sss_dxbc_patch::
						SnapshotCounters(counters).preimageMismatch == 1,
				"integrated preimage mismatch did not retain stock");
		}
		{
			FakePixelShader stock;
			FakePixelShader rejectedTag;
			rejectedTag.rejectPrivateData = true;
			ID3D11PixelShader* output = &stock;
			CreateFixture create{
				.expectedDevice = device,
				.expectedLinkage = linkage,
				.shader = &rejectedTag
			};
			g_createFixture = &create;
			const engine::PixelShaderSwapRequest request{
				.device = device,
				.linkage = linkage,
				.bytecode = stockBytes.data(),
				.bytecodeLength = stockBytes.size(),
				.variant = variant,
				.stockSha1 = plan.stock.sha1,
				.stockOutput = &stock,
				.output = &output
			};
			features::sss_dxbc_patch::AtomicCounters counters;
			Check(
				features::sss_dxbc_patch::ResolveRequest(
					artifact,
					true,
					true,
					true,
					counters,
					request,
					&CreatePatchedShader)
						== engine::PixelShaderSwapResolverResult::kKeepStock
					&& output == &stock
					&& stock.references == 1
					&& rejectedTag.references == 0,
				"integrated tag rejection did not retain stock");
			const auto snapshot =
				features::sss_dxbc_patch::SnapshotCounters(counters);
			Check(
				snapshot.createPsAccepted == 1
					&& snapshot.identityPublished == 0,
				"integrated tag rejection counters are inconsistent");
		}
		{
			FakePixelShader stock;
			FakePixelShader rejectedCreate;
			ID3D11PixelShader* output = &stock;
			CreateFixture create{
				.expectedDevice = device,
				.expectedLinkage = linkage,
				.shader = &rejectedCreate,
				.result = E_FAIL
			};
			g_createFixture = &create;
			const engine::PixelShaderSwapRequest request{
				.device = device,
				.linkage = linkage,
				.bytecode = stockBytes.data(),
				.bytecodeLength = stockBytes.size(),
				.variant = variant,
				.stockSha1 = plan.stock.sha1,
				.stockOutput = &stock,
				.output = &output
			};
			features::sss_dxbc_patch::AtomicCounters counters;
			Check(
				features::sss_dxbc_patch::ResolveRequest(
					artifact,
					true,
					true,
					true,
					counters,
					request,
					&CreatePatchedShader)
						== engine::PixelShaderSwapResolverResult::kKeepStock
					&& output == &stock
					&& stock.references == 1
					&& rejectedCreate.references == 0
					&& features::sss_dxbc_patch::
						SnapshotCounters(counters).createPsRejected == 1,
				"integrated D3D rejection did not retain stock");
		}
		{
			FakePixelShader stock;
			ID3D11PixelShader* output = &stock;
			CreateFixture create{
				.expectedDevice = device,
				.expectedLinkage = linkage
			};
			g_createFixture = &create;
			const engine::PixelShaderSwapRequest request{
				.device = device,
				.linkage = linkage,
				.bytecode = stockBytes.data(),
				.bytecodeLength = stockBytes.size(),
				.variant = variant,
				.stockSha1 = plan.stock.sha1,
				.stockOutput = &stock,
				.output = &output
			};
			features::sss_dxbc_patch::AtomicCounters counters;
			Check(
				features::sss_dxbc_patch::ResolveRequest(
					artifact,
					true,
					true,
					false,
					counters,
					request,
					&CreatePatchedShader)
						== engine::PixelShaderSwapResolverResult::kKeepStock
					&& output == &stock
					&& stock.references == 1
					&& !create.called
					&& features::sss_dxbc_patch::
						SnapshotCounters(counters).unknownStockFallback == 1,
				"binding-not-ready route did not stay stock");
		}
		{
			FakePixelShader stock;
			ID3D11PixelShader* output = &stock;
			CreateFixture create{
				.expectedDevice = device,
				.expectedLinkage = linkage
			};
			g_createFixture = &create;
			const engine::PixelShaderSwapRequest request{
				.device = device,
				.linkage = linkage,
				.bytecode = stockBytes.data(),
				.bytecodeLength = stockBytes.size(),
				.variant = variant,
				.stockSha1 = plan.stock.sha1,
				.stockOutput = &stock,
				.output = &output
			};
			features::sss_dxbc_patch::AtomicCounters counters;
			Check(
				features::sss_dxbc_patch::ResolveRequest(
					artifact,
					true,
					false,
					true,
					counters,
					request,
					&CreatePatchedShader)
						== engine::PixelShaderSwapResolverResult::kKeepStock
					&& output == &stock
					&& stock.references == 1
					&& !create.called
					&& features::sss_dxbc_patch::
						SnapshotCounters(counters).unknownStockFallback == 1,
				"unpublished dispatch route did not stay stock");
		}
		{
			FakePixelShader stock;
			ID3D11PixelShader* output = &stock;
			auto* nonNullLinkage =
				reinterpret_cast<ID3D11ClassLinkage*>(
					const_cast<std::byte*>(stockBytes.data() + 1));
			CreateFixture create{
				.expectedDevice = device,
				.expectedLinkage = nonNullLinkage
			};
			g_createFixture = &create;
			const engine::PixelShaderSwapRequest request{
				.device = device,
				.linkage = nonNullLinkage,
				.bytecode = stockBytes.data(),
				.bytecodeLength = stockBytes.size(),
				.variant = variant,
				.stockSha1 = plan.stock.sha1,
				.stockOutput = &stock,
				.output = &output
			};
			features::sss_dxbc_patch::AtomicCounters counters;
			Check(
				features::sss_dxbc_patch::ResolveRequest(
					artifact,
					true,
					true,
					true,
					counters,
					request,
					&CreatePatchedShader)
						== engine::PixelShaderSwapResolverResult::kKeepStock
					&& output == &stock
					&& stock.references == 1
					&& !create.called,
				"non-null class linkage bypassed stock fallback guard");
		}
	}

	void TestMaskBinding()
	{
		FakeSrv real;
		FakeSrv white;
		FakeSrv foreign;
		constexpr features::sss_mask_binding::Extent required{
			1920,
			1080
		};
		constexpr features::sss_mask_binding::Extent fullExtent{
			1920,
			1080
		};
		constexpr features::sss_mask_binding::Extent onePixel{
			1,
			1
		};
		Check(
			features::sss_mask_binding::kWhiteR8Unorm == 0xFF
				&& features::sss_mask_binding::LoadInBounds(
					onePixel,
					0,
					0)
				&& !features::sss_mask_binding::LoadInBounds(
					onePixel,
					required.width - 1,
					required.height - 1)
				&& features::sss_mask_binding::LoadInBounds(
					fullExtent,
					required.width - 1,
					required.height - 1)
				&& !features::sss_mask_binding::Covers(
					onePixel,
					required)
				&& features::sss_mask_binding::Covers(
					fullExtent,
					required),
			"Texture2D.Load extent model did not reject a 1x1 fallback");

		features::sss_mask_binding::ExtentState lifecycle;
		lifecycle.Commit({ 1280, 720 });
		Check(
			lifecycle.IsCompatible({ 1280, 720 })
				&& !lifecycle.CompleteAllocation(
					fullExtent,
					false)
				&& lifecycle.allocated
					== features::sss_mask_binding::Extent{
						1280,
						720
					}
				&& !lifecycle.IsCompatible(required),
			"failed transactional growth did not retain the old extent");
		Check(
			lifecycle.CompleteAllocation(fullExtent, true),
			"successful transactional growth was not committed");
		Check(
			lifecycle.IsCompatible(required),
			"successful transactional growth did not admit the new extent");
		Check(
			features::sss_mask_binding::TelemetryBindingCountsHold(
				3,
				0,
				0,
				1,
				2,
				0)
				&& !features::sss_mask_binding::
					TelemetryBindingCountsHold(
						3,
						0,
						0,
						1,
						0,
						0),
			"stock-restored draws were omitted from binding telemetry accounting");

		{
			BindingFixture fixture;
			const auto result = features::sss_mask_binding::Bind(
				BindingApi(fixture),
				6,
				&real,
				fullExtent,
				true,
				&white,
				fullExtent,
				required);
			Check(
				result.validBinding
					&& result.source
						== features::sss_mask_binding::Source::kRealMask
					&& fixture.bound == &real,
				"ready real mask was not bound");
			Check(
				features::sss_mask_binding::RestoreNullIfOwned(
				BindingApi(fixture),
				6,
				&real,
				&white),
				"owned real mask was not recognized during cleanup");
			Check(!fixture.bound, "real mask restore was not null");
		}
		{
			BindingFixture fixture;
			const auto result = features::sss_mask_binding::Bind(
				BindingApi(fixture),
				6,
				&real,
				fullExtent,
				false,
				&white,
				fullExtent,
				required);
			Check(
				result.validBinding
					&& result.source
						== features::sss_mask_binding::Source::
							kWhiteFallback
					&& fixture.bound == &white,
				"white fallback was not bound");
			Check(
				features::sss_mask_binding::RestoreNullIfOwned(
				BindingApi(fixture),
				6,
				&real,
				&white),
				"owned white fallback was not recognized during cleanup");
			Check(!fixture.bound, "white fallback restore was not null");
		}
		{
			BindingFixture fixture;
			fixture.Assign(&foreign);
			const auto result = features::sss_mask_binding::Bind(
				BindingApi(fixture),
				6,
				&real,
				fullExtent,
				true,
				&white,
				fullExtent,
				required);
			Check(
				result.foreignBindingDetected
					&& !result.validBinding
					&& result.source
						== features::sss_mask_binding::Source::kNone
					&& fixture.bound == &foreign,
				"foreign t6 was overwritten instead of failing closed");
			Check(
				!features::sss_mask_binding::RestoreNullIfOwned(
					BindingApi(fixture),
					6,
					&real,
					&white)
					&& fixture.bound == &foreign,
				"end-of-pass cleanup cleared a foreign t6");
		}
		{
			BindingFixture fixture;
			fixture.rejectSet = true;
			const auto result = features::sss_mask_binding::Bind(
				BindingApi(fixture),
				6,
				&real,
				fullExtent,
				true,
				&white,
				fullExtent,
				required);
			Check(
				!result.validBinding,
				"invalid t6 binding was not detected");
		}
		{
			BindingFixture fixture;
			const auto result = features::sss_mask_binding::Bind(
				BindingApi(fixture),
				6,
				nullptr,
				{},
				false,
				&white,
				onePixel,
				required);
			Check(
				!result.validBinding
					&& result.source
						== features::sss_mask_binding::Source::kNone
					&& !fixture.bound,
				"extent-incompatible 1x1 fallback was bound");
		}
	}

	void TestTelemetryRelationships()
	{
		features::sss_dxbc_patch::TelemetrySnapshot snapshot{
			.candidateSeen = 3,
			.exactRouteAdmitted = 2,
			.patchBuilt = 2,
			.createPsAccepted = 1,
			.createPsRejected = 1,
			.identityPublished = 1,
			.coveragePass = 2,
			.coverageCandidates = 64,
			.artifactLoaded = true
		};
		Check(
			features::sss_dxbc_patch::TelemetryRelationshipsHold(snapshot),
			"valid telemetry relationships were rejected");
		snapshot.exactRouteAdmitted = 4;
		Check(
			!features::sss_dxbc_patch::TelemetryRelationshipsHold(snapshot),
			"invalid admission telemetry relationship was accepted");
		snapshot.exactRouteAdmitted = 2;
		snapshot.bindingReady = true;
		snapshot.invariantBroken = true;
		Check(
			!features::sss_dxbc_patch::TelemetryRelationshipsHold(snapshot),
			"telemetry allowed ready=true with a broken invariant");
	}

	void TestConcurrentTelemetrySnapshot()
	{
		const auto resolverSnapshot =
			SnapshotDuringConcurrentPublish(
				features::sss_dxbc_patch::SnapshotTestPoint::
					kCandidateSeen,
				{
					.candidateSeen = 1,
					.exactRouteAdmitted = 1
				});
		Check(
			features::sss_dxbc_patch::TelemetryRelationshipsHold(
				resolverSnapshot)
				&& resolverSnapshot.candidateSeen == 1
				&& resolverSnapshot.exactRouteAdmitted == 1,
			"concurrent resolver update produced an incoherent telemetry snapshot");

		const auto drawSnapshot =
			SnapshotDuringConcurrentPublish(
				features::sss_dxbc_patch::SnapshotTestPoint::
					kPatchedDrawMatched,
				{
					.patchedDrawMatched = 1,
					.realMaskBinds = 1
				});
		Check(
			drawSnapshot.patchedDrawMatched == 1
				&& drawSnapshot.realMaskBinds == 1
				&& features::sss_mask_binding::
					TelemetryBindingCountsHold(
						drawSnapshot.patchedDrawMatched,
						drawSnapshot.realMaskBinds,
						drawSnapshot.whiteFallbackBinds,
						drawSnapshot.invariantViolations,
						drawSnapshot.stockShaderFallback,
						drawSnapshot.stockShaderFallbackFailed),
			"concurrent patched draw update produced an incoherent telemetry snapshot");
	}

	void TestStickyBindingInvariant()
	{
		features::sss_dxbc_patch::BindingReadinessLatch latch;
		Check(
			latch.Publish(true)
				&& latch.IsReady()
				&& !latch.IsBroken(),
			"initial compatible extent did not arm patch admission");
		latch.Break();
		latch.Break();
		const auto brokenSnapshot = latch.GetSnapshot();
		Check(
			brokenSnapshot.broken && !brokenSnapshot.ready,
			"binding latch snapshot reported ready and broken");
		for (int frame = 0; frame < 4; ++frame) {
			Check(
				!latch.Publish(true)
					&& !latch.IsReady()
					&& latch.IsBroken(),
				"resource recovery re-armed a broken session invariant");
		}
		Check(
			!latch.Publish(false)
				&& !latch.IsReady()
				&& latch.IsBroken(),
			"resize reset the broken session invariant");

		const auto stockBytes = MakeSyntheticDxbc();
		const auto plan = FinalizeSyntheticPlan(stockBytes);
		const auto artifact = MakeSyntheticArtifact(stockBytes, plan);
		FakePixelShader stock;
		ID3D11PixelShader* output = &stock;
		auto* device = reinterpret_cast<ID3D11Device*>(
			const_cast<std::byte*>(stockBytes.data()));
		CreateFixture create{
			.expectedDevice = device,
			.expectedLinkage = nullptr
		};
		g_createFixture = &create;
		const engine::PixelShaderSwapRequest request{
			.device = device,
			.bytecode = stockBytes.data(),
			.bytecodeLength = stockBytes.size(),
			.variant = engine::ViewShaderVariantKey(
				artifact.routes[0].variant),
			.stockSha1 = plan.stock.sha1,
			.stockOutput = &stock,
			.output = &output
		};
		features::sss_dxbc_patch::AtomicCounters counters;
		Check(
			features::sss_dxbc_patch::ResolveRequest(
				artifact,
				true,
				true,
				latch.IsReady(),
				counters,
				request,
				&CreatePatchedShader)
					== engine::PixelShaderSwapResolverResult::kKeepStock
				&& output == &stock
				&& stock.references == 1
				&& !create.called,
			"broken session invariant admitted a new patched shader");
	}

	void TestEffectivePatchAdmission()
	{
		for (unsigned mask = 0; mask < 128; ++mask) {
			const bool artifactLoaded = (mask & 1u) != 0;
			const bool runtimeExact = (mask & 2u) != 0;
			const bool resolverActive = (mask & 4u) != 0;
			const bool brokerHookInstalled = (mask & 8u) != 0;
			const bool dispatchPublished = (mask & 16u) != 0;
			const bool bindingReady = (mask & 32u) != 0;
			const bool invariantBroken = (mask & 64u) != 0;
			const bool expectedReady =
				artifactLoaded
				&& runtimeExact
				&& resolverActive
				&& brokerHookInstalled
				&& dispatchPublished
				&& bindingReady
				&& !invariantBroken;
			const auto admission =
				features::sss_dxbc_patch::EvaluatePatchAdmission(
					artifactLoaded,
					runtimeExact,
					resolverActive,
					brokerHookInstalled,
					dispatchPublished,
					bindingReady,
					invariantBroken);
			Check(
				admission.ready == expectedReady
					&& admission.stockFallback == !expectedReady,
				"effective patch admission combination was contradictory");
		}

		const bool resolverRegistrationSucceeded = true;
		const auto installFailure =
			features::sss_dxbc_patch::EvaluatePatchAdmission(
				true,
				true,
				resolverRegistrationSucceeded,
				false,
				true,
				true,
				false);
		Check(
			!installFailure.ready && installFailure.stockFallback,
			"broker install failure was reported as patch-ready");

		features::sss_dxbc_patch::TelemetrySnapshot telemetry;
		telemetry.coveragePass = 2;
		telemetry.coverageCandidates = 64;
		telemetry.artifactLoaded = true;
		telemetry.runtimeExact = true;
		telemetry.resolverActive = resolverRegistrationSucceeded;
		telemetry.brokerHookInstalled = false;
		telemetry.dispatchPublished = true;
		telemetry.bindingReady = true;
		telemetry.admissionReady = installFailure.ready;
		telemetry.stockFallback = installFailure.stockFallback;
		Check(
			features::sss_dxbc_patch::TelemetryRelationshipsHold(telemetry),
			"broker install failure produced contradictory fallback telemetry");
		telemetry.admissionReady = true;
		telemetry.stockFallback = false;
		Check(
			!features::sss_dxbc_patch::TelemetryRelationshipsHold(telemetry),
			"telemetry accepted patch admission without the broker hook");

		const auto publicationFailure =
			features::sss_dxbc_patch::EvaluatePatchAdmission(
				true,
				true,
				resolverRegistrationSucceeded,
				true,
				false,
				true,
				false);
		telemetry.brokerHookInstalled = true;
		telemetry.dispatchPublished = false;
		telemetry.admissionReady = publicationFailure.ready;
		telemetry.stockFallback = publicationFailure.stockFallback;
		Check(
			!publicationFailure.ready
				&& publicationFailure.stockFallback
				&& features::sss_dxbc_patch::
					TelemetryRelationshipsHold(telemetry),
			"missing dispatch publication produced contradictory telemetry");
	}

	void TestFallbackAllocationBackoff()
	{
		using namespace features::sss_mask_binding;
		FallbackAllocationBackoff backoff;
		ExtentState allocated;
		allocated.Commit({ 1280, 720 });
		std::uint32_t attempts = 0;
		const auto attempt =
			[&](FallbackAllocationKey a_key, bool a_succeeds) {
				if (!backoff.ShouldAttempt(a_key))
					return;
				++attempts;
				if (a_succeeds) {
					allocated.Commit(a_key.extent);
					backoff.RecordSuccess();
				} else {
					backoff.RecordFailure(a_key);
				}
			};

		const FallbackAllocationKey failedGrowth{
			{ 1920, 1080 },
			1
		};
		for (int frame = 0; frame < 120; ++frame)
			attempt(failedGrowth, false);
		Check(
			attempts == 1
				&& backoff.IsLatchedFor(failedGrowth)
				&& allocated.allocated == Extent{ 1280, 720 }
				&& allocated.IsCompatible({ 1280, 720 })
				&& !allocated.IsCompatible(failedGrowth.extent),
			"persistent fallback failure retried or replaced the prior resource");

		const FallbackAllocationKey changedExtent{
			{ 2560, 1440 },
			1
		};
		attempt(changedExtent, false);
		attempt(changedExtent, false);
		Check(
			attempts == 2
				&& backoff.IsLatchedFor(changedExtent),
			"changed extent did not retry exactly once");

		const FallbackAllocationKey successfulExtent{
			{ 3440, 1440 },
			1
		};
		attempt(successfulExtent, true);
		Check(
			attempts == 3
				&& !backoff.HasFailure()
				&& allocated.allocated == successfulExtent.extent,
			"successful changed extent did not commit transactionally");

		const FallbackAllocationKey failedDevice{
			{ 3840, 2160 },
			1
		};
		attempt(failedDevice, false);
		attempt(failedDevice, false);
		const FallbackAllocationKey nextDevice{
			failedDevice.extent,
			2
		};
		attempt(nextDevice, true);
		Check(
			attempts == 5
				&& !backoff.HasFailure()
				&& allocated.allocated == nextDevice.extent,
			"device generation change did not permit one fresh attempt");
	}
}

int main(int a_argc, char** a_argv)
{
	if (a_argc != 3 && a_argc != 4) {
		std::cerr << "usage: SssDxbcPatchTests [--package] <artifact> <config>\n";
		return 2;
	}
	const bool packageOnly =
		a_argc == 4 && std::string_view(a_argv[1]) == "--package";
	const auto artifactPath =
		std::filesystem::path(a_argv[packageOnly ? 2 : 1]);
	const auto configPath =
		std::filesystem::path(a_argv[packageOnly ? 3 : 2]);

	struct Test
	{
		const char* name;
		void (*run)();
	};
	try {
		TestPackageArtifact(artifactPath, configPath);
		std::cout << "PASS: package artifact and shipped default\n";
		if (packageOnly)
			return 0;
		TestArtifactRejections(artifactPath);
		std::cout << "PASS: strict artifact rejection\n";
		TestExactIdentitySelection(artifactPath);
		std::cout << "PASS: exact identity selection\n";
		const Test tests[]{
			{ "bounded patch builder", &TestPatchBuilder },
			{ "independent DXBC checksum vectors", &TestDxbcChecksumVectors },
			{ "publication transaction", &TestPublicationTransaction },
			{ "integrated patch resolver", &TestIntegratedResolver },
			{ "transactional t6 binding", &TestMaskBinding },
			{ "telemetry relationships", &TestTelemetryRelationships },
			{ "concurrent telemetry snapshot", &TestConcurrentTelemetrySnapshot },
			{ "sticky binding invariant", &TestStickyBindingInvariant },
			{ "effective patch admission", &TestEffectivePatchAdmission },
			{ "fallback allocation backoff", &TestFallbackAllocationBackoff }
		};
		for (const auto& test : tests) {
			test.run();
			std::cout << "PASS: " << test.name << '\n';
		}
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}

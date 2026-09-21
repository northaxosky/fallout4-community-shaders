#include "Log.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "Render/ShaderFamilyDescriptor.h"
#include "Render/ShaderVariantCompilation.h"
#include "Render/SharedData.h"
#include "Utils/CSSha1.h"
#include "Utils/CSSha256.h"
#include "Utils/ShaderCompile.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace cs::log
{
	spdlog::logger* Get(const char*)
	{
		return spdlog::default_logger_raw();
	}
}

namespace cs::engine
{
	std::shared_ptr<ShaderVariantCompilationCache>
		CreateCachingShaderVariantCompilationCache()
	{
		return {};
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

	bool EnsureDeferredDrawAnchorInstalled()
	{
		return true;
	}
}

namespace cs::render
{
	void EnsureSharedDataUpdateInstalled()
	{}

	bool IsSharedDataReady() noexcept
	{
		return true;
	}

	void BindSharedData(
		ID3D11DeviceContext*,
		cs::engine::ShaderStage) noexcept
	{}

	bool IsDeferredLightsActive() noexcept
	{
		return false;
	}
}

namespace
{
	using ShaderDefines =
		std::vector<std::pair<std::string, std::string>>;

	struct ShaderCase
	{
		const char* path;
		ShaderDefines defines;
		const char* profile{ "cs_5_0" };
		const char* entryPoint{ "main" };
	};

	enum class SubstrateExpectation
	{
		kNone,
		kAbsent,
		kPresent
	};

	enum class FeatureIdentityVariant : std::uint8_t
	{
		kBase,
		kWetness,
		kTerrainShadows,
		kInverseSquareLighting
	};

	struct FeatureOffIdentityExpectation
	{
		std::string            key;
		FeatureIdentityVariant variant = FeatureIdentityVariant::kBase;
		bool                   wetnessShouldDiffer = false;
		bool                   terrainShouldDiffer = false;
		bool                   inverseSquareShouldDiffer = false;
		bool                   expectTerrainVariant = false;
		bool                   expectInverseSquareVariant = false;
	};

	struct StrippedShaderIdentityExpectation
	{
		std::size_t byteLength = 0;
		std::string sha1;
		std::string sha256;
	};

	std::string NativeRouteKey(
		cs::engine::ShaderInjectionTarget a_target,
		cs::engine::ShaderStage a_stage,
		std::uint32_t a_descriptor,
		std::string_view a_nativeName,
		std::string_view a_nativeClassName,
		bool a_forceEarlyDepthStencil,
		std::string_view a_nativeSourceGroup,
		const cs::engine::ShaderInjectionDefines& a_nativeMacros)
	{
		auto key = std::to_string(
				static_cast<std::uint32_t>(a_target))
			+ "|" + std::to_string(
				static_cast<std::uint32_t>(a_stage))
			+ "|" + std::to_string(a_descriptor)
			+ "|" + std::string(a_nativeName)
			+ "|" + std::string(a_nativeClassName)
			+ "|" + (a_forceEarlyDepthStencil ? "1" : "0")
			+ "|" + std::string(a_nativeSourceGroup);
		for (const auto& [name, value] : a_nativeMacros)
			key += "|" + name + "=" + value;
		return key;
	}

	struct BaselineShaderCase
	{
		cs::engine::ShaderInjectionTarget targetId =
			cs::engine::ShaderInjectionTarget::kCount;
		std::string name;
		std::string routeKey;
		std::string expectedStockSha1;
		cs::engine::ShaderStage stage =
			cs::engine::ShaderStage::kCompute;
		std::uint32_t descriptor = 0;
		cs::engine::ShaderVariantCompilationDescriptor compilation;
		StrippedShaderIdentityExpectation expected;
		std::string compileInputAliasGroup;
		std::string preparationError;
	};

	bool IsLowerHexString(std::string_view a_value, std::size_t a_size)
	{
		return a_value.size() == a_size
			&& std::ranges::all_of(a_value, [](char a_character) {
				return (a_character >= '0' && a_character <= '9')
					|| (a_character >= 'a' && a_character <= 'f');
			});
	}

	void SetTomlFieldError(
		std::string& a_error,
		std::string_view a_location,
		std::string_view a_field,
		std::string_view a_message)
	{
		a_error = std::string(a_location) + "." + std::string(a_field)
			+ " " + std::string(a_message);
	}

	template <class T>
	std::optional<T> RequireTomlValue(
		const toml::table& a_table,
		std::string_view a_field,
		std::string_view a_location,
		std::string& a_error,
		std::string_view a_expectedType)
	{
		const auto* node = a_table.get(a_field);
		const auto value = node ? node->value<T>() : std::nullopt;
		if (!value) {
			SetTomlFieldError(
				a_error,
				a_location,
				a_field,
				"must be " + std::string(a_expectedType));
		}
		return value;
	}

	std::string OptionalTomlString(
		const toml::table& a_table,
		std::string_view a_field,
		std::string_view a_location,
		std::string& a_error)
	{
		const auto* node = a_table.get(a_field);
		if (!node)
			return {};
		const auto value = node->value<std::string>();
		if (!value) {
			SetTomlFieldError(
				a_error,
				a_location,
				a_field,
				"must be a string when present");
			return {};
		}
		return *value;
	}

	std::vector<BaselineShaderCase> GetBaselineShaderCases(
		const std::filesystem::path& a_path,
		std::string& a_error)
	{
		toml::table root;
		try {
			root = toml::parse_file(a_path.string());
		} catch (const toml::parse_error& error) {
			a_error = "Failed to parse " + a_path.string() + ": "
				+ std::string(error.description());
			return {};
		} catch (const std::exception& error) {
			a_error = "Failed to read " + a_path.string() + ": "
				+ error.what();
			return {};
		}

		const auto schemaVersion =
			root["schema_version"].value<std::int64_t>();
		if (!schemaVersion || *schemaVersion != 1) {
			a_error = a_path.string()
				+ ": schema_version must be integer 1";
			return {};
		}
		const auto* witnesses = root["witnesses"].as_array();
		if (!witnesses) {
			a_error = a_path.string()
				+ ": witnesses must be an array";
			return {};
		}

		std::vector<BaselineShaderCase> cases;
		cases.reserve(witnesses->size());
		for (std::size_t index = 0; index < witnesses->size(); ++index) {
			const auto location = a_path.string() + ": witnesses["
				+ std::to_string(index) + "]";
			const auto* witness = (*witnesses)[index].as_table();
			if (!witness) {
				a_error = location + " must be a table";
				return {};
			}
			const auto fail = [&](std::string_view a_field,
								  std::string_view a_message) {
				SetTomlFieldError(
					a_error,
					location,
					a_field,
					a_message);
			};

			const auto targetName = RequireTomlValue<std::string>(
				*witness, "target", location, a_error, "a string");
			const auto stageName = RequireTomlValue<std::string>(
				*witness, "stage", location, a_error, "a string");
			const auto stageId = RequireTomlValue<std::int64_t>(
				*witness, "stage_id", location, a_error, "an integer");
			const auto nativeName = OptionalTomlString(
				*witness, "native_name", location, a_error);
			const auto nativeClass = OptionalTomlString(
				*witness, "native_class", location, a_error);
			const auto earlyDepth = RequireTomlValue<bool>(
				*witness, "early_depth", location, a_error, "a boolean");
			const auto reached = RequireTomlValue<bool>(
				*witness, "reached", location, a_error, "a boolean");
			const auto sourceGroup = OptionalTomlString(
				*witness, "source_group", location, a_error);
			const auto stockSha1 = RequireTomlValue<std::string>(
				*witness, "stock_sha1", location, a_error, "a string");
			const auto aliasGroup = OptionalTomlString(
				*witness, "alias_group", location, a_error);
			if (!a_error.empty())
				return {};

			const auto* target =
				cs::engine::FindShaderInjectionTarget(*targetName);
			if (!target) {
				fail("target", "contains unknown target \""
					+ *targetName + "\"");
				return {};
			}
			cs::engine::ShaderStage stage;
			if (*stageName == "vertex") {
				stage = cs::engine::ShaderStage::kVertex;
			} else if (*stageName == "pixel") {
				stage = cs::engine::ShaderStage::kPixel;
			} else if (*stageName == "compute") {
				stage = cs::engine::ShaderStage::kCompute;
			} else {
				fail("stage", "contains unknown stage \""
					+ *stageName + "\"");
				return {};
			}
			if (*stageId < 0
				|| static_cast<std::uint64_t>(*stageId)
					> std::numeric_limits<std::uint32_t>::max()) {
				fail("stage_id", "must fit an unsigned 32-bit integer");
				return {};
			}
			if (!IsLowerHexString(*stockSha1, 40)) {
				fail("stock_sha1",
					"must be a 40-character lowercase SHA-1");
				return {};
			}

			cs::engine::ShaderInjectionDefines nativeMacros;
			if (const auto* macrosNode = witness->get("macros")) {
				const auto* macros = macrosNode->as_table();
				if (!macros) {
					fail("macros", "must be a table when present");
					return {};
				}
				if (macros->size() > 7) {
					fail("macros", "must contain at most 7 entries");
					return {};
				}
				for (const auto& [name, valueNode] : *macros) {
					const auto value = valueNode.value<std::string>();
					if (!value) {
						fail("macros", "values must be strings");
						return {};
					}
					nativeMacros.emplace(name.str(), *value);
				}
			}

			const auto* candidate = witness->get("candidate");
			const auto* candidateTable =
				candidate ? candidate->as_table() : nullptr;
			if (!candidateTable) {
				fail("candidate", "must be a table");
				return {};
			}
			const auto candidateLocation = location + ".candidate";
			const auto byteLength = RequireTomlValue<std::int64_t>(
				*candidateTable,
				"byte_length",
				candidateLocation,
				a_error,
				"an integer");
			const auto candidateSha1 = RequireTomlValue<std::string>(
				*candidateTable,
				"sha1",
				candidateLocation,
				a_error,
				"a string");
			const auto candidateSha256 = RequireTomlValue<std::string>(
				*candidateTable,
				"sha256",
				candidateLocation,
				a_error,
				"a string");
			if (!a_error.empty())
				return {};
			if (*byteLength < 0
				|| static_cast<std::uint64_t>(*byteLength)
					> std::numeric_limits<std::size_t>::max()) {
				fail("candidate.byte_length",
					"must fit an unsigned size");
				return {};
			}
			if (!IsLowerHexString(*candidateSha1, 40)) {
				fail("candidate.sha1",
					"must be a 40-character lowercase SHA-1");
				return {};
			}
			if (!IsLowerHexString(*candidateSha256, 64)) {
				fail("candidate.sha256",
					"must be a 64-character lowercase SHA-256");
				return {};
			}

			const auto stageDescriptor =
				static_cast<std::uint32_t>(*stageId);
			BaselineShaderCase shaderCase{
				.targetId = target->id,
				.name = *reached ?
					*stockSha1 :
					"archive-unreachable:" + *stockSha1,
				.routeKey = NativeRouteKey(
					target->id,
					stage,
					stageDescriptor,
					nativeName,
					nativeClass,
					*earlyDepth,
					sourceGroup,
					nativeMacros),
				.expectedStockSha1 = *stockSha1,
				.stage = stage,
				.descriptor = stageDescriptor,
				.expected = {
					.byteLength =
						static_cast<std::size_t>(*byteLength),
					.sha1 = *candidateSha1,
					.sha256 = *candidateSha256
				},
				.compileInputAliasGroup = aliasGroup
			};
			const auto compilationDescriptor =
				cs::engine::BuildShaderFamilyCompilationDescriptor({
					.target = target->id,
					.stage = stage,
					.descriptor = shaderCase.descriptor,
					.nativeName = nativeName,
					.nativeClassName = nativeClass,
					.nativeSourceGroup = sourceGroup,
					.forceEarlyDepthStencil = *earlyDepth,
					.nativeMacros = std::move(nativeMacros)
				});
			if (compilationDescriptor) {
				shaderCase.compilation = *compilationDescriptor;
			} else {
				shaderCase.preparationError =
					"Native family descriptor was not admitted";
			}
			cases.push_back(std::move(shaderCase));
		}
		return cases;
	}

	struct ShaderCompileJob
	{
		std::filesystem::path path;
		ShaderDefines         defines;
		std::string           profile;
		std::string           entryPoint;
		std::string           description;
		std::string           preparationError;
		SubstrateExpectation  substrateExpectation =
			SubstrateExpectation::kNone;
		bool                  validateXeGTAOCB = false;
		std::vector<UINT>     requiredTextureSlots;
		std::vector<UINT>     forbiddenTextureSlots;
		std::vector<UINT>     requiredSamplerSlots;
		std::vector<UINT>     forbiddenSamplerSlots;
		std::optional<FeatureOffIdentityExpectation> featureOffIdentity;
		std::optional<StrippedShaderIdentityExpectation>
			strippedIdentity;
	};

	struct ShaderCompileResult
	{
		std::string error;
		Microsoft::WRL::ComPtr<ID3DBlob> blob;
	};

	std::string CompileInputKey(
		const std::filesystem::path& a_path,
		const ShaderDefines& a_defines,
		const char* a_profile = "cs_5_0",
		const char* a_entryPoint = "main")
	{
		std::string key = a_path.string();
		key.append("|").append(a_profile).append("|").append(a_entryPoint);
		for (const auto& [name, value] : a_defines)
			key.append("|").append(name).append("=").append(value);
		return key;
	}

	template <class Defines>
	ShaderDefines CopyShaderDefines(const Defines& a_defines)
	{
		ShaderDefines defines;
		defines.reserve(a_defines.size());
		for (const auto& [name, value] : a_defines)
			defines.emplace_back(name, value);
		return defines;
	}

	std::string BaselineCompileInputKey(
		const std::filesystem::path& a_root,
		const BaselineShaderCase& a_registration)
	{
		const auto& compilation = a_registration.compilation;
		return CompileInputKey(
			a_root / compilation.sourcePath,
			CopyShaderDefines(compilation.defines),
			compilation.profile.c_str(),
			compilation.entryPoint.c_str());
	}

	ShaderCompileJob& AddCompile(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_path,
		ShaderDefines a_defines,
		const char* a_profile = "cs_5_0",
		const char* a_entryPoint = "main",
		std::string a_context = {},
		SubstrateExpectation a_substrateExpectation =
			SubstrateExpectation::kNone)
	{
		std::string description = a_context.empty() ?
			a_path.string() :
			std::move(a_context) + ": " + a_path.string();
		description.append(" [")
			.append(a_profile)
			.append("/")
			.append(a_entryPoint);
		for (const auto& [name, value] : a_defines)
			description.append(", ").append(name).append("=").append(value);
		description.append("]");

		a_jobs.push_back({
			.path = a_path,
			.defines = std::move(a_defines),
			.profile = a_profile,
			.entryPoint = a_entryPoint,
			.description = std::move(description),
			.substrateExpectation = a_substrateExpectation
		});
		return a_jobs.back();
	}

	void AddPreparationFailure(
		std::vector<ShaderCompileJob>& a_jobs,
		std::string a_description,
		std::string a_error)
	{
		a_jobs.push_back({
			.description = std::move(a_description),
			.preparationError = std::move(a_error)
		});
	}

	constexpr std::array kSssSurfaceContactDescriptors{
		0x1000U, 0x11000U, 0x81000U, 0x91000U, 0x181000U, 0x191000U
	};
	constexpr std::array kSssRecordNormalDescriptors{
		0x3000U, 0x1020U, 0x11020U, 0x13000U
	};

	void CheckNativeDescriptorAliases(std::vector<ShaderCompileJob>& a_jobs)
	{
		using namespace cs::engine;
		const auto check = [&a_jobs](
			ShaderInjectionTarget a_target,
			std::uint32_t a_canonical,
			std::span<const std::uint32_t> a_aliases) {
			const auto build = [a_target](std::uint32_t a_descriptor) {
				return BuildShaderFamilyCompilationDescriptor({
					.target = a_target,
					.stage = ShaderStage::kPixel,
					.descriptor = a_descriptor
				});
			};
			const auto canonical = build(a_canonical);
			for (const auto alias : a_aliases) {
				const auto actual = build(alias);
				if (!canonical || !actual
					|| actual->sourcePath != canonical->sourcePath
					|| actual->profile != canonical->profile
					|| actual->entryPoint != canonical->entryPoint
					|| actual->defines != canonical->defines) {
					AddPreparationFailure(
						a_jobs,
						"native descriptor alias " + std::to_string(alias),
						"Alias must use the same verified recipe as "
							+ std::to_string(a_canonical));
				}
			}
		};

		check(ShaderInjectionTarget::kBsdfLight, 0x440U, std::array{
			0x500U, 0x8010102U, 0x100U, 0x101U, 0x104U, 0x108U,
			0x10000100U, 0x10000101U, 0x10000104U, 0x10000108U,
			0x10000120U, 0x10000500U, 0x110U, 0x120U, 0x40U,
			0x10102U, 0x44U, 0x48U, 0x60U });
		check(ShaderInjectionTarget::kBsdfLight, 0x4804U,
			std::array{ 0x804U, 0xC804U, 0x1804U });
		check(ShaderInjectionTarget::kBsdfComposite, 0x30008U,
			std::array{
				0x210008U, 0x10008U, 0x10000U, 0x210000U });
		check(ShaderInjectionTarget::kBsdfComposite, 0x20088U,
			std::array{ 0x88U, 0x200088U });
		check(ShaderInjectionTarget::kBsdfComposite, 0x30208U,
			std::array{ 0x10208U });
		check(ShaderInjectionTarget::kBsdfComposite, 0x20008U,
			std::array{ 0x200008U, 0x8U, 0x200000U });
		check(ShaderInjectionTarget::kBsdfComposite, 0x20800U,
			std::array{
				0x800U, 0x40800U, 0x50800U, 0x60800U, 0x70800U });
		check(ShaderInjectionTarget::kBsdfComposite, 0x20801U,
			std::array{
				0x801U, 0x805U, 0x20805U, 0x40801U, 0x40805U,
				0x60801U, 0x60805U, 0x70801U, 0x70805U });
		check(ShaderInjectionTarget::kBsdfComposite, 0x24088U,
			std::array{ 0x204088U });
		check(ShaderInjectionTarget::kBsdfComposite, 0x20208U,
			std::array{ 0x208U });
		check(ShaderInjectionTarget::kBsdfComposite, 0x30088U,
			std::array{ 0x210088U, 0x10088U });
		check(
			ShaderInjectionTarget::kBsdfComposite,
			kSssSurfaceContactDescriptors.front(),
			std::span(kSssSurfaceContactDescriptors).subspan(1));
		check(
			ShaderInjectionTarget::kBsdfComposite,
			kSssRecordNormalDescriptors.front(),
			std::span(kSssRecordNormalDescriptors).subspan(1));

		for (const auto descriptor : std::array{
				 0x21020U, 0x23000U, 0x31020U, 0x33000U,
				 0x61000U, 0x63000U, 0x71000U, 0x73000U,
				 0xC1000U, 0xD1000U, 0xE1000U, 0xF1000U,
				 0x41000U, 0x43000U, 0x51000U, 0x53000U,
				 0x21000U, 0x1A1000U, 0x1B1000U, 0x31000U, 0xA1000U, 0xB1000U }) {
			if (BuildShaderFamilyCompilationDescriptor({
					.target = ShaderInjectionTarget::kBsdfComposite,
					.stage = ShaderStage::kPixel,
					.descriptor = descriptor })) {
				AddPreparationFailure(
					a_jobs,
					"unsupported native SSS descriptor " + std::to_string(descriptor),
					"Unreconstructed SSS MRT variants must remain native");
			}
		}
	}

	void CheckImageSpaceFailureRouteAdmission(
		std::vector<ShaderCompileJob>& a_jobs)
	{
		using namespace cs::engine;
		const auto reject = [&a_jobs](
			std::string a_name,
			ShaderFamilyDescriptor a_descriptor) {
			if (BuildShaderFamilyCompilationDescriptor(a_descriptor)) {
				AddPreparationFailure(
					a_jobs,
					"Imagespace failure-route admission " + a_name,
					"Unproven or identity-mismatched native route was admitted");
			}
		};

		reject("nonzero copy descriptor", {
			.target = ShaderInjectionTarget::kImageSpace,
			.stage = ShaderStage::kPixel,
			.descriptor = 1,
			.nativeName = "ISCopy",
			.nativeClassName = "BSImagespaceShaderCopy",
			.nativeSourceGroup = "ISCopy"
		});
		reject("copy owner/macro cross-product", {
			.target = ShaderInjectionTarget::kImageSpace,
			.stage = ShaderStage::kPixel,
			.nativeName = "ISCopyNormals",
			.nativeClassName = "BSImagespaceShaderCopy",
			.nativeSourceGroup = "ISCopy",
			.nativeMacros = { { "COPY_NORMALS", "" } }
		});
		reject("fullscreen source-group-only fallback", {
			.target = ShaderInjectionTarget::kImageSpace,
			.stage = ShaderStage::kPixel,
			.nativeName = "ISFullScreenColor",
			.nativeClassName = "BSImagespaceShaderCopy",
			.nativeSourceGroup = "ISFullScreenColor"
		});
		reject("fullscreen parameter name is not a source group", {
			.target = ShaderInjectionTarget::kImageSpace,
			.stage = ShaderStage::kPixel,
			.nativeName = "ISFullScreenColor",
			.nativeClassName = "BSImagespaceShaderFullScreenColor",
			.nativeSourceGroup = "FullScreenColor"
		});
		reject("HUD Glass owner/macro cross-product", {
			.target = ShaderInjectionTarget::kImageSpace,
			.stage = ShaderStage::kVertex,
			.nativeName = "ISHUDGlassDS",
			.nativeClassName = "BSImagespaceShaderHUDGlass",
			.nativeSourceGroup = "ISHUDGlass",
			.nativeMacros = { { "DROPSHADOW", "" } }
		});
		reject("HUD Glass extra macro", {
			.target = ShaderInjectionTarget::kImageSpace,
			.stage = ShaderStage::kPixel,
			.nativeName = "ISHUDGlassCopy",
			.nativeClassName = "BSImagespaceShaderHUDGlassCopy",
			.nativeSourceGroup = "ISHUDGlass",
			.nativeMacros = {
				{ "COPY", "" },
				{ "UNPROVEN", "" }
			}
		});
		reject("unproven Gamma LUT route", {
			.target = ShaderInjectionTarget::kImageSpace,
			.stage = ShaderStage::kPixel,
			.nativeName = "ISGammaLUT",
			.nativeClassName = "BSImagespaceShaderGammaCorrectLUT",
			.nativeSourceGroup = "ISGamma",
			.nativeMacros = { { "LUT", "" } }
		});
		reject("unproven SSAO raw AO compute route", {
			.target = ShaderInjectionTarget::kImageSpace,
			.stage = ShaderStage::kCompute,
			.nativeName = "ISSAORawAOCS",
			.nativeClassName = "BSImagespaceShaderSAORawAOCS",
			.nativeSourceGroup = "ISSAORawAOCS"
		});
		reject("SSAO blur owner/grid cross-product", {
			.target = ShaderInjectionTarget::kImageSpace,
			.stage = ShaderStage::kCompute,
			.nativeName = "ISSAOBlurHCS",
			.nativeClassName = "BSImagespaceShaderSAOBlurHCS",
			.nativeSourceGroup = "ISSAOBlurCS",
			.nativeMacros = { { "AXIS_H", "" }, { "GRID_SIZE", "552" } }
		});
		reject("SSAO blur owner/axis cross-product", {
			.target = ShaderInjectionTarget::kImageSpace,
			.stage = ShaderStage::kCompute,
			.nativeName = "ISSAOBlurHCS",
			.nativeClassName = "BSImagespaceShaderSAOBlurHCS",
			.nativeSourceGroup = "ISSAOBlurCS",
			.nativeMacros = { { "AXIS_V", "" }, { "GRID_SIZE", "972" } }
		});
		reject("SSAO camera route extra macro", {
			.target = ShaderInjectionTarget::kImageSpace,
			.stage = ShaderStage::kCompute,
			.nativeName = "ISSAOCameraZAndMipsCS",
			.nativeClassName = "BSImagespaceShaderSAOCameraZAndMipsCS",
			.nativeSourceGroup = "ISSAOCameraZAndMipsCS",
			.nativeMacros = { { "UNPROVEN", "" } }
		});
		reject("LensFlare visibility macro is required", {
			.target = ShaderInjectionTarget::kImageSpace,
			.stage = ShaderStage::kPixel,
			.nativeName = "LensFlareVis",
			.nativeClassName = "BSLensFlareVis",
			.nativeSourceGroup = "LensFlare"
		});
		reject("LensFlare base/visibility cross-product", {
			.target = ShaderInjectionTarget::kImageSpace,
			.stage = ShaderStage::kPixel,
			.nativeName = "LensFlare",
			.nativeClassName = "BSLensFlare",
			.nativeSourceGroup = "LensFlare",
			.nativeMacros = { { "VISIBILITY", "" } }
		});
		reject("unproven HDR downsample route", {
			.target = ShaderInjectionTarget::kImageSpace,
			.stage = ShaderStage::kPixel,
			.nativeName = "ISHDRDownSample4",
			.nativeClassName = "BSImagespaceShaderHDRDownSample4",
			.nativeSourceGroup = "ISHDR",
			.nativeMacros = { { "DOWNSAMPLE", "" } }
		});
		reject("HDR complete set with wrong DOWNSAMPLE", {
			.target = ShaderInjectionTarget::kImageSpace,
			.stage = ShaderStage::kPixel,
			.nativeName = "ISHDRDownSample16Lum",
			.nativeClassName = "BSImagespaceShaderHDRDownSample16Lum",
			.nativeSourceGroup = "ISHDR",
			.nativeMacros = { { "DOWNSAMPLE", "4" }, { "LUM", "" } }
		});
		reject("HDR complete set with extra native flag", {
			.target = ShaderInjectionTarget::kImageSpace,
			.stage = ShaderStage::kPixel,
			.nativeName = "ISHDRDownSample16Lum",
			.nativeClassName = "BSImagespaceShaderHDRDownSample16Lum",
			.nativeSourceGroup = "ISHDR",
			.nativeMacros = {
				{ "DOWNSAMPLE", "16" },
				{ "LUM", "" },
				{ "RGB2LUM", "" }
			}
		});
	}

	struct ExpectedVariable
	{
		const char* name;
		UINT        offset;
		UINT        size;
	};

	std::string ValidateConstantBuffer(
		ID3D11ShaderReflection* a_reflection,
		const char* a_name,
		UINT a_bindPoint,
		UINT a_size,
		std::span<const ExpectedVariable> a_variables)
	{
		D3D11_SHADER_DESC shaderDesc{};
		if (FAILED(a_reflection->GetDesc(&shaderDesc)))
			return "shared substrate reflection description failed";

		std::optional<D3D11_SHADER_INPUT_BIND_DESC> binding;
		for (UINT index = 0; index < shaderDesc.BoundResources; ++index) {
			D3D11_SHADER_INPUT_BIND_DESC candidate{};
			if (SUCCEEDED(a_reflection->GetResourceBindingDesc(
					index,
					&candidate))
				&& candidate.Type == D3D_SIT_CBUFFER
				&& candidate.BindPoint == a_bindPoint) {
				binding = candidate;
				break;
			}
		}
		if (!binding)
			return std::string("missing reflected binding b")
				+ std::to_string(a_bindPoint);
		if (binding->BindCount != 1) {
			return std::string("unexpected binding for ") + a_name;
		}

		auto* buffer = a_reflection->GetConstantBufferByName(binding->Name);
		D3D11_SHADER_BUFFER_DESC bufferDesc{};
		if (!buffer || FAILED(buffer->GetDesc(&bufferDesc)))
			return std::string("missing reflected cbuffer ") + a_name;
		if (bufferDesc.Size != a_size
			|| bufferDesc.Variables != a_variables.size()) {
			return std::string("unexpected reflected layout for ") + a_name;
		}

		for (std::size_t index = 0; index < a_variables.size(); ++index) {
			const auto& expected = a_variables[index];
			auto* variable = buffer->GetVariableByIndex(
				static_cast<UINT>(index));
			D3D11_SHADER_VARIABLE_DESC variableDesc{};
			if (!variable || FAILED(variable->GetDesc(&variableDesc))) {
				return std::string("missing reflected variable ")
					+ a_name + "." + expected.name;
			}
			if (variableDesc.StartOffset != expected.offset
				|| variableDesc.Size != expected.size) {
				return std::string("unexpected reflected variable layout ")
					+ a_name + "." + expected.name;
			}
		}
		return {};
	}

	// Named lookup catches fields optimized out of a permutation.
	std::string ValidateConstantBufferOffsets(
		ID3D11ShaderReflection* a_reflection,
		const char* a_name,
		UINT a_bindPoint,
		UINT a_size,
		std::span<const ExpectedVariable> a_variables)
	{
		D3D11_SHADER_DESC shaderDesc{};
		if (FAILED(a_reflection->GetDesc(&shaderDesc)))
			return "constant buffer reflection description failed";

		std::optional<D3D11_SHADER_INPUT_BIND_DESC> binding;
		for (UINT index = 0; index < shaderDesc.BoundResources; ++index) {
			D3D11_SHADER_INPUT_BIND_DESC candidate{};
			if (SUCCEEDED(a_reflection->GetResourceBindingDesc(index, &candidate))
				&& candidate.Type == D3D_SIT_CBUFFER
				&& candidate.BindPoint == a_bindPoint) {
				binding = candidate;
				break;
			}
		}
		if (!binding)
			return std::string("missing reflected binding b")
				+ std::to_string(a_bindPoint);

		auto* buffer = a_reflection->GetConstantBufferByName(binding->Name);
		D3D11_SHADER_BUFFER_DESC bufferDesc{};
		if (!buffer || FAILED(buffer->GetDesc(&bufferDesc)))
			return std::string("missing reflected cbuffer ") + a_name;
		if (bufferDesc.Size != a_size) {
			return std::string("unexpected reflected size ")
				+ std::to_string(bufferDesc.Size) + " for " + a_name;
		}

		for (const auto& expected : a_variables) {
			auto* variable = buffer->GetVariableByName(expected.name);
			D3D11_SHADER_VARIABLE_DESC variableDesc{};
			if (!variable || FAILED(variable->GetDesc(&variableDesc))) {
				return std::string("missing reflected variable ")
					+ a_name + "." + expected.name;
			}
			if (variableDesc.StartOffset != expected.offset
				|| variableDesc.Size != expected.size) {
				return std::string("unexpected reflected variable layout ")
					+ a_name + "." + expected.name + " at "
					+ std::to_string(variableDesc.StartOffset);
			}
		}
		return {};
	}

	std::string ValidateXeGTAOConstantBuffer(ID3DBlob* a_blob)
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
		if (FAILED(D3DReflect(
				a_blob->GetBufferPointer(),
				a_blob->GetBufferSize(),
				__uuidof(ID3D11ShaderReflection),
				reinterpret_cast<void**>(reflection.GetAddressOf())))) {
			return "D3DReflect failed for the XeGTAO constant buffer witness";
		}

		constexpr std::array giVariables{
			ExpectedVariable{ "NDCToViewMul", 0, 16 },
			ExpectedVariable{ "NDCToViewAdd", 16, 16 },
			ExpectedVariable{ "TexDim", 32, 8 },
			ExpectedVariable{ "RcpTexDim", 40, 8 },
			ExpectedVariable{ "FrameDim", 48, 8 },
			ExpectedVariable{ "RcpFrameDim", 56, 8 },
			ExpectedVariable{ "PrevFrameDim", 64, 8 },
			ExpectedVariable{ "RcpPrevFrameDim", 72, 8 },
			ExpectedVariable{ "FrameIndex", 80, 4 },
			ExpectedVariable{ "NumSlices", 84, 4 },
			ExpectedVariable{ "NumSteps", 88, 4 },
			ExpectedVariable{ "MinScreenRadius", 92, 4 },
			ExpectedVariable{ "AORadius", 96, 4 },
			ExpectedVariable{ "EffectRadius", 100, 4 },
			ExpectedVariable{ "Thickness", 104, 4 },
			ExpectedVariable{ "GIRadius", 108, 4 },
			ExpectedVariable{ "DepthFadeRange", 112, 8 },
			ExpectedVariable{ "DepthFadeScaleConst", 120, 4 },
			ExpectedVariable{ "BlurRadius", 124, 4 },
			ExpectedVariable{ "DistanceNormalisation", 128, 4 },
			ExpectedVariable{ "CenterBeta", 132, 4 },
			ExpectedVariable{ "DepthDisocclusion", 136, 4 },
			ExpectedVariable{ "MaxAccumFrames", 140, 4 },
			ExpectedVariable{ "TemporalFlags", 144, 4 },
			ExpectedVariable{ "RadianceScale", 160, 8 },
			ExpectedVariable{ "PrevNDCToViewMul", 176, 8 },
			ExpectedVariable{ "PrevNDCToViewAdd", 184, 8 },
			ExpectedVariable{ "ViewToWorld", 192, 48 },
			ExpectedVariable{ "PrevViewToWorld", 240, 48 },
			ExpectedVariable{ "CameraOrigin", 288, 16 },
			ExpectedVariable{ "PrevCameraOrigin", 304, 16 }
		};
		return ValidateConstantBufferOffsets(
			reflection.Get(), "XeGTAOCB", 0, 320, giVariables);
	}

	std::string ValidateSubstrateReflection(
		ID3DBlob* a_blob,
		SubstrateExpectation a_expectation)
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
		if (FAILED(D3DReflect(
				a_blob->GetBufferPointer(),
				a_blob->GetBufferSize(),
				__uuidof(ID3D11ShaderReflection),
				reinterpret_cast<void**>(reflection.GetAddressOf())))) {
			return "D3DReflect failed for the shared substrate probe";
		}

		D3D11_SHADER_DESC shaderDesc{};
		if (FAILED(reflection->GetDesc(&shaderDesc)))
			return "shared substrate reflection description failed";
		if (a_expectation == SubstrateExpectation::kAbsent) {
			if (shaderDesc.ConstantBuffers != 0
				|| shaderDesc.BoundResources != 0) {
				return "inactive shared substrate probe emitted resources";
			}
			return {};
		}

		constexpr std::array sharedVariables{
			ExpectedVariable{ "CameraData", 0, 16 },
			ExpectedVariable{ "BufferDim", 16, 16 },
			ExpectedVariable{ "DynamicResolution", 32, 16 },
			ExpectedVariable{ "NDCToViewMul", 48, 16 },
			ExpectedVariable{ "NDCToViewAdd", 64, 16 },
			ExpectedVariable{ "SunDirection", 80, 16 },
			ExpectedVariable{ "Timer", 96, 4 },
			ExpectedVariable{ "DeltaTime", 100, 4 },
			ExpectedVariable{ "FrameCount", 104, 4 },
			ExpectedVariable{ "InInterior", 108, 4 }
		};
		constexpr std::array featureVariables{
			ExpectedVariable{ "screenSpaceShadowsSettings", 0, 16 },
			ExpectedVariable{ "screenSpaceGISettings", 16, 16 },
			ExpectedVariable{ "wetnessEffectsSettings", 32, 16 },
			ExpectedVariable{ "terrainShadowsSettings", 48, 48 },
			ExpectedVariable{
				"inverseSquareLightingSettings", 96, 16 },
			ExpectedVariable{ "waterEffectsSettings", 112, 16 },
			ExpectedVariable{ "dynamicCubemapsSettings", 128, 16 },
			ExpectedVariable{ "exponentialHeightFogSettings", 144, 16 }
		};
		if (shaderDesc.ConstantBuffers != 2
			|| shaderDesc.BoundResources != 2) {
			return "active shared substrate probe emitted the wrong resource count";
		}
		if (auto error = ValidateConstantBuffer(
				reflection.Get(),
				"SharedData",
				5,
				112,
				sharedVariables);
			!error.empty()) {
			return error;
		}
		return ValidateConstantBuffer(
				reflection.Get(),
				"FeatureData",
				6,
				160,
				featureVariables);
	}

	std::string ValidateTextureBindings(
		ID3DBlob* a_blob,
		const ShaderCompileJob& a_job)
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
		if (FAILED(D3DReflect(
				a_blob->GetBufferPointer(),
				a_blob->GetBufferSize(),
				__uuidof(ID3D11ShaderReflection),
				reinterpret_cast<void**>(reflection.GetAddressOf())))) {
			return "D3DReflect failed for the texture binding witness";
		}

		D3D11_SHADER_DESC shaderDesc{};
		if (FAILED(reflection->GetDesc(&shaderDesc)))
			return "texture binding reflection description failed";

		std::set<UINT> boundTextures;
		std::set<UINT> boundSamplers;
		for (UINT index = 0; index < shaderDesc.BoundResources; ++index) {
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			if (FAILED(reflection->GetResourceBindingDesc(index, &binding))) {
				continue;
			}
			for (UINT slot = 0; slot < std::max(binding.BindCount, 1u); ++slot) {
				if (binding.Type == D3D_SIT_TEXTURE)
					boundTextures.insert(binding.BindPoint + slot);
				else if (binding.Type == D3D_SIT_SAMPLER)
					boundSamplers.insert(binding.BindPoint + slot);
			}
		}

		for (const UINT slot : a_job.requiredTextureSlots) {
			if (!boundTextures.contains(slot))
				return "missing reflected texture t" + std::to_string(slot);
		}
		for (const UINT slot : a_job.forbiddenTextureSlots) {
			if (boundTextures.contains(slot))
				return "unexpected reflected texture t" + std::to_string(slot);
		}
		for (const UINT slot : a_job.requiredSamplerSlots) {
			if (!boundSamplers.contains(slot))
				return "missing reflected sampler s" + std::to_string(slot);
		}
		for (const UINT slot : a_job.forbiddenSamplerSlots) {
			if (boundSamplers.contains(slot))
				return "unexpected reflected sampler s" + std::to_string(slot);
		}
		return {};
	}

	std::string ValidateStrippedShaderIdentity(
		ID3DBlob* a_blob,
		const StrippedShaderIdentityExpectation& a_expected)
	{
		Microsoft::WRL::ComPtr<ID3DBlob> stripped;
		if (FAILED(D3DStripShader(
				a_blob->GetBufferPointer(),
				a_blob->GetBufferSize(),
				D3DCOMPILER_STRIP_REFLECTION_DATA,
				stripped.GetAddressOf()))
			|| !stripped) {
			return "D3DStripShader failed for the exact DXBC identity witness";
		}

		const auto byteLength =
			static_cast<std::size_t>(stripped->GetBufferSize());
		cs::sha1::Sha1InitOnce();
		const auto sha1 = cs::sha1::Sha1ToHex(cs::sha1::Sha1Compute(
			stripped->GetBufferPointer(),
			byteLength));
		const auto sha256 = cs::sha256::Sha256ToHex(
			cs::sha256::Sha256Compute(
				stripped->GetBufferPointer(),
				byteLength));
		if (byteLength != a_expected.byteLength
			|| sha1 != a_expected.sha1
			|| sha256 != a_expected.sha256) {
			return "stripped DXBC identity mismatch: expected "
				+ std::to_string(a_expected.byteLength) + " bytes, sha1="
				+ std::string(a_expected.sha1) + ", sha256="
				+ std::string(a_expected.sha256) + "; got "
				+ std::to_string(byteLength) + " bytes, sha1=" + sha1
				+ ", sha256=" + sha256;
		}
		return {};
	}

	ShaderCompileResult Compile(const ShaderCompileJob& a_job)
	{
		if (!a_job.preparationError.empty())
			return { a_job.preparationError };

		std::vector<std::pair<const char*, const char*>> defines;
		defines.reserve(a_job.defines.size());
		for (const auto& [name, value] : a_job.defines)
			defines.emplace_back(name.c_str(), value.c_str());

		const std::wstring widePath = a_job.path.wstring();
		std::string error;
		auto blob = cs::util::CompileShaderToBlob(
			widePath.c_str(),
			defines,
			a_job.profile.c_str(),
			a_job.entryPoint.c_str(),
			&error);
		if (!blob)
			return { std::move(error) };
		if (a_job.substrateExpectation != SubstrateExpectation::kNone) {
			if (auto validation = ValidateSubstrateReflection(
					blob.Get(),
					a_job.substrateExpectation);
				!validation.empty()) {
				return { std::move(validation) };
			}
		}
		if (a_job.validateXeGTAOCB) {
			if (auto validation = ValidateXeGTAOConstantBuffer(blob.Get());
				!validation.empty()) {
				return { std::move(validation) };
			}
		}
		if (!a_job.requiredTextureSlots.empty()
			|| !a_job.forbiddenTextureSlots.empty()
			|| !a_job.requiredSamplerSlots.empty()
			|| !a_job.forbiddenSamplerSlots.empty()) {
			if (auto validation = ValidateTextureBindings(blob.Get(), a_job);
				!validation.empty()) {
				return { std::move(validation) };
			}
		}
		if (a_job.strippedIdentity) {
			if (auto validation = ValidateStrippedShaderIdentity(
					blob.Get(),
					*a_job.strippedIdentity);
				!validation.empty()) {
				return { std::move(validation) };
			}
		}
		if (!a_job.featureOffIdentity)
			return {};
		return { {}, std::move(blob) };
	}

	struct FeatureOffIdentityPair
	{
		const ShaderCompileJob* featureOffJob = nullptr;
		const ShaderCompileResult* featureOffResult = nullptr;
		const ShaderCompileJob* wetnessJob = nullptr;
		const ShaderCompileResult* wetnessResult = nullptr;
		const ShaderCompileJob* terrainJob = nullptr;
		const ShaderCompileResult* terrainResult = nullptr;
		const ShaderCompileJob* inverseSquareJob = nullptr;
		const ShaderCompileResult* inverseSquareResult = nullptr;
		bool wetnessShouldDiffer = false;
		bool terrainShouldDiffer = false;
		bool inverseSquareShouldDiffer = false;
		bool expectTerrainVariant = false;
		bool expectInverseSquareVariant = false;
		bool hasExpectation = false;
	};

	std::optional<bool> HaveEqualStrippedShaderBytes(
		ID3DBlob* a_featureOff,
		ID3DBlob* a_featureOn,
		std::string& a_error)
	{
		constexpr UINT stripFlags = D3DCOMPILER_STRIP_DEBUG_INFO
			| D3DCOMPILER_STRIP_REFLECTION_DATA
			| D3DCOMPILER_STRIP_TEST_BLOBS
			| D3DCOMPILER_STRIP_PRIVATE_DATA;
		Microsoft::WRL::ComPtr<ID3DBlob> featureOff;
		if (FAILED(D3DStripShader(
				a_featureOff->GetBufferPointer(),
				a_featureOff->GetBufferSize(),
				stripFlags,
				featureOff.GetAddressOf()))) {
			a_error = "D3DStripShader failed for the feature-off blob";
			return std::nullopt;
		}
		Microsoft::WRL::ComPtr<ID3DBlob> featureOn;
		if (FAILED(D3DStripShader(
				a_featureOn->GetBufferPointer(),
				a_featureOn->GetBufferSize(),
				stripFlags,
				featureOn.GetAddressOf()))) {
			a_error = "D3DStripShader failed for the feature-on blob";
			return std::nullopt;
		}
		return featureOff->GetBufferSize() == featureOn->GetBufferSize()
			&& std::memcmp(
				featureOff->GetBufferPointer(),
				featureOn->GetBufferPointer(),
				featureOff->GetBufferSize())
				== 0;
	}

	int ValidateFeatureOffIdentityPairs(
		const std::vector<ShaderCompileJob>& a_jobs,
		const std::vector<ShaderCompileResult>& a_results)
	{
		std::map<std::string, FeatureOffIdentityPair> pairs;
		int failures = 0;
		for (std::size_t index = 0; index < a_jobs.size(); ++index) {
			const auto& job = a_jobs[index];
			if (!job.featureOffIdentity)
				continue;

			const auto& identity = *job.featureOffIdentity;
			auto& pair = pairs[identity.key];
			if (identity.variant == FeatureIdentityVariant::kBase) {
				pair.wetnessShouldDiffer = identity.wetnessShouldDiffer;
				pair.terrainShouldDiffer = identity.terrainShouldDiffer;
				pair.inverseSquareShouldDiffer =
					identity.inverseSquareShouldDiffer;
				pair.expectTerrainVariant = identity.expectTerrainVariant;
				pair.expectInverseSquareVariant =
					identity.expectInverseSquareVariant;
				pair.hasExpectation = true;
			}

			const ShaderCompileJob** pairedJob = nullptr;
			const ShaderCompileResult** pairedResult = nullptr;
			const char* variantName = "feature-off";
			switch (identity.variant) {
			case FeatureIdentityVariant::kBase:
				pairedJob = &pair.featureOffJob;
				pairedResult = &pair.featureOffResult;
				break;
			case FeatureIdentityVariant::kWetness:
				pairedJob = &pair.wetnessJob;
				pairedResult = &pair.wetnessResult;
				variantName = "wetness";
				break;
			case FeatureIdentityVariant::kTerrainShadows:
				pairedJob = &pair.terrainJob;
				pairedResult = &pair.terrainResult;
				variantName = "terrain-shadows";
				break;
			case FeatureIdentityVariant::kInverseSquareLighting:
				pairedJob = &pair.inverseSquareJob;
				pairedResult = &pair.inverseSquareResult;
				variantName = "inverse-square-lighting";
				break;
			}
			if (*pairedJob) {
				std::printf(
					"FAIL: feature-off identity %s has duplicate %s variants\n",
					identity.key.c_str(),
					variantName);
				++failures;
				continue;
			}
			*pairedJob = &job;
			*pairedResult = &a_results[index];
		}

		const auto compareVariant = [&failures](
			const std::string& a_key,
			const char* a_variantName,
			const FeatureOffIdentityPair& a_pair,
			const ShaderCompileJob* a_variantJob,
			const ShaderCompileResult* a_variantResult,
			bool a_shouldDiffer) {
			if (!a_variantJob) {
				std::printf(
					"FAIL: feature-off identity %s is missing a %s variant\n",
					a_key.c_str(),
					a_variantName);
				++failures;
				return;
			}
			if (!a_pair.featureOffResult->error.empty()
				|| !a_variantResult->error.empty()) {
				return;
			}
			std::string error;
			const auto equal = HaveEqualStrippedShaderBytes(
				a_pair.featureOffResult->blob.Get(),
				a_variantResult->blob.Get(),
				error);
			if (!equal) {
				std::printf(
					"FAIL: feature-off identity %s (%s)\n%s\n",
					a_key.c_str(),
					a_variantName,
					error.c_str());
				++failures;
				return;
			}
			if (*equal == a_shouldDiffer) {
				std::printf(
					"FAIL: feature-off identity %s (%s) expected stripped DXBC to %s\n",
					a_key.c_str(),
					a_variantName,
					a_shouldDiffer ? "differ" : "match");
				++failures;
			}
		};

		for (const auto& [key, pair] : pairs) {
			if (!pair.hasExpectation || !pair.featureOffJob) {
				std::printf(
					"FAIL: feature-off identity %s is missing a feature-off variant\n",
					key.c_str());
				++failures;
				continue;
			}
			compareVariant(
				key,
				"wetness",
				pair,
				pair.wetnessJob,
				pair.wetnessResult,
				pair.wetnessShouldDiffer);
			if (pair.expectTerrainVariant) {
				compareVariant(
					key,
					"terrain-shadows",
					pair,
					pair.terrainJob,
					pair.terrainResult,
					pair.terrainShouldDiffer);
			} else if (pair.terrainJob) {
				std::printf(
					"FAIL: feature-off identity %s carries an unexpected terrain-shadows variant\n",
					key.c_str());
				++failures;
			}
			if (pair.expectInverseSquareVariant) {
				compareVariant(
					key,
					"inverse-square-lighting",
					pair,
					pair.inverseSquareJob,
					pair.inverseSquareResult,
					pair.inverseSquareShouldDiffer);
			} else if (pair.inverseSquareJob) {
				std::printf(
					"FAIL: feature-off identity %s carries an unexpected inverse-square variant\n",
					key.c_str());
				++failures;
			}
		}
		return failures;
	}

	int CompileAll(const std::vector<ShaderCompileJob>& a_jobs)
	{
		if (a_jobs.empty())
			return 0;

		std::vector<ShaderCompileResult> results(a_jobs.size());
		std::atomic_size_t nextJob{ 0 };
		const auto hardwareThreads =
			std::max(1u, std::thread::hardware_concurrency());
		const auto workerCount = std::min({
			a_jobs.size(),
			static_cast<std::size_t>(hardwareThreads),
			std::size_t{ 16 }
		});
		{
			std::vector<std::jthread> workers;
			workers.reserve(workerCount);
			for (std::size_t worker = 0; worker < workerCount; ++worker) {
				workers.emplace_back([&a_jobs, &results, &nextJob] {
					for (;;) {
						const auto index =
							nextJob.fetch_add(
								1,
								std::memory_order_relaxed);
						if (index >= a_jobs.size())
							return;
						results[index] = Compile(a_jobs[index]);
					}
				});
			}
		}

		int failures = 0;
		for (std::size_t index = 0; index < a_jobs.size(); ++index) {
			if (results[index].error.empty())
				continue;
			std::printf(
				"FAIL: %s\n%s\n",
				a_jobs[index].description.c_str(),
				results[index].error.c_str());
			++failures;
		}
		failures += ValidateFeatureOffIdentityPairs(a_jobs, results);
		return failures;
	}

	std::size_t AddSharedDataProbes(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_root)
	{
		const auto probe = a_root / "SharedDataProbe.hlsl";
		AddCompile(
			a_jobs,
			probe,
			{},
			"ps_5_0",
			"main",
			"shared substrate inactive",
			SubstrateExpectation::kAbsent);
		AddCompile(
			a_jobs,
			probe,
			{ { "FO4CS_SUBSTRATE", "1" } },
			"ps_5_0",
			"main",
			"shared substrate active",
			SubstrateExpectation::kPresent);
		return 2;
	}

	constexpr std::size_t kScreenSpaceGIPermutations = 9;

	std::size_t AddScreenSpaceGI(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_root)	{
		const auto firstJob = a_jobs.size();
		const auto screenSpaceGiRoot = a_root / "ScreenSpaceGI" / "XeGTAO";

		// Non-decode permutations share one XeGTAOCB layout.
		AddCompile(a_jobs, screenSpaceGiRoot / "decode.cs.hlsl", {});

		const std::array<ShaderCase, 8> xegtaoCases{ {
			{ "prefilterDepths.cs.hlsl", {} },
			{ "prefilterRadiance.cs.hlsl", {} },
			{ "prefilterNormal.cs.hlsl", {} },
			{ "radianceDisocc.cs.hlsl", {} },
			{ "gi.cs.hlsl", {} },
			{ "gi.cs.hlsl", { { "SSGI_BOUNCE", "1" } } },
			{ "denoise.cs.hlsl", {} },
			{ "denoise.cs.hlsl", { { "SSGI_BOUNCE", "1" } } }
		} };
		for (const auto& shaderCase : xegtaoCases) {
			auto& job = AddCompile(
				a_jobs,
				screenSpaceGiRoot / shaderCase.path,
				shaderCase.defines);
			job.validateXeGTAOCB = true;
		}
		return a_jobs.size() - firstJob;
	}

	constexpr std::size_t kDynamicCubemapPermutations = 7;

	std::size_t AddDynamicCubemaps(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_root)
	{
		const auto firstJob = a_jobs.size();
		const auto root = a_root / "DynamicCubemaps";
		AddCompile(a_jobs, root / "UpdateCubemapCS.hlsl", {});
		AddCompile(
			a_jobs,
			root / "UpdateCubemapCS.hlsl",
			{ { "REFLECTIONS", "1" } });
		AddCompile(a_jobs, root / "InferCubemapCS.hlsl", {});
		AddCompile(
			a_jobs,
			root / "InferCubemapCS.hlsl",
			{ { "REFLECTIONS", "1" } });
		AddCompile(a_jobs, root / "SpecularIrradianceCS.hlsl", {});
		AddCompile(a_jobs, root / "BC6HEncodeCS.hlsl", {});
		AddCompile(a_jobs, root / "CubemapPreviewCS.hlsl", {});
		return a_jobs.size() - firstJob;
	}

	constexpr std::size_t kUpscalingPermutations = 7;

	std::size_t AddUpscaling(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_root)
	{
		const auto firstJob = a_jobs.size();
		const auto upscalingRoot = a_root / "Upscaling";

		const std::array<ShaderCase, 4> encodeCases{ {
			{ "EncodeTexturesCS.hlsl", { { "FO4CS_SUBSTRATE", "1" } } },
			{ "EncodeTexturesCS.hlsl", { { "FO4CS_SUBSTRATE", "1" }, { "DLSS", "" }, { "DEPTH_OUTPUT", "1" } } },
			{ "EncodeTexturesCS.hlsl", { { "FO4CS_SUBSTRATE", "1" }, { "FSR", "" }, { "DEPTH_OUTPUT", "1" } } },
			{ "EncodeTexturesCS.hlsl", { { "FO4CS_SUBSTRATE", "1" }, { "DEPTH_OUTPUT", "" } } }
		} };
		for (const auto& shaderCase : encodeCases) {
			AddCompile(
				a_jobs,
				upscalingRoot / shaderCase.path,
				shaderCase.defines,
				"cs_5_0",
				"main",
				"upscaling encode");
		}

		AddCompile(
			a_jobs,
			upscalingRoot / "DepthRefractionUpscalePS.hlsl",
			{ { "PSHADER", "" }, { "FO4CS_SUBSTRATE", "1" } },
			"ps_5_0",
			"main",
			"upscaling depth");

		AddCompile(
			a_jobs,
			upscalingRoot / "UpscaleVS.hlsl",
			{ { "VSHADER", "" } },
			"vs_5_0",
			"main",
			"upscaling fullscreen");

		AddCompile(
			a_jobs,
			upscalingRoot / "RCAS" / "RCAS.hlsl",
			{},
			"cs_5_1",
			"main",
			"upscaling D3D12 sharpening");

		return a_jobs.size() - firstJob;
	}

	constexpr std::size_t kTerrainShadowsPermutations = 2;

	std::size_t AddTerrainShadows(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_root)
	{
		const auto firstJob = a_jobs.size();
		AddCompile(
			a_jobs,
			a_root / "TerrainShadows" / "ShadowUpdate.cs.hlsl",
			{},
			"cs_5_0",
			"main",
			"terrain shadow update");
		AddCompile(
			a_jobs,
			a_root / "TerrainShadows" / "ShadowStatistics.cs.hlsl",
			{},
			"cs_5_0",
			"main",
			"terrain shadow statistics");
		return a_jobs.size() - firstJob;
	}

	struct SlotExpectations
	{
		std::vector<UINT> requiredTextures;
		std::vector<UINT> forbiddenTextures;
		std::vector<UINT> requiredSamplers;
		std::vector<UINT> forbiddenSamplers;
	};

	void AttachBaselineIdentity(
		const BaselineShaderCase& a_registration,
		ShaderCompileJob& a_job)
	{
		a_job.strippedIdentity = a_registration.expected;
	}

	void AddRegistration(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_root,
		const BaselineShaderCase& a_registration,
		const ShaderDefines& a_contributorDefines,
		std::set<std::string>* a_uniqueInputs = nullptr,
		SlotExpectations a_slots = {},
		std::optional<FeatureOffIdentityExpectation> a_featureOffIdentity = {})
	{
		if (!a_registration.preparationError.empty()) {
			AddPreparationFailure(
				a_jobs,
				"registration " + a_registration.name,
				a_registration.preparationError);
			return;
		}
		const auto* target =
			cs::engine::GetShaderInjectionTarget(
				a_registration.targetId);
		if (!target) {
			AddPreparationFailure(
				a_jobs,
				"registration " + a_registration.name,
				"Registration target metadata is missing");
			return;
		}

		std::vector<cs::engine::ShaderReplacementRegistration>
			contributions;
		if (!a_contributorDefines.empty()) {
			cs::engine::ShaderReplacementRegistration contribution;
			contribution.targetId = a_registration.targetId;
			contribution.stages =
				cs::engine::ShaderStageBit(a_registration.stage);
			contribution.contributor = "ShaderCompile";
			for (const auto& [name, value] : a_contributorDefines)
				contribution.defines.emplace(name, value);
			contributions.push_back(std::move(contribution));
		}

		std::string compileTestError;
		const auto compileTestRequest =
			cs::engine::BuildEffectiveShaderCompileRequest(
				*target,
				a_registration.stage,
				a_registration.compilation,
				contributions,
				&compileTestError);
		if (!compileTestRequest) {
			AddPreparationFailure(
				a_jobs,
				"registration " + a_registration.name,
				"Effective compile request failed: "
					+ compileTestError);
			return;
		}

		auto defines = CopyShaderDefines(compileTestRequest->defines);
		const auto path = a_root / compileTestRequest->sourcePath;
		if (a_uniqueInputs) {
			a_uniqueInputs->insert(CompileInputKey(
				path,
				defines,
				compileTestRequest->profile.c_str(),
				compileTestRequest->entryPoint.c_str()));
		}
		auto& job = AddCompile(
			a_jobs,
			path,
			std::move(defines),
			compileTestRequest->profile.c_str(),
			compileTestRequest->entryPoint.c_str(),
			"registration " + a_registration.name);
		job.requiredTextureSlots = std::move(a_slots.requiredTextures);
		job.forbiddenTextureSlots = std::move(a_slots.forbiddenTextures);
		job.requiredSamplerSlots = std::move(a_slots.requiredSamplers);
		job.forbiddenSamplerSlots = std::move(a_slots.forbiddenSamplers);
		job.featureOffIdentity = std::move(a_featureOffIdentity);
		if (a_contributorDefines.empty())
			AttachBaselineIdentity(a_registration, job);
	}

	struct LightingCounts
	{
		std::size_t registrationDerived = 0;
		std::size_t uniqueRegistrationInputs = 0;
		std::size_t explicitPermutations = 0;
		std::size_t ambientCompositionRows = 0;
		std::size_t ambientNonTargetRows = 0;
		std::size_t wetnessDirectRows = 0;
		std::size_t wetnessDirectInertRows = 0;
		std::size_t terrainDirectRows = 0;
		std::size_t terrainDirectInertRows = 0;
		std::size_t terrainCompositeRows = 0;
		std::size_t terrainCompositeInertRows = 0;
		std::size_t terrainCompositeVertexRows = 0;
		std::size_t wetnessDebugCompositeRows = 0;
		std::size_t wetnessDebugCompositeInertRows = 0;
		std::size_t wetnessDebugCompositeVertexRows = 0;
		std::size_t wetnessCompositeRows = 0;
		std::size_t wetnessCompositeNeutralRows = 0;
		std::size_t wetnessCompositeVertexRows = 0;
		std::size_t inverseSquareRows = 0;
		std::size_t dfTiledLightingRows = 0;
		std::size_t inverseSquareTiledRows = 0;
		std::size_t inverseSquareInertRows = 0;
		std::size_t exponentialFogRows = 0;
	};

	// The SSGI composition extends the existing plugin texture block.
	constexpr std::array kCompositionTextureSlots{ 26u, 27u, 28u, 29u };
	constexpr UINT kGbufferNormalTextureSlot = 25;
	constexpr std::array kDynamicCubemapTextureSlots{ 16u, 17u };

	// only families that can isolate directional ambient carry the composition
	constexpr std::array kAmbientCompositionFamilies{
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY",
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY",
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY"
	};

	// the authoritative normal is declared by every family that darkens or coats
	constexpr std::array kWetnessCompositeFamilies{
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY",
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY",
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY",
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_MINIMAL_FAMILY",
		"BSDFCOMPOSITE_PS_2D_ACCUMULATOR",
		"BSDFCOMPOSITE_PS_2D_FOG",
		"BSDFCOMPOSITE_PS_CUBE_IBL"
	};
	constexpr std::array kDynamicCubemapCompositeFamilies{
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY",
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY",
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY",
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_MINIMAL_FAMILY",
		"BSDFCOMPOSITE_PS_CUBE_IBL"
	};
	constexpr std::array kWetnessDirectFamilies{
		"BSDFLIGHT_PS_DEFERRED",
		"BSDFLIGHT_PS_DIRSPLITS1",
		"BSDFLIGHT_PS_DIRSPLITS2",
		"BSDFLIGHT_PS_DIRSPLITS3",
		"BSDFLIGHT_PS_GOBO",
		"BSDFLIGHT_PS_UNSHADOWED"
	};
	constexpr std::array kTerrainDebugDepthFallbackFamilies{
		"BSDFCOMPOSITE_PS_2D_ACCUMULATOR",
		"BSDFCOMPOSITE_PS_NO_SRV_POSITION_TEXCOORD",
		"BSDFCOMPOSITE_PS_NO_SRV_POSITION",
		"BSDFCOMPOSITE_PS_NO_T0_ACCUMULATOR"
	};

	bool IsInverseSquareConsumer(
		const BaselineShaderCase&
			a_registration)
	{
		if (a_registration.targetId
				!= cs::engine::ShaderInjectionTarget::kBsdfLight
			|| a_registration.stage != cs::engine::ShaderStage::kPixel) {
			return false;
		}
		const auto& defines = a_registration.compilation.defines;
		if (defines.contains("BSDFLIGHT_PS_ATTENUATION_ONLY"))
			return true;
		if (defines.contains("BSDFLIGHT_PS_GOBO"))
			return defines.contains("POINTOMNI");
		if (defines.contains("BSDFLIGHT_PS_UNSHADOWED"))
			return defines.contains("POINTOMNI");
		if (!defines.contains("BSDFLIGHT_PS_DEFERRED"))
			return false;
		const auto lightType = defines.find("LIGHT_TYPE");
		return lightType != defines.end() && lightType->second != "1";
	}

	std::string TrimLeft(std::string_view a_text)
	{
		const auto first = a_text.find_first_not_of(" \t\r");
		return first == std::string_view::npos ?
			std::string{} :
			std::string(a_text.substr(first));
	}

	std::string NormalizeShaderSource(std::string_view a_source)
	{
		std::string normalized;
		normalized.reserve(a_source.size());
		for (const char character : a_source) {
			if (!std::isspace(static_cast<unsigned char>(character)))
				normalized.push_back(character);
		}
		return normalized;
	}

	bool HasVanillaHeightFogSignature(std::string_view a_source)
	{
		const auto source = NormalizeShaderSource(a_source);
		const bool hasHeightRemap =
			(source.contains("saturate(") && source.contains("[46].xy")
				&& source.contains("[46].zw"))
			|| source.contains(
				"FogHeightRamp.xy-FogHeightRamp.zw")
			|| source.contains(
				"FogHeightRampScaleBiasPair.xy-FogHeightRampScaleBiasPair.zw");
		const bool hasDistanceExtinction =
			(source.contains("pow(") && source.contains("[42].w"))
			|| source.contains(
				"pow(distanceFactor,FogNearLowColorAndPower.w)")
			|| source.contains(
				"pow(distanceFactor,FogNearLowColor_and_power.w)");
		return hasHeightRemap && hasDistanceExtinction;
	}

	bool IsPositiveExponentialFogGuard(std::string_view a_directive)
	{
		const auto directive = NormalizeShaderSource(a_directive);
		return directive == "#ifdefEXPONENTIAL_HEIGHT_FOG"
			|| directive == "#ifdefined(EXPONENTIAL_HEIGHT_FOG)"
			|| directive == "#ifdefinedEXPONENTIAL_HEIGHT_FOG";
	}

	bool HasGuardedExponentialFogTreatment(std::string_view a_source)
	{
		std::vector<bool> guards;
		bool hasEvaluate = false;
		bool hasHeightOverride = false;
		bool hasDistanceOverride = false;
		std::size_t offset = 0;
		while (offset <= a_source.size()) {
			const auto end = a_source.find('\n', offset);
			const auto line = a_source.substr(
				offset,
				end == std::string_view::npos ?
					a_source.size() - offset :
					end - offset);
			const auto trimmed = TrimLeft(line);
			if (trimmed.starts_with("#ifdef")
				|| trimmed.starts_with("#ifndef")
				|| trimmed.starts_with("#if ")) {
				guards.push_back(IsPositiveExponentialFogGuard(trimmed));
			} else if (trimmed.starts_with("#else")) {
				if (!guards.empty())
					guards.back() = false;
			} else if (trimmed.starts_with("#elif")) {
				if (!guards.empty())
					guards.back() = IsPositiveExponentialFogGuard(trimmed);
			} else if (trimmed.starts_with("#endif")) {
				if (!guards.empty())
					guards.pop_back();
			} else if (std::ranges::any_of(guards, std::identity{})) {
				const auto normalized = NormalizeShaderSource(line);
				hasEvaluate = hasEvaluate
					|| normalized.contains(
						"ExponentialHeightFog::TryEvaluate(");
				hasHeightOverride = hasHeightOverride
					|| normalized.contains("=exponentialHeight;");
				hasDistanceOverride = hasDistanceOverride
					|| normalized.contains("=exponentialDistance;")
					|| normalized.contains("=min(exponentialDistance,");
			}
			if (end == std::string_view::npos)
				break;
			offset = end + 1;
		}
		return hasEvaluate && hasHeightOverride && hasDistanceOverride;
	}

	struct FogFamilyBlock
	{
		std::string name;
		std::string source;
		std::size_t line = 0;
	};

	std::optional<std::string> ParseCompositeFamily(
		std::string_view a_directive)
	{
		constexpr std::string_view prefix = "BSDFCOMPOSITE_PS_";
		const auto position = a_directive.find(prefix);
		if (position == std::string_view::npos)
			return std::nullopt;
		const auto end = a_directive.find_first_of(
			" \t\r\n)", position);
		return std::string(a_directive.substr(
			position,
			end == std::string_view::npos ?
				a_directive.size() - position :
				end - position));
	}

	std::vector<FogFamilyBlock> FindFogFamilyBlocks(
		std::string_view a_source)
	{
		std::vector<std::pair<std::size_t, std::string_view>> lines;
		std::size_t offset = 0;
		while (offset <= a_source.size()) {
			const auto end = a_source.find('\n', offset);
			lines.emplace_back(
				offset,
				a_source.substr(
					offset,
					end == std::string_view::npos ?
						a_source.size() - offset :
						end - offset));
			if (end == std::string_view::npos)
				break;
			offset = end + 1;
		}

		std::vector<FogFamilyBlock> blocks;
		for (std::size_t index = 0; index < lines.size(); ++index) {
			const auto directive = TrimLeft(lines[index].second);
			if (!directive.starts_with("#if"))
				continue;
			const auto family = ParseCompositeFamily(directive);
			if (!family)
				continue;

			std::size_t depth = 1;
			std::size_t closing = index + 1;
			for (; closing < lines.size(); ++closing) {
				const auto nested = TrimLeft(lines[closing].second);
				if (nested.starts_with("#ifdef")
					|| nested.starts_with("#ifndef")
					|| nested.starts_with("#if ")) {
					++depth;
				} else if (nested.starts_with("#endif") && --depth == 0) {
					break;
				}
			}
			if (closing == lines.size())
				continue;
			const auto bodyStart = lines[index].first;
			const auto bodyEnd = lines[closing].first
				+ lines[closing].second.size();
			const auto body = a_source.substr(bodyStart, bodyEnd - bodyStart);
			if (HasVanillaHeightFogSignature(body)) {
				blocks.push_back({
					.name = *family,
					.source = std::string(body),
					.line = index + 1
				});
			}
			index = closing;
		}
		return blocks;
	}

	std::optional<std::string> ReadShaderSource(
		const std::filesystem::path& a_path,
		std::string& a_error)
	{
		std::ifstream stream(a_path, std::ios::binary);
		if (!stream) {
			a_error = "Could not open " + a_path.string();
			return std::nullopt;
		}
		std::string source{
			std::istreambuf_iterator<char>(stream),
			std::istreambuf_iterator<char>()
		};
		if (!stream.good() && !stream.eof()) {
			a_error = "Could not read " + a_path.string();
			return std::nullopt;
		}
		return source;
	}

	std::string RemoveIncludeDirectives(std::string_view a_source)
	{
		std::string result;
		result.reserve(a_source.size());
		std::size_t offset = 0;
		while (offset <= a_source.size()) {
			const auto end = a_source.find('\n', offset);
			const auto line = a_source.substr(
				offset,
				end == std::string_view::npos ?
					a_source.size() - offset :
					end - offset);
			if (!TrimLeft(line).starts_with("#include"))
				result.append(line);
			result.push_back('\n');
			if (end == std::string_view::npos)
				break;
			offset = end + 1;
		}
		return result;
	}

	std::optional<std::string> PreprocessShaderRoute(
		std::string_view a_source,
		const std::filesystem::path& a_path,
		const cs::engine::ShaderInjectionDefines& a_defines,
		std::string& a_error)
	{
		std::vector<D3D_SHADER_MACRO> macros;
		macros.reserve(a_defines.size() + 1);
		for (const auto& [name, value] : a_defines)
			macros.push_back({ name.c_str(), value.c_str() });
		macros.push_back({ nullptr, nullptr });

		const auto includeFreeSource = RemoveIncludeDirectives(a_source);
		Microsoft::WRL::ComPtr<ID3DBlob> preprocessed;
		Microsoft::WRL::ComPtr<ID3DBlob> errors;
		const auto result = D3DPreprocess(
			includeFreeSource.data(),
			includeFreeSource.size(),
			a_path.string().c_str(),
			macros.data(),
			nullptr,
			preprocessed.GetAddressOf(),
			errors.GetAddressOf());
		if (FAILED(result) || !preprocessed) {
			a_error = errors ?
				std::string(
					static_cast<const char*>(errors->GetBufferPointer()),
					errors->GetBufferSize()) :
				"D3DPreprocess failed";
			return std::nullopt;
		}
		return std::string(
			static_cast<const char*>(preprocessed->GetBufferPointer()),
			preprocessed->GetBufferSize());
	}

	constexpr UINT kTerrainShadowTextureSlot = 30;
	constexpr UINT kTerrainSceneDepthTextureSlot = 31;
	constexpr UINT kTerrainShadowSamplerSlot = 13;

	constexpr UINT kWaterCausticsTextureSlot = 32;
	constexpr UINT kWaterSceneDepthTextureSlot = 33;
	constexpr UINT kWaterCausticsSamplerSlot = 14;

	constexpr std::size_t kExpectedBaselineRegistrationRows = 1905;
	constexpr std::size_t kExpectedBaselineCompileInputs = 1834;
	constexpr std::size_t kExpectedEquivalentCompileInputGroups = 32;
	constexpr std::size_t kExpectedCanonicalIdentityRows = 11;
	constexpr std::size_t kExpectedAmbientCompositionRows = 26;
	constexpr std::size_t kExpectedAmbientNonTargetRows = 72;
	constexpr std::size_t kExpectedBsdfLightRows = 207;
	constexpr std::size_t kExpectedWetnessDirectRows = 167;
	constexpr std::size_t kExpectedWetnessDirectInertRows = 40;
	constexpr std::size_t kExpectedTerrainDirectRows = 114;
	constexpr std::size_t kExpectedTerrainDirectInertRows = 93;
	constexpr std::size_t kExpectedInverseSquareRows = 84;
	constexpr std::size_t kExpectedInverseSquareInertRows = 123;
	constexpr std::size_t kExpectedTerrainCompositeRows = 98;
	constexpr std::size_t kExpectedTerrainCompositeInertRows = 0;
	constexpr std::size_t kExpectedWetnessDebugCompositeRows = 98;
	constexpr std::size_t kExpectedWetnessDebugCompositeInertRows = 0;
	constexpr std::size_t kExpectedCompositeRegistrationRows = 102;
	constexpr std::size_t kExpectedWetnessCompositeRows = 61;
	constexpr std::size_t kExpectedWetnessCompositeNeutralRows = 37;
	constexpr std::size_t kExpectedWetnessCompositeVertexRows = 4;
	bool DeclaresFamily(
		const BaselineShaderCase& a_registration,
		std::span<const char* const> a_families)
	{
		return std::ranges::any_of(
			a_families,
			[&a_registration](const char* a_family) {
				return a_registration.compilation.defines.contains(a_family);
			});
	}

	bool HasDefine(const ShaderDefines& a_defines, std::string_view a_name)
	{
		return std::ranges::any_of(
			a_defines,
			[a_name](const auto& a_define) { return a_define.first == a_name; });
	}

	bool IsWetnessDirectConsumer(
		const BaselineShaderCase& a_registration)
	{
		return a_registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsdfLight
			&& a_registration.stage == cs::engine::ShaderStage::kPixel
			&& DeclaresFamily(a_registration, kWetnessDirectFamilies)
			&& !a_registration.compilation.defines.contains("ATTENUATION_ONLY");
	}

	bool IsWetnessCompositeConsumer(
		const BaselineShaderCase& a_registration)
	{
		return a_registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsdfComposite
			&& a_registration.stage == cs::engine::ShaderStage::kPixel
			&& DeclaresFamily(a_registration, kWetnessCompositeFamilies);
	}

	bool IsDynamicCubemapCompositeConsumer(
		const BaselineShaderCase&
			a_registration)
	{
		return a_registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsdfComposite
			&& a_registration.stage == cs::engine::ShaderStage::kPixel
			&& DeclaresFamily(
				a_registration, kDynamicCubemapCompositeFamilies);
	}

	bool IsDynamicCubemapForwardConsumer(
		const BaselineShaderCase&
			a_registration)
	{
		return a_registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsLighting
			&& a_registration.stage == cs::engine::ShaderStage::kPixel
			&& a_registration.compilation.defines.contains("BSL_ENVMAP");
	}

	bool IsDynamicCubemapWaterConsumer(
		const BaselineShaderCase&
			a_registration)
	{
		return a_registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsWater
			&& a_registration.stage == cs::engine::ShaderStage::kPixel
			&& a_registration.compilation.defines.contains("REFLECTIONS")
			&& !a_registration.compilation.defines.contains("FOG")
			&& !a_registration.compilation.defines.contains("SPECULAR")
			&& !a_registration.compilation.defines.contains("STENCIL")
			&& !a_registration.compilation.defines.contains(
				"STENCIL_DISPLACEMENT");
	}

	bool IsWetnessConsumer(
		const BaselineShaderCase& a_registration)
	{
		return IsWetnessDirectConsumer(a_registration)
			|| IsWetnessCompositeConsumer(a_registration);
	}

	bool IsTerrainShadowConsumer(
		const BaselineShaderCase& a_registration)
	{
		return a_registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsdfLight
			&& a_registration.stage == cs::engine::ShaderStage::kPixel
			&& a_registration.compilation.defines.contains("DIRECTIONAL");
	}

	bool IsTerrainDebugCompositeConsumer(
		const BaselineShaderCase& a_registration)
	{
		return a_registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsdfComposite
			&& a_registration.stage == cs::engine::ShaderStage::kPixel;
	}

	bool IsWetnessDebugCompositeConsumer(
		const BaselineShaderCase& a_registration)
	{
		return a_registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsdfComposite
			&& a_registration.stage == cs::engine::ShaderStage::kPixel;
	}

	bool UsesTerrainDebugDepthFallback(
		const BaselineShaderCase& a_registration)
	{
		return IsTerrainDebugCompositeConsumer(a_registration)
			&& DeclaresFamily(
				a_registration,
				kTerrainDebugDepthFallbackFamilies);
	}

	LightingCounts AddLighting(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_root,
		const std::filesystem::path& a_identityWitnessPath)
	{
		std::string witnessLoadError;
		const auto registrations =
			GetBaselineShaderCases(a_identityWitnessPath, witnessLoadError);
		if (!witnessLoadError.empty()) {
			AddPreparationFailure(
				a_jobs,
				"baseline identity witness data",
				std::move(witnessLoadError));
		} else if (registrations.empty()) {
			AddPreparationFailure(
				a_jobs,
				"shader replacement registrations",
				"No shader replacement registrations were discovered");
		}
		const auto compositePath = a_root / "BSDFCompositeShader.hlsl";
		std::string compositeSourceError;
		const auto compositeSource =
			ReadShaderSource(compositePath, compositeSourceError);
		std::vector<FogFamilyBlock> fogFamilyBlocks;
		std::set<std::string, std::less<>> fogFamilies;
		if (!compositeSource) {
			AddPreparationFailure(
				a_jobs,
				"BSDFComposite fog source inventory",
				compositeSourceError);
		} else {
			fogFamilyBlocks = FindFogFamilyBlocks(*compositeSource);
			if (fogFamilyBlocks.empty()) {
				AddPreparationFailure(
					a_jobs,
					"BSDFComposite fog source inventory",
					"No family block contains the vanilla height-remap and "
					"distance-extinction signature");
			}
			for (const auto& block : fogFamilyBlocks) {
				if (!fogFamilies.insert(block.name).second) {
					AddPreparationFailure(
						a_jobs,
						"BSDFComposite fog source inventory",
						"Family " + block.name
							+ " has more than one fog implementation block");
				}
				if (!HasGuardedExponentialFogTreatment(block.source)) {
					AddPreparationFailure(
						a_jobs,
						"BSDFComposite exponential fog treatment",
						"Family " + block.name + " at line "
							+ std::to_string(block.line)
							+ " does not guard both vanilla-term overrides");
				}
			}
		}
		std::set<std::string> uniqueRegistrationInputs;
		std::set<std::string> uniqueRegistrationRoutes;
		std::map<
			std::string,
			std::vector<const BaselineShaderCase*>>
			registrationInputRoutes;
		std::map<
			std::string,
			std::set<std::string>,
			std::less<>>
			aliasGroupRouteContracts;
		std::size_t dfTiledLightingRows = 0;
		std::size_t inverseSquareTiledRows = 0;
		std::size_t exponentialFogRows = 0;
		std::size_t canonicalIdentityRows = 0;
		for (const auto& registration : registrations) {
			if (registration.expectedStockSha1
				!= registration.expected.sha1) {
				++canonicalIdentityRows;
			}
			const auto registrationInputKey =
				BaselineCompileInputKey(a_root, registration);
			auto& inputRoutes =
				registrationInputRoutes[registrationInputKey];
			inputRoutes.push_back(&registration);
			if (!registration.compileInputAliasGroup.empty()) {
				aliasGroupRouteContracts[
					registration.compileInputAliasGroup]
					.insert(registration.routeKey);
			}
			const bool isCompileIdentityRepresentative =
				inputRoutes.size() == 1;
			uniqueRegistrationRoutes.insert(registration.routeKey);

			if (registration.targetId
				== cs::engine::ShaderInjectionTarget::kDfTiledLighting) {
				++dfTiledLightingRows;
				AddRegistration(
					a_jobs,
					a_root,
					registration,
					{
						{
							cs::engine::shader_injection_defines::
								kInverseSquareLighting,
							"1"
						}
					});
				if (registration.descriptor == 0)
					AttachBaselineIdentity(registration, a_jobs.back());
				else
					++inverseSquareTiledRows;
			}
			bool exponentialFogConsumer = false;
			if (compositeSource
				&& registration.targetId
					== cs::engine::ShaderInjectionTarget::kBsdfComposite
				&& registration.stage == cs::engine::ShaderStage::kPixel) {
				std::string preprocessError;
				const auto preprocessed = PreprocessShaderRoute(
					*compositeSource,
					compositePath,
					registration.compilation.defines,
					preprocessError);
				if (!preprocessed) {
					AddPreparationFailure(
						a_jobs,
						"BSDFComposite fog route inventory "
							+ registration.name,
						preprocessError);
				} else if (HasVanillaHeightFogSignature(*preprocessed)) {
					const auto familyCount = std::ranges::count_if(
						fogFamilies,
						[&registration](const std::string& a_family) {
							return registration.compilation.defines.contains(
								a_family);
						});
					if (familyCount != 1) {
						AddPreparationFailure(
							a_jobs,
							"BSDFComposite fog route family "
								+ registration.name,
							"An active fog route must select exactly one "
							"source-derived fog family");
					} else {
						exponentialFogConsumer = true;
					}
				}
			}
			if (exponentialFogConsumer) {
				const auto previousJobCount = a_jobs.size();
				AddRegistration(
					a_jobs,
					a_root,
					registration,
					{
						{
							cs::engine::shader_injection_defines::
								kExponentialHeightFog,
							"1"
						}
					});
				if (a_jobs.size() != previousJobCount + 1
					|| !HasDefine(
						a_jobs.back().defines,
						cs::engine::shader_injection_defines::
							kExponentialHeightFog)) {
					AddPreparationFailure(
						a_jobs,
						"BSDFComposite exponential fog delivery "
							+ registration.name,
						"The effective compile request did not receive "
						"EXPONENTIAL_HEIGHT_FOG");
				}
				++exponentialFogRows;
			}
			std::optional<FeatureOffIdentityExpectation> identity;
			const bool directRow = registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsdfLight;
			if (isCompileIdentityRepresentative
				&& (directRow
				|| registration.targetId
					== cs::engine::ShaderInjectionTarget::kBsdfComposite)) {
				identity = FeatureOffIdentityExpectation{
					.key = registrationInputKey,
					.variant = FeatureIdentityVariant::kBase,
					.wetnessShouldDiffer = IsWetnessConsumer(registration),
					.terrainShouldDiffer =
						IsTerrainShadowConsumer(registration)
						|| IsTerrainDebugCompositeConsumer(registration),
					.inverseSquareShouldDiffer =
						IsInverseSquareConsumer(registration),
					.expectTerrainVariant = true,
					.expectInverseSquareVariant = directRow
				};
			}
			AddRegistration(
				a_jobs,
				a_root,
				registration,
				{},
				&uniqueRegistrationInputs,
				{},
				std::move(identity));
		}
		if (registrations.size() != kExpectedBaselineRegistrationRows
			|| uniqueRegistrationRoutes.size() != registrations.size()) {
			AddPreparationFailure(
				a_jobs,
				"base registration route coverage",
				"Expected "
					+ std::to_string(kExpectedBaselineRegistrationRows)
					+ " distinct target/stage/descriptor routes, found "
					+ std::to_string(registrations.size()) + " rows and "
					+ std::to_string(uniqueRegistrationRoutes.size())
					+ " distinct routes");
		}
		if (canonicalIdentityRows != kExpectedCanonicalIdentityRows) {
			AddPreparationFailure(
				a_jobs,
				"base registration canonical identities",
				"Expected "
					+ std::to_string(kExpectedCanonicalIdentityRows)
					+ " approved canonical identities, found "
					+ std::to_string(canonicalIdentityRows));
		}
		if (uniqueRegistrationInputs.size()
			!= kExpectedBaselineCompileInputs) {
			AddPreparationFailure(
				a_jobs,
				"base registration compile inputs",
				"Expected "
					+ std::to_string(kExpectedBaselineCompileInputs)
					+ " unique inputs, found "
					+ std::to_string(
						uniqueRegistrationInputs.size()));
		}
		if (aliasGroupRouteContracts.size()
			!= kExpectedEquivalentCompileInputGroups) {
			AddPreparationFailure(
				a_jobs,
				"base registration alias contracts",
				"Expected "
					+ std::to_string(
						kExpectedEquivalentCompileInputGroups)
					+ " witness-declared alias groups, found "
					+ std::to_string(aliasGroupRouteContracts.size()));
		}
		std::set<std::string, std::less<>> witnessedAliasGroups;
		std::size_t equivalentCompileInputGroups = 0;
		for (const auto& [input, routes] : registrationInputRoutes) {
			if (routes.size() == 1)
				continue;
			++equivalentCompileInputGroups;
			std::set<std::string> routeKeys;
			std::set<std::string> aliasGroups;
			for (const auto* route : routes) {
				routeKeys.insert(route->routeKey);
				aliasGroups.insert(route->compileInputAliasGroup);
			}
			const auto aliasContract =
				aliasGroups.size() == 1
						&& !aliasGroups.begin()->empty() ?
					aliasGroupRouteContracts.find(*aliasGroups.begin()) :
					aliasGroupRouteContracts.end();
			const bool exactAliasContract =
				aliasContract != aliasGroupRouteContracts.end()
				&& routeKeys == aliasContract->second;
			const auto& expected = routes.front()->expected;
			const auto& expectedStockSha1 =
				routes.front()->expectedStockSha1;
			const bool identicalStockIdentities = std::ranges::all_of(
				routes,
				[&](const BaselineShaderCase* a_route) {
					return a_route->expectedStockSha1
							== expectedStockSha1
						&& a_route->expected.byteLength
							== expected.byteLength
						&& a_route->expected.sha1 == expected.sha1
						&& a_route->expected.sha256 == expected.sha256;
				});
			if (!exactAliasContract || !identicalStockIdentities) {
				AddPreparationFailure(
					a_jobs,
					"base registration equivalent input " + input,
					"Shared compile inputs must exactly match one "
					"witness-declared route group and stock identity");
			} else {
				witnessedAliasGroups.insert(aliasContract->first);
			}
		}
		for (const auto& [group, routeKeys] : aliasGroupRouteContracts) {
			if (routeKeys.size() < 2
				|| !witnessedAliasGroups.contains(group)) {
				AddPreparationFailure(
					a_jobs,
					"base registration equivalent input group "
						+ std::string(group),
					"Exact witness-declared members did not share one "
					"compile input and stock identity");
			}
		}
		if (equivalentCompileInputGroups
			!= kExpectedEquivalentCompileInputGroups) {
			AddPreparationFailure(
				a_jobs,
				"base registration equivalent input groups",
				"Expected "
					+ std::to_string(
						kExpectedEquivalentCompileInputGroups)
					+ " proven alias groups, found "
					+ std::to_string(equivalentCompileInputGroups));
		}
		if (dfTiledLightingRows != 3) {
			AddPreparationFailure(
				a_jobs,
				"DFTiledLighting registration coverage",
				"Expected "
					+ std::to_string(3)
					+ " depth-bounds and final-kernel routes, found "
					+ std::to_string(dfTiledLightingRows));
		}
		if (inverseSquareTiledRows != 2) {
			AddPreparationFailure(
				a_jobs,
				"DFTiledLighting inverse-square coverage",
				"Expected "
					+ std::to_string(2)
					+ " contributed final-kernel routes, found "
					+ std::to_string(inverseSquareTiledRows));
		}
		const std::array<ShaderCase, 3> featureCompositionCases{ {
			{
				"BSDFLightShader.hlsl",
				{
					{ "BSDFLIGHT_PS_DEFERRED", "1" },
					{ "LIGHT_TYPE", "1" },
					{
						cs::engine::shader_injection_defines::
							kScreenSpaceShadows,
						"1"
					}
				},
				"ps_5_0"
			},
			{
				"BSDFLightShader.hlsl",
				{
					{ "AMBIENT_IBL_IN_LIGHT", "1" },
					{ "BSDFLIGHT_PS_DEFERRED", "1" },
					{ "LIGHT_TYPE", "1" }
				},
				"ps_5_0"
			},
			{
				"BSDFLightShader.hlsl",
				{
					{ "AMBIENT_IBL_IN_LIGHT", "1" },
					{ "BSDFLIGHT_PS_DEFERRED", "1" },
					{ "LIGHT_TYPE", "1" },
					{
						cs::engine::shader_injection_defines::
							kScreenSpaceShadows,
						"1"
					}
				},
				"ps_5_0"
			}
		} };
		std::array<ShaderCase, 8> terrainSssDebugCases;
		std::array<ShaderCase, 8> wetnessSssDebugCases;
		for (std::size_t index = 0; index < 4; ++index) {
			const auto shape = std::to_string(index + 1);
			terrainSssDebugCases[index * 2] = {
				"BSDFCompositeShader.hlsl",
				{
					{ "FO4CS_SUBSTRATE", "1" },
					{ "TERRAIN_SHADOWS", "1" },
					{ "TERRAIN_SHADOWS_FULLSCREEN_DEBUG", "1" },
					{ "BSDFCOMPOSITE_PS_SSS_MRT_RECORD_NORMAL", "1" },
					{ "WAVE5B_SSS_RECORD_NORMAL_SHAPE", shape }
				},
				"ps_5_0"
			};
			terrainSssDebugCases[index * 2 + 1] = {
				"BSDFCompositeShader.hlsl",
				{
					{ "FO4CS_SUBSTRATE", "1" },
					{ "TERRAIN_SHADOWS", "1" },
					{ "TERRAIN_SHADOWS_FULLSCREEN_DEBUG", "1" },
					{ "BSDFCOMPOSITE_PS_SSS_MRT_SURFACE_CONTACT", "1" },
					{ "WAVE5B_SSS_SURFACE_CONTACT_SHAPE", shape }
				},
				"ps_5_0"
			};
			wetnessSssDebugCases[index * 2] = {
				"BSDFCompositeShader.hlsl",
				{
					{ "FO4CS_SUBSTRATE", "1" },
					{ "WETNESS_EFFECTS", "1" },
					{ "WETNESS_EFFECTS_FULLSCREEN_DEBUG", "1" },
					{ "BSDFCOMPOSITE_PS_SSS_MRT_RECORD_NORMAL", "1" },
					{ "WAVE5B_SSS_RECORD_NORMAL_SHAPE", shape }
				},
				"ps_5_0"
			};
			wetnessSssDebugCases[index * 2 + 1] = {
				"BSDFCompositeShader.hlsl",
				{
					{ "FO4CS_SUBSTRATE", "1" },
					{ "WETNESS_EFFECTS", "1" },
					{ "WETNESS_EFFECTS_FULLSCREEN_DEBUG", "1" },
					{ "BSDFCOMPOSITE_PS_SSS_MRT_SURFACE_CONTACT", "1" },
					{ "WAVE5B_SSS_SURFACE_CONTACT_SHAPE", shape }
				},
				"ps_5_0"
			};
		}
		const auto compileCases =
			[&a_jobs, &a_root](const auto& a_cases) {
			for (const auto& shader : a_cases) {
				AddCompile(
					a_jobs,
					a_root / shader.path,
					shader.defines,
					shader.profile,
					shader.entryPoint);
			}
		};
		compileCases(featureCompositionCases);
		for (const auto& shader : terrainSssDebugCases) {
			auto& job = AddCompile(
				a_jobs,
				a_root / shader.path,
				shader.defines,
				shader.profile,
				shader.entryPoint);
			job.requiredTextureSlots = {
				kTerrainShadowTextureSlot
			};
			job.forbiddenTextureSlots = { kTerrainSceneDepthTextureSlot };
			job.requiredSamplerSlots = { kTerrainShadowSamplerSlot };
		}
		for (const auto& shader : wetnessSssDebugCases) {
			auto& job = AddCompile(
				a_jobs,
				a_root / shader.path,
				shader.defines,
				shader.profile,
				shader.entryPoint);
			job.requiredTextureSlots = { kGbufferNormalTextureSlot };
		}

		using namespace cs::engine::shader_injection_defines;
		const std::array<ShaderDefines, 8> directionalCompositions{ {
			{ { kScreenSpaceShadows, "1" } },
			{ { kTerrainShadows, "1" } },
			{ { kWetnessEffects, "1" } },
			{ { kWaterEffects, "1" } },
			{
				{ kScreenSpaceShadows, "1" },
				{ kTerrainShadows, "1" }
			},
			{
				{ kScreenSpaceShadows, "1" },
				{ kWetnessEffects, "1" }
			},
			{
				{ kTerrainShadows, "1" },
				{ kWaterEffects, "1" }
			},
			{
				{ kScreenSpaceShadows, "1" },
				{ kTerrainShadows, "1" },
				{ kWetnessEffects, "1" },
				{ kWaterEffects, "1" }
			}
		} };
		const std::array<ShaderDefines, 3> ambientCompositions{ {
			{ { kScreenSpaceGi, "1" } },
			{ { kWetnessEffects, "1" } },
			{
				{ kScreenSpaceGi, "1" },
				{ kWetnessEffects, "1" }
			}
		} };
		const std::array<ShaderDefines, 2> dynamicCubemapCompositions{ {
			{
				{ kDynamicCubemaps, "1" },
				{ kDynamicCubemapsFullscreenDebug, "1" }
			},
			{
				{ kDynamicCubemaps, "1" },
				{ kDynamicCubemapsFullscreenDebug, "1" },
				{ kWetnessEffects, "1" }
			}
		} };
		const std::array<ShaderDefines, 2> inverseSquareCompositions{ {
			{ { kInverseSquareLighting, "1" } },
			{
				{ kInverseSquareLighting, "1" },
				{ kScreenSpaceShadows, "1" },
				{ kTerrainShadows, "1" },
				{ kWetnessEffects, "1" }
			}
		} };
		std::size_t contributorCompositionCount = 0;
		std::size_t ambientCompositionRows = 0;
		std::size_t ambientNonTargetRows = 0;
		std::size_t wetnessDirectRows = 0;
		std::size_t wetnessDirectInertRows = 0;
		std::size_t terrainDirectRows = 0;
		std::size_t terrainDirectInertRows = 0;
		std::size_t terrainCompositeRows = 0;
		std::size_t terrainCompositeInertRows = 0;
		std::size_t terrainCompositeVertexRows = 0;
		std::size_t wetnessDebugCompositeRows = 0;
		std::size_t wetnessDebugCompositeInertRows = 0;
		std::size_t wetnessDebugCompositeVertexRows = 0;
		std::size_t wetnessCompositeRows = 0;
		std::size_t wetnessCompositeNeutralRows = 0;
		std::size_t wetnessCompositeVertexRows = 0;
		std::size_t inverseSquareRows = 0;
		std::size_t inverseSquareInertRows = 0;
		std::set<std::string> preparedFeatureInputs;
		for (const auto& registration : registrations) {
			const auto registrationInputKey =
				BaselineCompileInputKey(a_root, registration);
			const bool prepareFeaturePermutations =
				preparedFeatureInputs.insert(registrationInputKey).second;
			if (registration.stage == cs::engine::ShaderStage::kPixel &&
				(registration.targetId
						== cs::engine::ShaderInjectionTarget::kBsLighting ||
				 registration.targetId
						== cs::engine::ShaderInjectionTarget::kBsWater)) {
				SlotExpectations slots;
				const bool consumesDynamicCubemaps =
					IsDynamicCubemapForwardConsumer(registration) ||
					IsDynamicCubemapWaterConsumer(registration);
				auto& cubemapSlots = consumesDynamicCubemaps ?
					slots.requiredTextures :
					slots.forbiddenTextures;
				cubemapSlots.assign(
					kDynamicCubemapTextureSlots.begin(),
					kDynamicCubemapTextureSlots.end());
				AddRegistration(
					a_jobs,
					a_root,
					registration,
					{ { kDynamicCubemaps, "1" } },
					nullptr,
					std::move(slots));
				++contributorCompositionCount;
			}

			if (registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsdfLight) {
				if (IsWetnessDirectConsumer(registration))
					++wetnessDirectRows;
				else
					++wetnessDirectInertRows;
				if (IsTerrainShadowConsumer(registration))
					++terrainDirectRows;
				else
					++terrainDirectInertRows;
				if (IsInverseSquareConsumer(registration))
					++inverseSquareRows;
				else
					++inverseSquareInertRows;

				if (prepareFeaturePermutations) {
					for (const auto& defines :
						inverseSquareCompositions) {
						const bool consumesTerrain =
							IsTerrainShadowConsumer(registration);
						SlotExpectations slots;
						slots.forbiddenTextures.push_back(
							kGbufferNormalTextureSlot);
						const bool terrainOn =
							HasDefine(defines, kTerrainShadows);
						auto& terrainTextures =
							terrainOn && consumesTerrain ?
								slots.requiredTextures :
								slots.forbiddenTextures;
						terrainTextures.push_back(
							kTerrainShadowTextureSlot);
						slots.forbiddenTextures.push_back(
							kTerrainSceneDepthTextureSlot);
						auto& terrainSamplers =
							terrainOn && consumesTerrain ?
								slots.requiredSamplers :
								slots.forbiddenSamplers;
						terrainSamplers.push_back(
							kTerrainShadowSamplerSlot);
						std::optional<FeatureOffIdentityExpectation>
							identity;
						if (defines.size() == 1) {
							identity = FeatureOffIdentityExpectation{
								.key = registrationInputKey,
								.variant = FeatureIdentityVariant::
									kInverseSquareLighting
							};
						}
						AddRegistration(
							a_jobs,
							a_root,
							registration,
							defines,
							nullptr,
							std::move(slots),
							std::move(identity));
						++contributorCompositionCount;
					}
				}
			}

			const auto* compositions =
				registration.targetId
						== cs::engine::ShaderInjectionTarget::kBsdfLight
					&& prepareFeaturePermutations
				? &directionalCompositions
				: nullptr;
			if (compositions) {
				const bool consumesTerrain = IsTerrainShadowConsumer(registration);
				for (const auto& defines : *compositions) {
					// the authoritative normal belongs to composite rows only
					std::optional<FeatureOffIdentityExpectation> identity;
					if (defines.size() == 1) {
						if (HasDefine(defines, kWetnessEffects)) {
							identity = FeatureOffIdentityExpectation{
								.key = registrationInputKey,
								.variant = FeatureIdentityVariant::kWetness
							};
						} else if (HasDefine(defines, kTerrainShadows)) {
							identity = FeatureOffIdentityExpectation{
								.key = registrationInputKey,
								.variant = FeatureIdentityVariant::kTerrainShadows
							};
						}
					}
					SlotExpectations slots;
					slots.forbiddenTextures.push_back(kGbufferNormalTextureSlot);
					const bool terrainOn = HasDefine(defines, kTerrainShadows);
					auto& terrainTextures = terrainOn && consumesTerrain ?
						slots.requiredTextures :
						slots.forbiddenTextures;
					terrainTextures.push_back(kTerrainShadowTextureSlot);
					slots.forbiddenTextures.push_back(
						kTerrainSceneDepthTextureSlot);
					auto& terrainSamplers = terrainOn && consumesTerrain ?
						slots.requiredSamplers :
						slots.forbiddenSamplers;
					terrainSamplers.push_back(kTerrainShadowSamplerSlot);
					const bool waterOn = HasDefine(defines, kWaterEffects);
					auto& waterTextures = waterOn && consumesTerrain ?
						slots.requiredTextures :
						slots.forbiddenTextures;
					waterTextures.push_back(kWaterCausticsTextureSlot);
					slots.forbiddenTextures.push_back(
						kWaterSceneDepthTextureSlot);
					auto& waterSamplers = waterOn && consumesTerrain ?
						slots.requiredSamplers :
						slots.forbiddenSamplers;
					waterSamplers.push_back(kWaterCausticsSamplerSlot);
					AddRegistration(
						a_jobs,
						a_root,
						registration,
						defines,
						nullptr,
						std::move(slots),
						std::move(identity));
					++contributorCompositionCount;
				}
			}
			if (registration.targetId
				!= cs::engine::ShaderInjectionTarget::kBsdfComposite) {
				continue;
			}

			const bool pixelRow =
				registration.stage == cs::engine::ShaderStage::kPixel;
			const bool composesAmbient = pixelRow
				&& DeclaresFamily(registration, kAmbientCompositionFamilies);
			const bool composesWetness = pixelRow
				&& DeclaresFamily(registration, kWetnessCompositeFamilies);
			const bool composesDynamicCubemaps =
				IsDynamicCubemapCompositeConsumer(registration);
			const bool consumesTerrainDebug =
				IsTerrainDebugCompositeConsumer(registration);
			const bool consumesWetnessDebug =
				IsWetnessDebugCompositeConsumer(registration);
			if (pixelRow) {
				if (composesAmbient)
					++ambientCompositionRows;
				else
					++ambientNonTargetRows;
				if (composesWetness)
					++wetnessCompositeRows;
				else
					++wetnessCompositeNeutralRows;
				if (consumesTerrainDebug)
					++terrainCompositeRows;
				else
					++terrainCompositeInertRows;
				if (consumesWetnessDebug)
					++wetnessDebugCompositeRows;
				else
					++wetnessDebugCompositeInertRows;
			} else if (registration.stage == cs::engine::ShaderStage::kVertex) {
				++wetnessCompositeVertexRows;
				++terrainCompositeVertexRows;
				++wetnessDebugCompositeVertexRows;
			} else {
				AddPreparationFailure(
					a_jobs,
					"unexpected kBsdfComposite stage",
					"Registration " + registration.name
						+ " is neither pixel nor vertex");
			}
			if (!prepareFeaturePermutations)
				continue;

			for (const auto& defines : ambientCompositions) {
				const bool screenSpaceGi = HasDefine(defines, kScreenSpaceGi);
				const bool wetnessEffects = HasDefine(defines, kWetnessEffects);
				SlotExpectations slots;
				if (pixelRow) {
					auto& compositionSlots = screenSpaceGi && composesAmbient ?
						slots.requiredTextures :
						slots.forbiddenTextures;
					compositionSlots.assign(
						kCompositionTextureSlots.begin(),
						kCompositionTextureSlots.end());
				}
				auto& normalSlot = wetnessEffects && composesWetness ?
					slots.requiredTextures :
					slots.forbiddenTextures;
				normalSlot.push_back(kGbufferNormalTextureSlot);
				if (pixelRow
					&& registration.compilation.defines.contains(
						"BSDFCOMPOSITE_PS_2D_ACCUMULATOR")) {
					slots.forbiddenTextures.push_back(7);
				}
				std::optional<FeatureOffIdentityExpectation> identity;
				if (defines.size() == 1 && wetnessEffects) {
					identity = FeatureOffIdentityExpectation{
						.key = registrationInputKey,
						.variant = FeatureIdentityVariant::kWetness
					};
				}
				AddRegistration(
					a_jobs,
					a_root,
					registration,
					defines,
					nullptr,
					std::move(slots),
					std::move(identity));
				++contributorCompositionCount;

			}

			for (const auto& defines : dynamicCubemapCompositions) {
				const bool wetnessEffects =
					HasDefine(defines, kWetnessEffects);
				SlotExpectations slots;
				auto& cubemapSlots =
					composesDynamicCubemaps ?
						slots.requiredTextures :
						slots.forbiddenTextures;
				cubemapSlots.assign(
					kDynamicCubemapTextureSlots.begin(),
					kDynamicCubemapTextureSlots.end());
				auto& normalSlot =
					wetnessEffects && composesWetness ?
						slots.requiredTextures :
						slots.forbiddenTextures;
				normalSlot.push_back(kGbufferNormalTextureSlot);
				AddRegistration(
					a_jobs,
					a_root,
					registration,
					defines,
					nullptr,
					std::move(slots));
				++contributorCompositionCount;
			}

			SlotExpectations wetnessDebugSlots;
			auto& wetnessDebugTextures = consumesWetnessDebug ?
				wetnessDebugSlots.requiredTextures :
				wetnessDebugSlots.forbiddenTextures;
			wetnessDebugTextures.push_back(kGbufferNormalTextureSlot);
			AddRegistration(
				a_jobs,
				a_root,
				registration,
				{
					{ kWetnessEffects, "1" },
					{ kWetnessEffectsFullscreenDebug, "1" }
				},
				nullptr,
				std::move(wetnessDebugSlots));
			++contributorCompositionCount;

			SlotExpectations terrainSlots;
			auto& terrainTextures = consumesTerrainDebug ?
				terrainSlots.requiredTextures :
				terrainSlots.forbiddenTextures;
			terrainTextures.push_back(kTerrainShadowTextureSlot);
			if (consumesTerrainDebug) {
				auto& terrainDepthTextures =
					UsesTerrainDebugDepthFallback(registration) ?
					terrainSlots.requiredTextures :
					terrainSlots.forbiddenTextures;
				terrainDepthTextures.push_back(
					kTerrainSceneDepthTextureSlot);
				terrainSlots.requiredSamplers.push_back(
					kTerrainShadowSamplerSlot);
			} else {
				terrainSlots.forbiddenTextures.push_back(
					kTerrainSceneDepthTextureSlot);
			}
			AddRegistration(
				a_jobs,
				a_root,
				registration,
				{
					{ kTerrainShadows, "1" },
					{ kTerrainShadowsFullscreenDebug, "1" }
				},
				nullptr,
				std::move(terrainSlots),
				FeatureOffIdentityExpectation{
					.key = registrationInputKey,
					.variant = FeatureIdentityVariant::kTerrainShadows
				});
			++contributorCompositionCount;

			SlotExpectations waterSlots;
			auto& waterTextures = consumesTerrainDebug ?
				waterSlots.requiredTextures :
				waterSlots.forbiddenTextures;
			waterTextures.push_back(kWaterCausticsTextureSlot);
			auto& waterDepthTextures =
				consumesTerrainDebug
					&& UsesTerrainDebugDepthFallback(registration) ?
				waterSlots.requiredTextures :
				waterSlots.forbiddenTextures;
			waterDepthTextures.push_back(kWaterSceneDepthTextureSlot);
			AddRegistration(
				a_jobs,
				a_root,
				registration,
				{
					{ kWaterEffects, "1" },
					{ kWaterEffectsFullscreenDebug, "1" }
				},
				nullptr,
				std::move(waterSlots));
			++contributorCompositionCount;
		}
		if (ambientCompositionRows != kExpectedAmbientCompositionRows
			|| ambientNonTargetRows != kExpectedAmbientNonTargetRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsdfComposite ambient composition coverage",
				"Expected "
					+ std::to_string(kExpectedAmbientCompositionRows)
					+ " composing and "
					+ std::to_string(kExpectedAmbientNonTargetRows)
					+ " non-target pixel rows, found "
					+ std::to_string(ambientCompositionRows)
					+ " and "
					+ std::to_string(ambientNonTargetRows));
		}
		if (wetnessCompositeRows != kExpectedWetnessCompositeRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsdfComposite wetness coverage",
				"Expected "
					+ std::to_string(kExpectedWetnessCompositeRows)
					+ " wetness pixel rows, found "
					+ std::to_string(wetnessCompositeRows));
		}
		if (wetnessDirectRows != kExpectedWetnessDirectRows
			|| wetnessDirectInertRows != kExpectedWetnessDirectInertRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsdfLight wetness coverage",
				"Expected "
					+ std::to_string(kExpectedWetnessDirectRows)
					+ " wetness and "
					+ std::to_string(kExpectedWetnessDirectInertRows)
					+ " inert rows, found "
					+ std::to_string(wetnessDirectRows)
					+ " and "
					+ std::to_string(wetnessDirectInertRows));
		}
		if (wetnessCompositeRows != kExpectedWetnessCompositeRows
			|| wetnessCompositeNeutralRows
				!= kExpectedWetnessCompositeNeutralRows
			|| wetnessCompositeVertexRows
				!= kExpectedWetnessCompositeVertexRows
			|| wetnessCompositeRows + wetnessCompositeNeutralRows
					+ wetnessCompositeVertexRows
				!= kExpectedCompositeRegistrationRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsdfComposite wetness registration partition",
				"Expected "
					+ std::to_string(kExpectedWetnessCompositeRows)
					+ " wetness, "
					+ std::to_string(kExpectedWetnessCompositeNeutralRows)
					+ " neutral pixel, and "
					+ std::to_string(kExpectedWetnessCompositeVertexRows)
					+ " vertex rows");
		}
		if (wetnessDirectRows + wetnessDirectInertRows
			!= kExpectedBsdfLightRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsdfLight wetness registration partition",
				"Expected "
					+ std::to_string(kExpectedBsdfLightRows)
					+ " total rows, found "
					+ std::to_string(
						wetnessDirectRows + wetnessDirectInertRows));
		}
		if (terrainDirectRows != kExpectedTerrainDirectRows
			|| terrainDirectInertRows != kExpectedTerrainDirectInertRows
			|| terrainDirectRows + terrainDirectInertRows
				!= kExpectedBsdfLightRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsdfLight terrain shadow coverage",
				"Expected "
					+ std::to_string(kExpectedTerrainDirectRows)
					+ " terrain and "
					+ std::to_string(kExpectedTerrainDirectInertRows)
					+ " inert rows, found "
					+ std::to_string(terrainDirectRows)
					+ " and "
					+ std::to_string(terrainDirectInertRows));
		}
		if (inverseSquareRows != kExpectedInverseSquareRows
			|| inverseSquareInertRows
				!= kExpectedInverseSquareInertRows
			|| inverseSquareRows + inverseSquareInertRows
				!= kExpectedBsdfLightRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsdfLight inverse-square coverage",
				"Expected "
					+ std::to_string(kExpectedInverseSquareRows)
					+ " punctual and "
					+ std::to_string(kExpectedInverseSquareInertRows)
					+ " inert rows, found "
					+ std::to_string(inverseSquareRows)
					+ " and "
					+ std::to_string(inverseSquareInertRows));
		}
		if (terrainCompositeRows != kExpectedTerrainCompositeRows
			|| terrainCompositeInertRows
				!= kExpectedTerrainCompositeInertRows
			|| terrainCompositeVertexRows
				!= kExpectedWetnessCompositeVertexRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsdfComposite terrain debug coverage",
				"Expected "
					+ std::to_string(kExpectedTerrainCompositeRows)
					+ " terrain debug, "
					+ std::to_string(kExpectedTerrainCompositeInertRows)
					+ " inert pixel, and "
					+ std::to_string(kExpectedWetnessCompositeVertexRows)
					+ " vertex rows");
		}
		if (wetnessDebugCompositeRows != kExpectedWetnessDebugCompositeRows
			|| wetnessDebugCompositeInertRows
				!= kExpectedWetnessDebugCompositeInertRows
			|| wetnessDebugCompositeVertexRows
				!= kExpectedWetnessCompositeVertexRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsdfComposite wetness debug coverage",
				"Expected "
					+ std::to_string(kExpectedWetnessDebugCompositeRows)
					+ " wetness debug, "
					+ std::to_string(kExpectedWetnessDebugCompositeInertRows)
					+ " inert pixel, and "
					+ std::to_string(kExpectedWetnessCompositeVertexRows)
					+ " vertex rows");
		}
		return {
			.registrationDerived = registrations.size(),
			.uniqueRegistrationInputs =
				uniqueRegistrationInputs.size(),
			.explicitPermutations =
				featureCompositionCases.size()
				+ exponentialFogRows
				+ contributorCompositionCount,
			.ambientCompositionRows = ambientCompositionRows,
			.ambientNonTargetRows = ambientNonTargetRows,
			.wetnessDirectRows = wetnessDirectRows,
			.wetnessDirectInertRows = wetnessDirectInertRows,
			.terrainDirectRows = terrainDirectRows,
			.terrainDirectInertRows = terrainDirectInertRows,
			.terrainCompositeRows = terrainCompositeRows,
			.terrainCompositeInertRows = terrainCompositeInertRows,
			.terrainCompositeVertexRows = terrainCompositeVertexRows,
			.wetnessDebugCompositeRows = wetnessDebugCompositeRows,
			.wetnessDebugCompositeInertRows = wetnessDebugCompositeInertRows,
			.wetnessDebugCompositeVertexRows =
				wetnessDebugCompositeVertexRows,
			.wetnessCompositeRows = wetnessCompositeRows,
			.wetnessCompositeNeutralRows = wetnessCompositeNeutralRows,
			.wetnessCompositeVertexRows = wetnessCompositeVertexRows,
			.inverseSquareRows = inverseSquareRows,
			.dfTiledLightingRows = dfTiledLightingRows,
			.inverseSquareTiledRows = inverseSquareTiledRows,
			.inverseSquareInertRows = inverseSquareInertRows,
			.exponentialFogRows = exponentialFogRows
		};
	}

}

int main(int argc, char** argv)
{
	if (argc != 3) {
		std::fprintf(
			stderr,
			"Usage: ShaderCompileTests <shader directory> "
			"<identity witness data>\n");
		return 2;
	}

	std::vector<ShaderCompileJob> jobs;
	CheckNativeDescriptorAliases(jobs);
	CheckImageSpaceFailureRouteAdmission(jobs);
	const auto sharedDataCount = AddSharedDataProbes(jobs, argv[1]);
	const auto screenSpaceGiCount = AddScreenSpaceGI(jobs, argv[1]);
	if (screenSpaceGiCount != kScreenSpaceGIPermutations) {
		AddPreparationFailure(
			jobs,
			"ScreenSpaceGI permutation census",
			"expected " + std::to_string(kScreenSpaceGIPermutations)
				+ " permutations, prepared " + std::to_string(screenSpaceGiCount));
	}
	const auto dynamicCubemapCount = AddDynamicCubemaps(jobs, argv[1]);
	if (dynamicCubemapCount != kDynamicCubemapPermutations) {
		AddPreparationFailure(
			jobs,
			"DynamicCubemaps permutation census",
			"expected " + std::to_string(kDynamicCubemapPermutations)
				+ " permutations, prepared "
				+ std::to_string(dynamicCubemapCount));
	}
	const auto lightingCounts = AddLighting(jobs, argv[1], argv[2]);
	const auto terrainShadowsCount = AddTerrainShadows(jobs, argv[1]);
	if (terrainShadowsCount != kTerrainShadowsPermutations) {
		AddPreparationFailure(
			jobs,
			"TerrainShadows permutation census",
			"expected " + std::to_string(kTerrainShadowsPermutations)
				+ " permutations, prepared "
				+ std::to_string(terrainShadowsCount));
	}
	const auto upscalingCount = AddUpscaling(jobs, argv[1]);
	if (upscalingCount != kUpscalingPermutations) {
		AddPreparationFailure(
			jobs,
			"Upscaling permutation census",
			"expected " + std::to_string(kUpscalingPermutations)
				+ " permutations, prepared " + std::to_string(upscalingCount));
	}

	std::printf(
		"ShaderCompile checked %zu shared substrate probes\n",
		sharedDataCount);
	std::printf(
		"ShaderCompile checked %zu ScreenSpaceGI permutations\n",
		screenSpaceGiCount);
	std::printf(
		"ShaderCompile verified %zu baseline identities (%zu unique inputs) and checked %zu lighting explicit/composed permutations\n",
		lightingCounts.registrationDerived,
		lightingCounts.uniqueRegistrationInputs,
		lightingCounts.explicitPermutations);
	std::printf(
		"ShaderCompile witnessed t26-t29 on %zu composing and %zu non-target kBsdfComposite pixel rows\n",
		lightingCounts.ambientCompositionRows,
		lightingCounts.ambientNonTargetRows);
	std::printf(
		"ShaderCompile witnessed t30+t31/s13 on %zu terrain shadow and %zu inert kBsdfLight rows\n",
		lightingCounts.terrainDirectRows,
		lightingCounts.terrainDirectInertRows);
	std::printf(
		"ShaderCompile checked inverse-square on %zu punctual and %zu inert kBsdfLight rows\n",
		lightingCounts.inverseSquareRows,
		lightingCounts.inverseSquareInertRows);
	std::printf(
		"ShaderCompile included %zu DFTiledLighting compute routes in the baseline identity set\n",
		lightingCounts.dfTiledLightingRows);
	std::printf(
		"ShaderCompile checked inverse-square on %zu DFTiledLighting compute routes\n",
		lightingCounts.inverseSquareTiledRows);
	std::printf(
		"ShaderCompile checked exponential fog on %zu BSDFComposite fog routes\n",
		lightingCounts.exponentialFogRows);
	std::printf(
		"ShaderCompile checked %zu TerrainShadows permutations\n",
		terrainShadowsCount);
	std::printf(
		"ShaderCompile checked %zu Upscaling permutations\n",
		upscalingCount);

	const int failures = CompileAll(jobs);

	if (failures == 0)
		std::printf("ShaderCompile passed\n");
	else
		std::printf("%d shader(s) failed to compile\n", failures);

	return failures ? 1 : 0;
}

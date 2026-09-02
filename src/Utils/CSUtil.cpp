#include "Utils/CSUtil.h"
#include "Utils/ShaderCompile.h"
#include "Utils/ShaderCache/ShaderCache.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <span>
#include <string>
#include <wrl/client.h>

#include "Log.h"

namespace cs::util
{
	namespace
	{
		auto* L = cs::log::Get("cs.util");
		thread_local shader_cache::RevalidationContext*
			g_activeRevalidation = nullptr;

		enum class ShaderKind : std::uint8_t
		{
			kPixel,
			kVertex,
			kHull,
			kDomain,
			kCompute,
			kUnsupported
		};

		ShaderKind ClassifyShader(const char* a_programType) noexcept
		{
			if (!a_programType)
				return ShaderKind::kUnsupported;
			if (!_stricmp(a_programType, "ps_5_0"))
				return ShaderKind::kPixel;
			if (!_stricmp(a_programType, "vs_5_0"))
				return ShaderKind::kVertex;
			if (!_stricmp(a_programType, "hs_5_0"))
				return ShaderKind::kHull;
			if (!_stricmp(a_programType, "ds_5_0"))
				return ShaderKind::kDomain;
			if (!_stricmp(a_programType, "cs_5_0") || !_stricmp(a_programType, "cs_4_0"))
				return ShaderKind::kCompute;
			return ShaderKind::kUnsupported;
		}

		bool TryGetCacheStage(
			ShaderKind a_kind,
			shader_cache::ShaderCacheStage& a_stage) noexcept
		{
			switch (a_kind) {
			case ShaderKind::kPixel:
				a_stage = shader_cache::ShaderCacheStage::kPixel;
				return true;
			case ShaderKind::kVertex:
				a_stage = shader_cache::ShaderCacheStage::kVertex;
				return true;
			case ShaderKind::kCompute:
				a_stage = shader_cache::ShaderCacheStage::kCompute;
				return true;
			default:
				return false;
			}
		}

		HRESULT CreateShaderChild(
			ID3D11Device&                a_device,
			ShaderKind                   a_kind,
			std::span<const std::uint8_t> a_bytecode,
			ID3D11DeviceChild*&          a_shader) noexcept
		{
			a_shader = nullptr;
			HRESULT result = E_INVALIDARG;
			switch (a_kind) {
			case ShaderKind::kPixel: {
				Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
				result = a_device.CreatePixelShader(
					a_bytecode.data(), a_bytecode.size(), nullptr, shader.GetAddressOf());
				if (SUCCEEDED(result) && shader)
					a_shader = shader.Detach();
				break;
			}
			case ShaderKind::kVertex: {
				Microsoft::WRL::ComPtr<ID3D11VertexShader> shader;
				result = a_device.CreateVertexShader(
					a_bytecode.data(), a_bytecode.size(), nullptr, shader.GetAddressOf());
				if (SUCCEEDED(result) && shader)
					a_shader = shader.Detach();
				break;
			}
			case ShaderKind::kHull: {
				Microsoft::WRL::ComPtr<ID3D11HullShader> shader;
				result = a_device.CreateHullShader(
					a_bytecode.data(), a_bytecode.size(), nullptr, shader.GetAddressOf());
				if (SUCCEEDED(result) && shader)
					a_shader = shader.Detach();
				break;
			}
			case ShaderKind::kDomain: {
				Microsoft::WRL::ComPtr<ID3D11DomainShader> shader;
				result = a_device.CreateDomainShader(
					a_bytecode.data(), a_bytecode.size(), nullptr, shader.GetAddressOf());
				if (SUCCEEDED(result) && shader)
					a_shader = shader.Detach();
				break;
			}
			case ShaderKind::kCompute: {
				Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader;
				result = a_device.CreateComputeShader(
					a_bytecode.data(), a_bytecode.size(), nullptr, shader.GetAddressOf());
				if (SUCCEEDED(result) && shader)
					a_shader = shader.Detach();
				break;
			}
			case ShaderKind::kUnsupported:
				break;
			}
			return SUCCEEDED(result) && !a_shader ? E_FAIL : result;
		}

		shader_cache::ShaderRecipe BuildShaderRecipe(
			const wchar_t* a_filePath,
			const std::vector<std::pair<const char*, const char*>>& a_defines,
			const char* a_programType,
			const char* a_program,
			shader_cache::ShaderCacheStage a_stage)
		{
			shader_cache::ShaderRecipe recipe;
			recipe.source = a_filePath;
			recipe.includeRoots.push_back(recipe.source.parent_path());
			recipe.defines.reserve(a_defines.size());
			for (const auto& [name, value] : a_defines)
				recipe.defines.emplace_back(name, value);
			recipe.entryPoint = a_program;
			recipe.profile = a_programType;
			recipe.stage = a_stage;
			return recipe;
		}

		void LogCompileFailure(
			const std::string& a_path,
			const std::string& a_error)
		{
			if (a_error == "Shader source missing")
				L->error("Shader source missing: {}", a_path);
			else
				L->warn("Shader compilation failed ({}):\n{}", a_path,
					a_error.empty() ? "Unknown error" : a_error);
		}
	}

	ShaderCompilationBatch::ShaderCompilationBatch() noexcept :
		_previous(std::exchange(g_activeRevalidation, &_revalidation))
	{}

	ShaderCompilationBatch::~ShaderCompilationBatch() noexcept
	{
		g_activeRevalidation = _previous;
	}

	ID3D11Device* GetD3DDevice()
	{
		auto* data = RE::BSGraphics::GetRendererData();
		if (!data || !data->device)
			return nullptr;
		return reinterpret_cast<ID3D11Device*>(data->device);
	}

	ID3D11DeviceChild* CompileShader(
		const wchar_t* a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program)
	{
		auto* device = GetD3DDevice();
		if (!device) {
			L->error("Cannot compile shader: D3D device not ready");
			return nullptr;
		}

		if (!a_filePath) {
			L->error("Cannot compile shader: no source path");
			return nullptr;
		}

		std::string narrow;
		std::wstring wide{ a_filePath };
		std::transform(wide.begin(), wide.end(), std::back_inserter(narrow), [](wchar_t c) { return (char)c; });

		const auto kind = ClassifyShader(a_programType);
		if (kind == ShaderKind::kUnsupported) {
			L->warn("CompileShader: unsupported program type '{}'", a_programType ? a_programType : "");
			return nullptr;
		}

		shader_cache::ShaderCacheStage cacheStage{};
		if (TryGetCacheStage(kind, cacheStage)) {
			const auto recipe =
				BuildShaderRecipe(a_filePath, a_defines, a_programType, a_program, cacheStage);
			shader_cache::ShaderCacheOptions options;
			options.revalidation = g_activeRevalidation;
			auto outcome =
				shader_cache::LoadOrCompileShader(recipe, options);
			if (!outcome.succeeded) {
				LogCompileFailure(narrow, outcome.error);
				return nullptr;
			}

			static std::atomic<bool> reportedCacheFailure{ false };
			if (!outcome.recordWritten && !outcome.cacheNote.empty()
				&& !reportedCacheFailure.exchange(true, std::memory_order_relaxed)) {
				L->warn("Shader cache unavailable: {}", outcome.cacheNote);
			}

			ID3D11DeviceChild* shader = nullptr;
			auto createResult = CreateShaderChild(*device, kind, outcome.bytecode, shader);
			if ((FAILED(createResult) || !shader)
				&& outcome.origin == shader_cache::CompileOrigin::kCacheHit) {
				L->warn("Cached shader bytecode rejected by the device; recompiling {}", narrow);
				outcome = shader_cache::LoadOrCompileShader(
					recipe,
					options,
					shader_cache::CacheMode::kRecompile);
				if (!outcome.succeeded) {
					LogCompileFailure(narrow, outcome.error);
					return nullptr;
				}
				createResult = CreateShaderChild(*device, kind, outcome.bytecode, shader);
			}

			DX::ThrowIfFailed(createResult);
			L->info(
				"Compile '{}' ok: {} bytes, source={}",
				narrow,
				outcome.bytecode.size(),
				shader_cache::DescribeCacheOutcome(outcome));
			return shader;
		}

		std::string shaderLogs;
		auto shaderBlob = CompileShaderToBlob(a_filePath, a_defines, a_programType, a_program, &shaderLogs);
		if (!shaderBlob) {
			LogCompileFailure(narrow, shaderLogs);
			return nullptr;
		}
		if (!shaderLogs.empty())
			L->debug("Shader logs ({}):\n{}", narrow, shaderLogs);

		const auto* bytecode = static_cast<const std::uint8_t*>(shaderBlob->GetBufferPointer());
		ID3D11DeviceChild* shader = nullptr;
		const auto result = CreateShaderChild(
			*device,
			kind,
			std::span(bytecode, shaderBlob->GetBufferSize()),
			shader);
		DX::ThrowIfFailed(result);
		return shader;
	}
}

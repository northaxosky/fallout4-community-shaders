#include "Utils/CSUtil.h"
#include "Render/ShaderInjection.h"
#include "Utils/ShaderCache/ShaderCache.h"
#include "Utils/ShaderCompile.h"

#include <algorithm>
#include <atomic>
#include <dxgi.h>
#include <filesystem>
#include <format>
#include <mutex>
#include <span>
#include <string>
#include <unordered_set>
#include <wrl/client.h>

#include "Log.h"

namespace cs::util
{
	std::string AdapterDescription(ID3D11Device* a_device)
	{
		if (!a_device)
			return "D3D11 device not ready";

		Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
		if (FAILED(a_device->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf()))))
			return "adapter query unavailable";

		Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
		if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf())) || !adapter)
			return "adapter query unavailable";

		DXGI_ADAPTER_DESC description{};
		if (FAILED(adapter->GetDesc(&description)))
			return "adapter description unavailable";

		std::string name = "unnamed adapter";
		const auto length = std::char_traits<wchar_t>::length(description.Description);
		if (length != 0) {
			const auto bytes = WideCharToMultiByte(
				CP_UTF8, 0, description.Description, static_cast<int>(length),
				nullptr, 0, nullptr, nullptr);
			if (bytes <= 0)
				return "adapter description unavailable";
			name.resize(static_cast<std::size_t>(bytes));
			if (WideCharToMultiByte(
					CP_UTF8, 0, description.Description, static_cast<int>(length),
					name.data(), bytes, nullptr, nullptr) != bytes) {
				return "adapter description unavailable";
			}
		}

		LARGE_INTEGER driverVersion{};
		std::string driver = "unavailable";
		if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion))) {
			driver = std::format("{}.{}.{}.{}",
				HIWORD(driverVersion.HighPart), LOWORD(driverVersion.HighPart),
				HIWORD(driverVersion.LowPart), LOWORD(driverVersion.LowPart));
		}

		return std::format(
			"{} (vendor 0x{:04X}, device 0x{:04X}, driver {}, {} MiB VRAM)",
			name, description.VendorId, description.DeviceId, driver,
			description.DedicatedVideoMemory / (1024 * 1024));
	}

	HRESULT CreateShaderChild(
		ID3D11Device& a_device,
		engine::ShaderStage a_stage,
		std::span<const std::uint8_t> a_bytecode,
		ID3D11DeviceChild*& a_shader) noexcept
	{
		engine::ScopedNativeShaderObservationBypass bypassObservation;
		a_shader = nullptr;
		HRESULT result = E_INVALIDARG;
		switch (a_stage) {
		case shader_cache::ShaderCacheStage::kPixel:
			{
				Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
				result = a_device.CreatePixelShader(
					a_bytecode.data(), a_bytecode.size(), nullptr, shader.GetAddressOf());
				if (SUCCEEDED(result) && shader)
					a_shader = shader.Detach();
				break;
			}
		case shader_cache::ShaderCacheStage::kVertex:
			{
				Microsoft::WRL::ComPtr<ID3D11VertexShader> shader;
				result = a_device.CreateVertexShader(
					a_bytecode.data(), a_bytecode.size(), nullptr, shader.GetAddressOf());
				if (SUCCEEDED(result) && shader)
					a_shader = shader.Detach();
				break;
			}
		case shader_cache::ShaderCacheStage::kCompute:
			{
				Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader;
				result = a_device.CreateComputeShader(
					a_bytecode.data(), a_bytecode.size(), nullptr, shader.GetAddressOf());
				if (SUCCEEDED(result) && shader)
					a_shader = shader.Detach();
				break;
			}
		default:
			break;
		}
		return SUCCEEDED(result) && !a_shader ? E_FAIL : result;
	}

	namespace
	{
		auto* L = cs::log::Get("cs.util");
		thread_local shader_cache::RevalidationContext*
			g_activeRevalidation = nullptr;

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

		// Stops per-frame getters from recompiling a known failure.
		class FailedRecipes
		{
		public:
			static std::string Key(
				const std::string& a_path,
				const std::vector<std::pair<const char*, const char*>>& a_defines,
				const char* a_programType,
				const char* a_program)
			{
				auto key = std::format("{}|{}|{}", a_path,
					a_programType ? a_programType : "", a_program ? a_program : "");
				for (const auto& [name, value] : a_defines)
					key += std::format("|{}={}", name ? name : "", value ? value : "");
				return key;
			}

			bool Contains(const std::string& a_key)
			{
				std::scoped_lock lock(_mutex);
				return _keys.contains(a_key);
			}

			void Add(std::string a_key)
			{
				std::scoped_lock lock(_mutex);
				_keys.insert(std::move(a_key));
			}

		private:
			std::mutex _mutex;
			std::unordered_set<std::string> _keys;
		};

		FailedRecipes& GetFailedRecipes()
		{
			static FailedRecipes recipes;
			return recipes;
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

		const auto recipe = BuildUtilityShaderRecipe(a_filePath, a_defines, a_programType, a_program);
		if (!recipe) {
			L->warn("CompileShader: unsupported program type '{}'", a_programType ? a_programType : "");
			return nullptr;
		}

		auto& failedRecipes = GetFailedRecipes();
		auto failureKey = FailedRecipes::Key(narrow, a_defines, a_programType, a_program);
		if (failedRecipes.Contains(failureKey))
			return nullptr;
		const auto fail = [&](const std::string& a_error) -> ID3D11DeviceChild* {
			LogCompileFailure(narrow, a_error);
			failedRecipes.Add(std::move(failureKey));
			return nullptr;
		};

		shader_cache::ShaderCacheOptions options;
		options.revalidation = g_activeRevalidation;
		auto outcome =
			shader_cache::LoadOrCompileShader(*recipe, options);
		if (!outcome.succeeded)
			return fail(outcome.error);

		static std::atomic<bool> reportedCacheFailure{ false };
		if (!outcome.recordWritten && !outcome.cacheNote.empty() && !reportedCacheFailure.exchange(true, std::memory_order_relaxed)) {
			L->warn("Shader cache unavailable: {}", outcome.cacheNote);
		}

		ID3D11DeviceChild* shader = nullptr;
		auto createResult = CreateShaderChild(*device, recipe->stage, outcome.bytecode, shader);
		if ((FAILED(createResult) || !shader) && outcome.origin == shader_cache::CompileOrigin::kCacheHit) {
			L->warn("Cached shader bytecode rejected by the device; recompiling {}", narrow);
			outcome = shader_cache::LoadOrCompileShader(
				*recipe,
				options,
				shader_cache::CacheMode::kRecompile);
			if (!outcome.succeeded)
				return fail(outcome.error);
			createResult = CreateShaderChild(*device, recipe->stage, outcome.bytecode, shader);
		}

		if (FAILED(createResult) || !shader)
			return fail(std::format("Device rejected shader: HRESULT {:#x}", static_cast<unsigned>(createResult)));
		L->info(
			"Compile '{}' ok: {} bytes, source={}",
			narrow,
			outcome.bytecode.size(),
			shader_cache::DescribeCacheOutcome(outcome));
		return shader;
	}
}

#include "Render/ShaderVariantCompilation.h"

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Utils/ShaderCache/ShaderCache.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdio>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.shadercache");

		constexpr std::string_view StageName(ShaderStage a_stage) noexcept
		{
			switch (a_stage) {
			case ShaderStage::kVertex:
				return "vertex";
			case ShaderStage::kPixel:
				return "pixel";
			case ShaderStage::kCompute:
				return "compute";
			}
			std::unreachable();
		}

		std::string DescribeDefines(
			const std::vector<std::pair<std::string, std::string>>& a_defines)
		{
			std::string result;
			for (const auto& [name, value] : a_defines) {
				if (!result.empty())
					result += ",";
				result += name;
				result += "=";
				result += value;
			}
			return result.empty() ? "none" : result;
		}

		void LogCompilationFailure(
			const ShaderVariantCompilationRequest& a_request,
			std::string_view a_error)
		{
			L->error(
				"Injected shader compile failed: owner='{}' family={} stage={} descriptor={:#010x} source='{}' entry='{}' profile='{}' defines='{}': {}",
				a_request.owner.empty() ? "<unknown>" : a_request.owner,
				a_request.familyId,
				StageName(a_request.stage),
				a_request.descriptor,
				a_request.sourcePath.string(),
				a_request.entryPoint,
				a_request.profile,
				DescribeDefines(a_request.defines),
				a_error.empty() ? "shader compilation failed" : a_error);
		}

		shader_cache::ShaderCacheStage ToCacheStage(
			ShaderStage a_stage) noexcept
		{
			static_assert(static_cast<std::uint8_t>(ShaderStage::kCount) == 3);
			switch (a_stage) {
			case ShaderStage::kVertex:
				return shader_cache::ShaderCacheStage::kVertex;
			case ShaderStage::kPixel:
				return shader_cache::ShaderCacheStage::kPixel;
			case ShaderStage::kCompute:
				return shader_cache::ShaderCacheStage::kCompute;
			}
			std::unreachable();
		}

		shader_cache::ShaderRecipe BuildRecipe(
			const ShaderVariantCompilationRequest& a_request)
		{
			shader_cache::ShaderRecipe recipe;
			recipe.source = a_request.sourcePath;
			recipe.includeRoots.push_back(a_request.sourcePath.parent_path());
			recipe.defines = a_request.defines;
			recipe.entryPoint = a_request.entryPoint;
			recipe.profile = a_request.profile;
			recipe.stage = ToCacheStage(a_request.stage);
			return recipe;
		}

		bool CreateShaderChild(
			ID3D11Device& a_device,
			ShaderStage a_stage,
			const void* a_bytecode,
			std::size_t a_bytecodeLength,
			winrt::com_ptr<ID3D11DeviceChild>& a_shader,
			std::string& a_error)
		{
			a_shader = nullptr;
			HRESULT createResult = E_FAIL;
			const char* createStage = "Unknown";
			static_assert(static_cast<std::uint8_t>(ShaderStage::kCount) == 3);
			{
				ScopedPixelShaderBrokerBypass bypassBroker;
				switch (a_stage) {
				case ShaderStage::kVertex: {
					createStage = "Vertex";
					winrt::com_ptr<ID3D11VertexShader> vertexShader;
					createResult = a_device.CreateVertexShader(
						a_bytecode,
						a_bytecodeLength,
						nullptr,
						vertexShader.put());
					if (vertexShader)
						a_shader.attach(vertexShader.detach());
					break;
				}
				case ShaderStage::kPixel: {
					createStage = "Pixel";
					winrt::com_ptr<ID3D11PixelShader> pixelShader;
					createResult = a_device.CreatePixelShader(
						a_bytecode,
						a_bytecodeLength,
						nullptr,
						pixelShader.put());
					if (pixelShader)
						a_shader.attach(pixelShader.detach());
					break;
				}
				case ShaderStage::kCompute: {
					createStage = "Compute";
					winrt::com_ptr<ID3D11ComputeShader> computeShader;
					createResult = a_device.CreateComputeShader(
						a_bytecode,
						a_bytecodeLength,
						nullptr,
						computeShader.put());
					if (computeShader)
						a_shader.attach(computeShader.detach());
					break;
				}
				}
			}

			if (FAILED(createResult) || !a_shader) {
				a_shader = nullptr;
				char buffer[64]{};
				std::snprintf(
					buffer,
					sizeof(buffer),
					"Create%sShader hr=0x%08x",
					createStage,
					static_cast<unsigned>(createResult));
				a_error = buffer;
				return false;
			}
			return true;
		}

		class GenerationRevalidationContexts
		{
		public:
			std::shared_ptr<shader_cache::RevalidationContext> Get(
				std::uint64_t a_generation)
			{
				std::scoped_lock lock(_mutex);
				auto& context = _contexts[a_generation];
				if (!context) {
					context = std::make_shared<
						shader_cache::RevalidationContext>();
				}
				return context;
			}

		private:
			std::mutex _mutex;
			std::unordered_map<
				std::uint64_t,
				std::shared_ptr<shader_cache::RevalidationContext>>
				_contexts;
		};

		ShaderVariantCompilationOutput CompileShaderVariant(
			ShaderVariantCompilationRequest a_request,
			shader_cache::RevalidationContext* a_revalidation,
			std::atomic<bool>& a_reportedCacheFailure)
		{
			ShaderVariantCompilationOutput result;
			if (!a_request.device) {
				result.error = "no D3D11 device";
				LogCompilationFailure(a_request, result.error);
				return result;
			}

			const auto recipe = BuildRecipe(a_request);
			shader_cache::ShaderCacheOptions options;
			options.revalidation = a_revalidation;

			auto outcome = shader_cache::LoadOrCompileShader(recipe, options);
			if (!outcome.succeeded) {
				result.error = outcome.error.empty() ?
					"shader compilation failed" :
					std::move(outcome.error);
				LogCompilationFailure(a_request, result.error);
				return result;
			}
			if (!outcome.recordWritten
				&& !outcome.cacheNote.empty()
				&& !a_reportedCacheFailure.exchange(
					true, std::memory_order_relaxed)) {
				L->warn("Shader cache unavailable: {}", outcome.cacheNote);
			}

			winrt::com_ptr<ID3D11DeviceChild> shader;
			std::string createError;
			bool created = CreateShaderChild(
				*a_request.device,
				a_request.stage,
				outcome.bytecode.data(),
				outcome.bytecode.size(),
				shader,
				createError);

			if (!created
				&& outcome.origin == shader_cache::CompileOrigin::kCacheHit) {
				outcome = shader_cache::LoadOrCompileShader(
					recipe,
					options,
					shader_cache::CacheMode::kRecompile);
				if (!outcome.succeeded) {
					result.error = outcome.error.empty() ?
						createError :
						std::move(outcome.error);
					LogCompilationFailure(a_request, result.error);
					return result;
				}
				created = CreateShaderChild(
					*a_request.device,
					a_request.stage,
					outcome.bytecode.data(),
					outcome.bytecode.size(),
					shader,
					createError);
			}

			if (!created) {
				result.error = std::move(createError);
				LogCompilationFailure(a_request, result.error);
				return result;
			}
			const std::string shaderName =
				"Render/Injected/" + a_request.sourcePath.stem().string()
				+ (a_request.stage == ShaderStage::kVertex ?
						".VS" :
						a_request.stage == ShaderStage::kCompute ?
							".CS" :
							".PS");
			render::annotation::SetName(shader.get(), shaderName);

			result.shader = std::move(shader);
			return result;
		}

		std::size_t DefaultWorkerCount() noexcept
		{
			static const auto performanceThreads = [] {
				const auto fallback =
					std::max(1U, std::thread::hardware_concurrency());
				DWORD size = 0;
				GetLogicalProcessorInformationEx(
					RelationProcessorCore, nullptr, &size);
				if (GetLastError() != ERROR_INSUFFICIENT_BUFFER
					|| size == 0) {
					return fallback;
				}

				std::vector<std::uint8_t> storage(size);
				auto* information = reinterpret_cast<
					PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
					storage.data());
				if (!GetLogicalProcessorInformationEx(
						RelationProcessorCore,
						information,
						&size)) {
					return fallback;
				}

				BYTE highestEfficiencyClass = 0;
				for (DWORD offset = 0; offset < size;) {
					const auto* entry = reinterpret_cast<
						const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
						storage.data() + offset);
					highestEfficiencyClass = std::max(
						highestEfficiencyClass,
						entry->Processor.EfficiencyClass);
					offset += entry->Size;
				}

				std::uint32_t count = 0;
				for (DWORD offset = 0; offset < size;) {
					const auto* entry = reinterpret_cast<
						const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
						storage.data() + offset);
					if (entry->Processor.EfficiencyClass
						== highestEfficiencyClass) {
						for (WORD group = 0;
							group < entry->Processor.GroupCount;
							++group) {
							count += static_cast<std::uint32_t>(
								std::popcount(
									entry->Processor
										.GroupMask[group].Mask));
						}
					}
					offset += entry->Size;
				}
				return count > 0 ? count : fallback;
			}();
			return std::max<std::size_t>(
				performanceThreads / 2, 1);
		}
	}

	std::shared_ptr<ShaderVariantCompilationCache>
		CreateCachingShaderVariantCompilationCache()
	{
		auto contexts =
			std::make_shared<GenerationRevalidationContexts>();
		auto reportedCacheFailure =
			std::make_shared<std::atomic<bool>>(false);
		return CreateAsyncShaderVariantCompilationCache(
			[contexts, reportedCacheFailure](
				ShaderVariantCompilationRequest a_request) {
				const auto revalidation =
					contexts->Get(a_request.sourceGeneration);
				return CompileShaderVariant(
					std::move(a_request),
					revalidation.get(),
					*reportedCacheFailure);
			},
			DefaultWorkerCount());
	}
}

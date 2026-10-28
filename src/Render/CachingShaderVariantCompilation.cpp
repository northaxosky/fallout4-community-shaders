#include "Render/ShaderVariantCompilation.h"
#include "Render/ShaderVariantRecipe.h"

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/ShaderInjection.h"
#include "Utils/CSUtil.h"
#include "Utils/ShaderCache/ShaderCache.h"
#include "Utils/ShaderSamplerBindings.h"
#include "Utils/WinApi.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdio>
#include <format>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.shadercache");

		void LogCompilationFailure(
			const ShaderVariantCompilationRequest& a_request,
			std::string_view a_error)
		{
			L->error(
				"Injected shader compile failed: owner='{}' family={} stage={} descriptor={:#010x} source='{}' entry='{}' profile='{}' defines='{}': {}",
				a_request.owner.empty() ? "<unknown>" : a_request.owner,
				a_request.familyId,
				ShaderStageName(a_request.stage),
				a_request.descriptor,
				a_request.sourcePath.string(),
				a_request.entryPoint,
				a_request.profile,
				DescribeShaderInjectionDefines(a_request.defines),
				a_error.empty() ? "shader compilation failed" : a_error);
		}

		class GenerationRevalidationContexts
		{
		public:
			std::shared_ptr<shader_cache::RevalidationContext> Get(
				std::uint64_t a_generation)
			{
				std::scoped_lock lock(_mutex);
				if (!_context || _generation != a_generation) {
					_context = std::make_shared<
						shader_cache::RevalidationContext>();
					_generation = a_generation;
				}
				return _context;
			}

		private:
			std::mutex _mutex;
			std::uint64_t _generation = 0;
			std::shared_ptr<shader_cache::RevalidationContext> _context;
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

			const auto recipe = BuildShaderVariantRecipe(a_request);
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
			if (!outcome.recordWritten && !outcome.cacheNote.empty() && !a_reportedCacheFailure.exchange(true, std::memory_order_relaxed)) {
				L->warn("Shader cache unavailable: {}", outcome.cacheNote);
			}

			ID3D11DeviceChild* child = nullptr;
			auto createResult = util::CreateShaderChild(
				*a_request.device,
				a_request.stage,
				outcome.bytecode,
				child);

			if (FAILED(createResult) && outcome.origin == shader_cache::CompileOrigin::kCacheHit) {
				outcome = shader_cache::LoadOrCompileShader(
					recipe,
					options,
					shader_cache::CacheMode::kRecompile);
				if (!outcome.succeeded) {
					result.error = outcome.error.empty() ?
					                   "shader recompilation failed" :
					                   std::move(outcome.error);
					LogCompilationFailure(a_request, result.error);
					return result;
				}
				createResult = util::CreateShaderChild(
					*a_request.device,
					a_request.stage,
					outcome.bytecode,
					child);
			}

			if (FAILED(createResult)) {
				result.error = std::format("Create {} shader hr=0x{:08x}",
					ShaderStageName(a_request.stage), static_cast<unsigned>(createResult));
				LogCompilationFailure(a_request, result.error);
				return result;
			}
			winrt::com_ptr<ID3D11DeviceChild> shader;
			shader.attach(child);
			const std::string shaderName =
				"Render/Injected/" + a_request.sourcePath.stem().string() + (a_request.stage == ShaderStage::kVertex ? ".VS" : a_request.stage == ShaderStage::kCompute ? ".CS" :
																																										  ".PS");
			render::annotation::SetName(shader.get(), shaderName);

			result.samplers = util::ReflectShaderSamplers(outcome.bytecode.data(), outcome.bytecode.size()).value_or(util::ShaderSamplerUse{});
			result.shader = std::move(shader);
			result.cacheHit = outcome.origin == shader_cache::CompileOrigin::kCacheHit;
			return result;
		}

		std::size_t DefaultWorkerCount() noexcept
		{
			return std::max<std::size_t>(Util::GetPerformanceCoreCount() / 2, 1);
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

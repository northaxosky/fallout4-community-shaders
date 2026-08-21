#include "Render/ShaderVariantCompilation.h"

#include "Log.h"
#include "Utils/CSSha1.h"
#include "Utils/ShaderCache/ShaderCache.h"

#include <cstdio>
#include <utility>

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.shadercache");

		class CachingShaderVariantCompilationHandle final :
			public ShaderVariantCompilationHandle
		{
		public:
			explicit CachingShaderVariantCompilationHandle(
				ShaderStage a_stage,
				winrt::com_ptr<ID3D11DeviceChild> a_shader) noexcept :
				_stage(a_stage),
				_shader(std::move(a_shader))
			{}

			ShaderVariantCompilationState GetState() const noexcept override
			{
				return ShaderVariantCompilationState::kReady;
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

			ShaderStage GetStage() const noexcept override
			{
				return _stage;
			}

		private:
			ShaderStage _stage;
			winrt::com_ptr<ID3D11DeviceChild> _shader;
		};

		shader_cache::ShaderCacheStage ToCacheStage(ShaderStage a_stage) noexcept
		{
			static_assert(static_cast<std::uint8_t>(ShaderStage::kCount) == 2);
			return a_stage == ShaderStage::kVertex
				? shader_cache::ShaderCacheStage::kVertex
				: shader_cache::ShaderCacheStage::kPixel;
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
			static_assert(static_cast<std::uint8_t>(ShaderStage::kCount) == 2);
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
				default:
					break;
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

		class CachingShaderVariantCompilationPolicy final :
			public ShaderVariantCompilationPolicy
		{
		public:
			ShaderVariantCompilationResult Prepare(
				ShaderVariantCompilationRequest a_request) override
			{
				ShaderVariantCompilationResult result;
				if (!a_request.device) {
					result.error = "no D3D11 device";
					return result;
				}

				const auto recipe = BuildRecipe(a_request);
				shader_cache::ShaderCacheOptions options;
				options.revalidation = &_revalidation;

				auto outcome = shader_cache::LoadOrCompileShader(recipe, options);
				if (!outcome.succeeded) {
					result.error = outcome.error.empty()
						? "shader compilation failed"
						: std::move(outcome.error);
					return result;
				}
				if (!outcome.recordWritten && !outcome.cacheNote.empty()
					&& !_reportedCacheFailure) {
					L->warn("Shader cache unavailable: {}", outcome.cacheNote);
					_reportedCacheFailure = true;
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

				// heal rejected cache hits with fresh FXC bytecode
				if (!created
					&& outcome.origin == shader_cache::CompileOrigin::kCacheHit) {
					outcome = shader_cache::LoadOrCompileShader(
						recipe,
						options,
						shader_cache::CacheMode::kRecompile);
					if (!outcome.succeeded) {
						result.error = outcome.error.empty()
							? createError
							: std::move(outcome.error);
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
					return result;
				}

				result.state = ShaderVariantCompilationState::kReady;
				result.bytecodeSize = outcome.bytecode.size();
				result.compiledSha1 = sha1::Sha1ToHex(sha1::Sha1Compute(
					outcome.bytecode.data(),
					outcome.bytecode.size()));
				result.servedFromCache =
					outcome.origin == shader_cache::CompileOrigin::kCacheHit;
				result.handle =
					std::make_shared<CachingShaderVariantCompilationHandle>(
						a_request.stage,
						std::move(shader));
				return result;
			}

		private:
			// memo lifetime = one freeze batch
			shader_cache::RevalidationContext _revalidation;
			bool                              _reportedCacheFailure = false;
		};
	}

	std::shared_ptr<ShaderVariantCompilationPolicy>
		CreateCachingShaderVariantCompilationPolicy()
	{
		return std::make_shared<CachingShaderVariantCompilationPolicy>();
	}
}

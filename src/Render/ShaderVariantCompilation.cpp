#include "Render/ShaderVariantCompilation.h"

#include "Render/PixelShaderSwapBroker.h"
#include "Utils/CSSha1.h"
#include "Utils/ShaderCompile.h"

#include <cstdio>

namespace cs::engine
{
	namespace
	{
		class EagerShaderVariantCompilationHandle final :
			public ShaderVariantCompilationHandle
		{
		public:
			explicit EagerShaderVariantCompilationHandle(
				winrt::com_ptr<ID3D11PixelShader> a_shader) noexcept :
				_shader(std::move(a_shader))
			{}

			ShaderVariantCompilationState GetState() const noexcept override
			{
				return ShaderVariantCompilationState::kReady;
			}

			winrt::com_ptr<ID3D11PixelShader>
				AcquireOrRequest() noexcept override
			{
				return _shader;
			}

			ID3D11PixelShader* PeekPixelShader() const noexcept override
			{
				return _shader.get();
			}

		private:
			winrt::com_ptr<ID3D11PixelShader> _shader;
		};

		class EagerShaderVariantCompilationPolicy final :
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

				std::vector<std::pair<const char*, const char*>> defines;
				defines.reserve(a_request.defines.size());
				for (const auto& [name, value] : a_request.defines)
					defines.emplace_back(name.c_str(), value.c_str());

				std::string compileError;
				const auto blob = util::CompileShaderToBlob(
					a_request.sourcePath.c_str(),
					defines,
					a_request.profile.c_str(),
					a_request.entryPoint.c_str(),
					&compileError);
				if (!blob) {
					result.error = compileError.empty()
						? "shader compilation failed"
						: std::move(compileError);
					return result;
				}

				winrt::com_ptr<ID3D11PixelShader> shader;
				HRESULT createResult = E_FAIL;
				{
					ScopedPixelShaderBrokerBypass bypassBroker;
					createResult = a_request.device->CreatePixelShader(
						blob->GetBufferPointer(),
						blob->GetBufferSize(),
						nullptr,
						shader.put());
				}
				if (FAILED(createResult) || !shader) {
					char buffer[64]{};
					std::snprintf(
						buffer,
						sizeof(buffer),
						"CreatePixelShader hr=0x%08x",
						static_cast<unsigned>(createResult));
					result.error = buffer;
					return result;
				}

				result.state = ShaderVariantCompilationState::kReady;
				result.bytecodeSize =
					static_cast<std::size_t>(blob->GetBufferSize());
				result.compiledSha1 = sha1::Sha1ToHex(sha1::Sha1Compute(
					blob->GetBufferPointer(),
					blob->GetBufferSize()));
				result.handle =
					std::make_shared<EagerShaderVariantCompilationHandle>(
						std::move(shader));
				return result;
			}
		};
	}

	std::shared_ptr<ShaderVariantCompilationPolicy>
		CreateEagerShaderVariantCompilationPolicy()
	{
		return std::make_shared<EagerShaderVariantCompilationPolicy>();
	}
}

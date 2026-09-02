#include "Render/ShaderVariantCompilation.h"

#include "Render/PixelShaderSwapBroker.h"
#include "Render/Annotation.h"
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

				winrt::com_ptr<ID3D11DeviceChild> shader;
				HRESULT createResult = E_FAIL;
				const char* createStage = nullptr;
				const char* shaderSuffix = nullptr;
				static_assert(
					static_cast<std::uint8_t>(ShaderStage::kCount) == 3);
				{
					ScopedPixelShaderBrokerBypass bypassBroker;
					switch (a_request.stage) {
					case ShaderStage::kVertex: {
						createStage = "Vertex";
						shaderSuffix = ".VS";
						winrt::com_ptr<ID3D11VertexShader> vertexShader;
						createResult = a_request.device->CreateVertexShader(
							blob->GetBufferPointer(),
							blob->GetBufferSize(),
							nullptr,
							vertexShader.put());
						render::annotation::SetName(
							vertexShader.get(), "Render/Injected/VertexShader.VS");
						if (vertexShader)
							shader.attach(vertexShader.detach());
						break;
					}
					case ShaderStage::kPixel: {
						createStage = "Pixel";
						shaderSuffix = ".PS";
						winrt::com_ptr<ID3D11PixelShader> pixelShader;
						createResult = a_request.device->CreatePixelShader(
							blob->GetBufferPointer(),
							blob->GetBufferSize(),
							nullptr,
							pixelShader.put());
						render::annotation::SetName(
							pixelShader.get(), "Render/Injected/PixelShader.PS");
						if (pixelShader)
							shader.attach(pixelShader.detach());
						break;
					}
					case ShaderStage::kCompute: {
						createStage = "Compute";
						shaderSuffix = ".CS";
						winrt::com_ptr<ID3D11ComputeShader> computeShader;
						createResult = a_request.device->CreateComputeShader(
							blob->GetBufferPointer(),
							blob->GetBufferSize(),
							nullptr,
							computeShader.put());
						render::annotation::SetName(
							computeShader.get(), "Render/Injected/ComputeShader.CS");
						if (computeShader)
							shader.attach(computeShader.detach());
						break;
					}
					}
				}
				if (FAILED(createResult) || !shader) {
					char buffer[64]{};
					std::snprintf(
						buffer,
						sizeof(buffer),
						"Create%sShader hr=0x%08x",
						createStage,
						static_cast<unsigned>(createResult));
					result.error = buffer;
					return result;
				}
				const std::string shaderName =
					"Render/Injected/" + a_request.sourcePath.stem().string()
					+ shaderSuffix;
				render::annotation::SetName(shader.get(), shaderName);

				result.state = ShaderVariantCompilationState::kReady;
				result.bytecodeSize =
					static_cast<std::size_t>(blob->GetBufferSize());
				result.compiledSha1 = sha1::Sha1ToHex(sha1::Sha1Compute(
					blob->GetBufferPointer(),
					blob->GetBufferSize()));
				result.handle =
					std::make_shared<EagerShaderVariantCompilationHandle>(
						a_request.stage,
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

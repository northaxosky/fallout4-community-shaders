#pragma once

#include <d3d11.h>
#include <winrt/base.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cs::engine
{
	enum class ShaderVariantCompilationState : std::uint8_t
	{
		kReady,
		kPending,
		kFailed
	};

	struct ShaderVariantCompilationRequest
	{
		winrt::com_ptr<ID3D11Device> device;
		std::filesystem::path sourcePath;
		std::string entryPoint;
		std::string profile;
		std::vector<std::pair<std::string, std::string>> defines;
	};

	class ShaderVariantCompilationHandle
	{
	public:
		virtual ~ShaderVariantCompilationHandle() = default;

		virtual ShaderVariantCompilationState GetState() const noexcept = 0;
		virtual winrt::com_ptr<ID3D11PixelShader>
			AcquireOrRequest() noexcept = 0;
		virtual ID3D11PixelShader* PeekPixelShader() const noexcept = 0;
	};

	struct ShaderVariantCompilationResult
	{
		ShaderVariantCompilationState state =
			ShaderVariantCompilationState::kFailed;
		std::shared_ptr<ShaderVariantCompilationHandle> handle;
		std::size_t bytecodeSize = 0;
		std::string compiledSha1;
		std::string error;
	};

	class ShaderVariantCompilationPolicy
	{
	public:
		virtual ~ShaderVariantCompilationPolicy() = default;

		virtual ShaderVariantCompilationResult Prepare(
			ShaderVariantCompilationRequest a_request) = 0;
	};

	std::shared_ptr<ShaderVariantCompilationPolicy>
		CreateEagerShaderVariantCompilationPolicy();
}

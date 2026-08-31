#pragma once

#include <d3d11_1.h>
#include <d3d12.h>

#include <string_view>

namespace cs::render::annotation
{
	void Initialize(ID3D11DeviceContext* a_context) noexcept;

	class ScopedEvent
	{
	public:
		explicit ScopedEvent(std::string_view a_name) noexcept;
		ScopedEvent(
			ID3D12GraphicsCommandList* a_commandList,
			std::string_view a_name) noexcept;
		~ScopedEvent() noexcept;

		ScopedEvent(const ScopedEvent&) = delete;
		ScopedEvent(ScopedEvent&&) = delete;
		ScopedEvent& operator=(const ScopedEvent&) = delete;
		ScopedEvent& operator=(ScopedEvent&&) = delete;

	private:
		ID3DUserDefinedAnnotation* _d3d11{};
		ID3D12GraphicsCommandList* _d3d12{};
	};

	void SetMarker(std::string_view a_name) noexcept;
	void SetMarker(
		ID3D12GraphicsCommandList* a_commandList,
		std::string_view a_name) noexcept;
	void SetName(ID3D11DeviceChild* a_object, std::string_view a_name) noexcept;
	void SetName(ID3D12Object* a_object, std::string_view a_name) noexcept;
}

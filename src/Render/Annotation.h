#pragma once

#include <d3d11_1.h>
#include <d3d12.h>

#include <string_view>

namespace cs::render::annotation
{
	void Initialize(ID3D11DeviceContext* a_context) noexcept;
	using BeginPassCallback = bool (*)(std::string_view);
	using EndPassCallback = void (*)();
	void SetProfilerCallbacks(BeginPassCallback a_begin, EndPassCallback a_end) noexcept;

	class ScopedEvent
	{
	public:
		// Container scopes must pass false so the disjoint profiler can time their GPU leaves.
		explicit ScopedEvent(std::string_view a_name, bool a_profile = true) noexcept;
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
		bool _profile{};
	};

	void SetMarker(std::string_view a_name) noexcept;
	void SetMarker(
		ID3D12GraphicsCommandList* a_commandList,
		std::string_view a_name) noexcept;
	void SetName(ID3D11DeviceChild* a_object, std::string_view a_name) noexcept;
	void SetName(ID3D12Object* a_object, std::string_view a_name) noexcept;
}

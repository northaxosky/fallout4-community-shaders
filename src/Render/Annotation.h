#pragma once

#include <d3d11_1.h>
#include <d3d12.h>

#include <optional>
#include <string_view>

namespace cs::render::annotation
{
	void Initialize(ID3D11DeviceContext* a_context) noexcept;
	using BeginPassCallback = bool (*)(std::string_view);
	using EndPassCallback = void (*)();
	void SetProfilerCallbacks(BeginPassCallback a_begin, EndPassCallback a_end) noexcept;
	// A range sink mirrors scoped events into a tool that reads them only while active().
	struct RangeSink
	{
		bool (*active)(){};
		void (*begin)(std::string_view){};
		void (*end)(){};
	};
	void SetRangeSink(const RangeSink& a_sink) noexcept;
	// True while a capture tool or a registered range sink consumes the events.
	[[nodiscard]] bool EventsConsumed() noexcept;

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
		bool _sink{};
	};

	// Spans a Pre/Post hook pair, so no C++ scope can own it.
	class SpannedEvent
	{
	public:
		void Begin(std::string_view a_name, bool a_profile = true) noexcept { _event.emplace(a_name, a_profile); }
		void End() noexcept { _event.reset(); }

	private:
		std::optional<ScopedEvent> _event;
	};

	void SetMarker(std::string_view a_name) noexcept;
	void SetMarker(
		ID3D12GraphicsCommandList* a_commandList,
		std::string_view a_name) noexcept;
	void SetName(ID3D11DeviceChild* a_object, std::string_view a_name) noexcept;
	void SetName(ID3D12Object* a_object, std::string_view a_name) noexcept;
}

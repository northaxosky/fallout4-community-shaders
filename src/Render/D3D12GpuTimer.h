#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <d3d12.h>
#include <winrt/base.h>

namespace cs::render
{
	// Times D3D12 lists for the profiler; no-ops if creation failed.
	class D3D12GpuTimer
	{
	public:
		static constexpr std::uint32_t kMarksPerRecord = 8;
		static constexpr std::uint32_t kIntervalsPerRecord = 4;

		// One per submission record; reused for every list recorded into it.
		struct Record
		{
			static constexpr std::uint32_t kOpen = UINT32_MAX;

			struct Interval
			{
				std::string_view name;
				std::uint32_t begin = 0;
				std::uint32_t end = kOpen;
			};

			std::uint32_t base = 0;
			std::uint32_t marks = 0;
			std::uint32_t intervalCount = 0;
			bool recording = false;
			// Set only once the list executes; closed-unexecuted lists are never read.
			bool pending = false;
			std::array<Interval, kIntervalsPerRecord> intervals;

			void Discard() noexcept { pending = false; }
		};

		bool Initialize(ID3D12Device* a_device, ID3D12CommandQueue* a_queue, std::uint32_t a_records);
		void Release() noexcept;

		// Hands out the next block of timestamp slots.
		[[nodiscard]] Record Reserve() noexcept;
		// After the list is reset; records only while the D3D11 profiler is active.
		void Begin(Record& a_record) const noexcept;
		void BeginScope(ID3D12GraphicsCommandList* a_list, Record& a_record, std::string_view a_name) const noexcept;
		void EndScope(ID3D12GraphicsCommandList* a_list, Record& a_record, std::string_view a_name) const noexcept;
		// Before Close.
		void Resolve(ID3D12GraphicsCommandList* a_list, const Record& a_record) const noexcept;
		// After ExecuteCommandLists.
		void Commit(Record& a_record) const noexcept;
		// Once the record's list is known complete.
		void Collect(Record& a_record) const noexcept;
		// Objects in-flight lists may reference; for the teardown quarantine.
		[[nodiscard]] std::array<IUnknown*, 2> Resources() const noexcept { return { _heap.get(), _readback.get() }; }

	private:
		void Mark(ID3D12GraphicsCommandList* a_list, Record& a_record) const noexcept;

		winrt::com_ptr<ID3D12QueryHeap> _heap;
		winrt::com_ptr<ID3D12Resource> _readback;
		const std::uint64_t* _mapped = nullptr;
		std::uint64_t _frequency = 0;
		std::uint32_t _records = 0;
		std::uint32_t _nextRecord = 0;
	};
}

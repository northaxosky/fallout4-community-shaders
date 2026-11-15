#pragma once

#include "../Shaders/FO4/TruePBR/Record.h"
#include "MaterialRegistry.h"

#include <memory>

struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;

namespace cs::buffer
{
	class StructuredBuffer;
}

namespace cs::features::true_pbr
{
	// Pixel slot of the record table.
	inline constexpr std::uint32_t kRecordTableSlot = 68;

	// GPU mirror of the registry, indexed by record id. Render thread only.
	class RecordTable
	{
	public:
		RecordTable();
		~RecordTable();

		// Throws when the device cannot create the buffer.
		void Create();
		// Appends the records published since the last call; they never change.
		void Upload(ID3D11DeviceContext* a_context);
		[[nodiscard]] ID3D11ShaderResourceView* SRV() const noexcept;

	private:
		std::unique_ptr<buffer::StructuredBuffer> _buffer;
		std::size_t _uploaded = 0;
	};

	[[nodiscard]] TruePBRRecord::Row MakeRecordRow(const MaterialRecord& a_record) noexcept;
}

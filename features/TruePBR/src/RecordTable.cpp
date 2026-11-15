#include "RecordTable.h"

#include "Utils/CSBuffer.h"

#include <d3d11.h>

#include <vector>

namespace cs::features::true_pbr
{
	TruePBRRecord::Row MakeRecordRow(const MaterialRecord& a_record) noexcept
	{
		const auto parameters = a_record.material.ResolvedParameters();
		const bool hasEmissive = !a_record.material.Texture(PbrTexture::kEmissive).empty();
		return {
			.roughnessScale = parameters[ParameterIndex("roughnessScale")],
			.specularLevel = parameters[ParameterIndex("specularLevel")],
			.flags = TruePBRRecord::Valid | (hasEmissive ? TruePBRRecord::HasEmissive : 0u)
		};
	}

	RecordTable::RecordTable() = default;
	RecordTable::~RecordTable() = default;

	void RecordTable::Create()
	{
		constexpr auto rows = static_cast<std::uint32_t>(MaterialRegistry::kCapacity);
		_buffer = std::make_unique<buffer::StructuredBuffer>(
			buffer::StructuredBufferDesc<TruePBRRecord::Row>(rows, false), rows);
		_buffer->CreateSRV();
		_buffer->SetName("TruePBR/Records");
		_uploaded = 0;
	}

	void RecordTable::Upload(ID3D11DeviceContext* a_context)
	{
		if (!_buffer || !a_context)
			return;
		const auto count = Records().Count();
		if (count <= _uploaded)
			return;
		std::vector<TruePBRRecord::Row> rows;
		rows.reserve(count - _uploaded);
		// Ids are 1-based, so the next row follows the uploaded count.
		for (auto id = _uploaded + 1; id <= count; ++id) {
			const auto* record = Records().Get(static_cast<RecordId>(id));
			rows.push_back(record ? MakeRecordRow(*record) : TruePBRRecord::Row{});
		}
		const D3D11_BOX box{
			.left = static_cast<UINT>((_uploaded + 1) * sizeof(TruePBRRecord::Row)),
			.top = 0,
			.front = 0,
			.right = static_cast<UINT>((count + 1) * sizeof(TruePBRRecord::Row)),
			.bottom = 1,
			.back = 1
		};
		a_context->UpdateSubresource(_buffer->Resource(), 0, &box, rows.data(), 0, 0);
		_uploaded = count;
	}

	ID3D11ShaderResourceView* RecordTable::SRV() const noexcept
	{
		return _buffer ? _buffer->SRV() : nullptr;
	}
}

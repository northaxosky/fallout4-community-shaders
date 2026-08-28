#pragma once

#include <cstring>
#include <new>
#include <optional>

#include <imgui_internal.h>

namespace cs
{
	class ImGuiRecoverySnapshot
	{
	public:
		static std::optional<ImGuiRecoverySnapshot> Capture() noexcept
		{
			try {
				auto* context = ImGui::GetCurrentContext();
				if (!context)
					return std::nullopt;
				return ImGuiRecoverySnapshot(*context);
			} catch (...) {
				return std::nullopt;
			}
		}

		ImGuiRecoverySnapshot(const ImGuiRecoverySnapshot&) = delete;
		ImGuiRecoverySnapshot& operator=(const ImGuiRecoverySnapshot&) = delete;

		ImGuiRecoverySnapshot(ImGuiRecoverySnapshot&& a_other) noexcept :
			_context(a_other._context),
			_stackState(a_other._stackState),
			_nextWindowData(a_other._nextWindowData),
			_nextItemData(a_other._nextItemData),
			_openPopupData(a_other._openPopupData),
			_openPopupSize(a_other._openPopupSize)
		{
			a_other._openPopupData = nullptr;
			a_other._openPopupSize = 0;
		}

		~ImGuiRecoverySnapshot()
		{
			if (_openPopupData)
				ImGui::MemFree(_openPopupData);
		}

		void Recover() noexcept
		{
			ImGui::SetCurrentContext(_context);
			ImGuiIO& io = ImGui::GetIO();
			const bool assertEnabled = io.ConfigErrorRecoveryEnableAssert;
			io.ConfigErrorRecoveryEnableAssert = false;
			ImGui::ErrorRecoveryTryToRecoverState(&_stackState);
			io.ConfigErrorRecoveryEnableAssert = assertEnabled;

			_context->NextWindowData = _nextWindowData;
			_context->NextItemData = _nextItemData;
			if (_context->OpenPopupStack.Data)
				ImGui::MemFree(_context->OpenPopupStack.Data);
			_context->OpenPopupStack.Data = _openPopupData;
			_context->OpenPopupStack.Size = _openPopupSize;
			_context->OpenPopupStack.Capacity = _openPopupSize;
			_openPopupData = nullptr;
			_openPopupSize = 0;
			if (!_context->OpenPopupStack.empty())
				ImGui::ClosePopupToLevel(0, true);
		}

	private:
		explicit ImGuiRecoverySnapshot(ImGuiContext& a_context) :
			_context(&a_context),
			_nextWindowData(a_context.NextWindowData),
			_nextItemData(a_context.NextItemData)
		{
			ImGui::ErrorRecoveryStoreState(&_stackState);
			if (!a_context.OpenPopupStack.empty()) {
				_openPopupSize = a_context.OpenPopupStack.Size;
				_openPopupData = static_cast<ImGuiPopupData*>(
					ImGui::MemAlloc(static_cast<std::size_t>(_openPopupSize) * sizeof(ImGuiPopupData)));
				if (!_openPopupData)
					throw std::bad_alloc();
				std::memcpy(
					_openPopupData,
					a_context.OpenPopupStack.Data,
					static_cast<std::size_t>(_openPopupSize) * sizeof(ImGuiPopupData));
			}
		}

		ImGuiContext* _context;
		ImGuiErrorRecoveryState _stackState{};
		ImGuiNextWindowData _nextWindowData;
		ImGuiNextItemData _nextItemData;
		ImGuiPopupData* _openPopupData = nullptr;
		int _openPopupSize = 0;
	};
}

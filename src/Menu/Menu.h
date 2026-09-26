#pragma once

#include "Menu/DebugViewSelection.h"
#include "Host/HostRuntimeModel.h"

#include <DearModdingUI/Client.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace cs
{
	class Feature;

	class Menu
	{
	public:
		static Menu& Get();

		void Load();
		bool Save();
		void ApplyDebugViewSelections();
		void DrawDebugViewSelector(const Feature& a_feature);

		void DrawHome(dmui::Client& a_client);
		void DrawAdvanced(dmui::Client& a_client);
		void DrawPresets(dmui::Client& a_client);
		void ObserveHostFrame(dmui::Client& a_client);
		void OnHostDeviceReady() noexcept;

		void RequestClearShaderCache() noexcept;
		void ReleaseDebugImages() noexcept;

		static void ShowToast(
			std::string a_text,
			double a_durationSec = 3.0,
			DMUI_StatusSeverity a_severity = DMUI_STATUS_SEVERITY_INFO);

	private:
		Menu() = default;

		enum class DialogOperation : std::uint8_t
		{
			kNone,
			kClearCache,
			kSavePresetAs,
			kDeletePreset
		};

		struct DialogState
		{
			DialogOperation operation{ DialogOperation::kNone };
			DMUI_DialogHandle handle{ DMUI_INVALID_DIALOG_HANDLE };
			host::DialogSubmissionTracker submissions;
			host::DialogCallRetry polling;
			host::DialogCallRetry resolution;
			std::string text;
			std::string presetIdentity;
			std::string presetName;
			std::filesystem::path presetPath;
		};

		struct DebugImage
		{
			ID3D11ShaderResourceView* source{};
			std::uint32_t width{};
			std::uint32_t height{};
			std::uint64_t generation{};
			std::uint64_t retryAfterFrame{};
			host::ImageImportFailure importFailure{
				host::ImageImportFailure::kNone };
			DMUI_Result importResult{ DMUI_RESULT_OK };
			std::optional<DMUI_Result> loggedFailure;
			dmui::ImageResource image;
		};

		void SetDebugViewSelection(const Feature& a_feature, std::string_view a_view);
		void DrawFeatureOverview(dmui::Client& a_client);
		void DrawShaderSettings(dmui::Client& a_client);
		void DrawDebugTexture(dmui::Client& a_client, const Feature& a_feature);
		void ProcessDialog(dmui::Client& a_client);
		void StartDialog(
			dmui::Client& a_client,
			DialogOperation a_operation,
			const DMUI_DialogDescriptor& a_descriptor);
		host::DialogSubmissionTracker::Outcome ExecuteDialogSubmission(
			const DMUI_DialogEvent& a_event,
			std::string_view a_text);
		void RetryDialogResolution(
			dmui::Client& a_client,
			const DMUI_DialogEvent& a_event);
		void HandleDialogFailure(
			std::string_view a_operation,
			DMUI_Result a_result,
			host::DialogCallRetry& a_retry);
		bool ClearShaderCache(std::string& a_error);
		bool CheckHostResult(
			dmui::Client& a_client,
			bool a_succeeded,
			std::string_view a_operation);

		debug_view::SelectionState _debugViews;
		host::StartupLoadSnapshot _startupLoads;
		std::unordered_map<std::string, DebugImage> _debugImages;
		std::unordered_map<std::string, DMUI_Result> _hostCallFailures;
		DialogState _dialog;
		std::atomic_bool _clearCacheRequested{};
		std::uint64_t _hostFrameSerial{};
		std::uint64_t _debugImageGeneration{};
	};
}

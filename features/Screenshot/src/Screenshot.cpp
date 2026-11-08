#include "Screenshot.h"

#include <DearModdingUI/Client.h>

#include "HDRDisplay.h"
#include "Host/HostClient.h"
#include "Log.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "RE/M/Main.h"
#include "RE/M/MenuControls.h"
#include "Render/Engine.h"
#include "Render/EngineCallSite.h"
#include "Render/FrameBoundary.h"
#include "Render/InterfaceLayer.h"
#include "Render/RenderHooks.h"
#include "Render/ScreenshotSites.h"
#include "ScreenshotFile.h"
#include "Settings/SettingsPersistence.h"
#include "Utils/PathUtf8.h"

#include <array>
#include <cstring>

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.screenshot");

		// Non-blocking read; DXGI_ERROR_WAS_STILL_DRAWING while the copy is in flight.
		HRESULT ReadStaging(ID3D11DeviceContext& a_context, ID3D11Texture2D& a_staging,
			DXGI_FORMAT a_format, UINT a_width, UINT a_height, DirectX::ScratchImage& a_image)
		{
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (const HRESULT result = a_context.Map(&a_staging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped); FAILED(result))
				return result == DXGI_ERROR_WAS_STILL_DRAWING ? result : E_FAIL;
			const auto unmap = [&](HRESULT a_result) {
				a_context.Unmap(&a_staging, 0);
				return a_result;
			};
			if (!mapped.pData || mapped.RowPitch == 0 || FAILED(a_image.Initialize2D(a_format, a_width, a_height, 1, 1)))
				return unmap(E_FAIL);
			const auto* destination = a_image.GetImage(0, 0, 0);
			if (!destination)
				return unmap(E_FAIL);
			// Never read past a short driver mapping; uncopied bytes stay black.
			const size_t rowBytes = std::min<size_t>(destination->rowPitch, mapped.RowPitch);
			const size_t mappedRows = mapped.DepthPitch != 0 ? mapped.DepthPitch / mapped.RowPitch : destination->height;
			const size_t rows = std::min<size_t>(destination->height, mappedRows);
			std::memset(a_image.GetPixels(), 0, a_image.GetPixelsSize());
			for (size_t row = 0; row < rows; ++row) {
				std::memcpy(a_image.GetPixels() + row * destination->rowPitch,
					static_cast<const uint8_t*>(mapped.pData) + row * mapped.RowPitch, rowBytes);
			}
			return unmap(S_OK);
		}

		struct QueueCheck
		{
			static bool thunk(RE::MenuControls* a_controls)
			{
				const bool queued = func(a_controls);
				Screenshot::GetSingleton()->OnQueueChecked(queued);
				return queued;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct TakeScreenShot
		{
			static void thunk(char* a_path, std::int32_t a_format, std::int32_t a_target)
			{
				if (!Screenshot::GetSingleton()->OnTakeScreenShot())
					func(a_path, a_format, a_target);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Resolves both before patching either, so the pair installs whole.
		bool InstallSites()
		{
			const auto queue = engine::ResolveCallSite(engine::screenshot::kSwapQueuedCheck);
			const auto take = engine::ResolveCallSite(engine::screenshot::kSwapTakeScreenShot);
			if (!queue || !take) {
				L->error("Screenshot hooks unavailable: {}", (queue ? take : queue).error());
				return false;
			}
			stl::write_thunk_call<QueueCheck>(*queue);
			stl::write_thunk_call<TakeScreenShot>(*take);
			return true;
		}
	}

	Screenshot* Screenshot::GetSingleton()
	{
		static Screenshot instance;
		return &instance;
	}

	bool Screenshot::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(screenshot_settings::kSchema, a_config, candidate, a_error))
			return false;
		_settings = std::move(candidate);
		_liveSettings = settings::BindLiveSettings(screenshot_settings::kSchema, _settings);
		return true;
	}

	bool Screenshot::SaveSettings()
	{
		return settings::SaveDelta(screenshot_settings::kSchema, GetConfigKey(), _settings, *L);
	}

	void Screenshot::RestoreDefaultSettings()
	{
		_settings = Settings{};
		SaveSettings();
	}

	void Screenshot::Load()
	{
		if (!engine::RegisterPreInterface(*this, [this] { OnPreInterface(); }) ||
			!render::RegisterPostSwap(*this, [this] { OnFrameBoundary(); })) {
			FailLoad("Screenshot needs the interface and frame boundary hooks.");
			return;
		}
		if (!InstallSites())
			FailLoad("Screenshot could not hook the engine screenshot step.");
	}

	std::expected<Screenshot::RequestId, Screenshot::CaptureRefusal> Screenshot::RequestCapture()
	{
		if (!IsHealthy())
			return std::unexpected(CaptureRefusal{ "Screenshot feature is not loaded", "communityshaders.feature(action='list') shows Screenshot.loaded" });
		auto* controls = RE::MenuControls::GetSingleton();
		if (!controls || !controls->QueueScreenshot())
			return std::unexpected(CaptureRefusal{ "A screenshot is already queued", "Retry on the next frame" });
		const auto id = ++_lastRequestId;
		_requested.store(id, std::memory_order_release);
		return id;
	}

	void Screenshot::AddCaptureObserver(CaptureObserver a_observer)
	{
		_observers.Add(std::move(a_observer));
	}

	void Screenshot::OnPreInterface()
	{
		if (!_queuedThisFrame || !_settings.captureWithoutInterface || render::InterfaceLayer::Get().IsSceneActive())
			return;
		// Not split: the frame buffer holds the scene alone right now.
		auto* frame = engine::GetRenderTargetTexture(RE::BSGraphics::RenderTargetID::kFrameBuffer);
		auto* context = engine::GetImmediateContext();
		if (!frame || !context)
			return;
		D3D11_TEXTURE2D_DESC desc{};
		frame->GetDesc(&desc);
		if (!_scene || _scene->desc.Width != desc.Width || _scene->desc.Height != desc.Height || _scene->desc.Format != desc.Format) {
			desc.BindFlags = 0;
			desc.MiscFlags = 0;
			try {
				_scene = std::make_unique<buffer::Texture2D>(desc);
			} catch (const std::exception& error) {
				L->error("Scene snapshot creation failed: {}", error.what());
				return;
			}
		}
		engine::CopyResourcePreservingOM(context, _scene->resource.get(), frame);
		_sceneKept = true;
	}

	ID3D11Texture2D* Screenshot::SelectSource(bool a_interface, bool& a_hdr)
	{
		auto* hdrDisplay = HDRDisplay::GetSingleton();
		const bool wantHdr = hdrDisplay->IsHdrActive();
		// SDR with the interface is already in the engine's copy target.
		if (wantHdr || !a_interface) {
			if (const auto* frame = hdrDisplay->ComposeCapture(wantHdr, a_interface)) {
				a_hdr = wantHdr;
				return frame->resource.get();
			}
		}
		a_hdr = false;
		if (!a_interface && _sceneKept)
			return _scene->resource.get();
		if (!a_interface)
			L->warn("No interface-free frame was available; saving the full frame.");
		return engine::GetRenderTargetTexture(RE::BSGraphics::RenderTargetID::kScreenshot);
	}

	bool Screenshot::OnTakeScreenShot()
	{
		_queuedThisFrame = false;
		const bool captured = Capture();
		// A failed capture leaves the engine's file as the fallback.
		return captured && _settings.disableVanillaScreenshot;
	}

	bool Screenshot::Capture()
	{
		const auto id = _requested.exchange(0, std::memory_order_acq_rel);
		const auto fail = [&](std::string a_error) {
			Complete({ .id = id, .error = std::move(a_error) });
			return false;
		};
		auto* device = engine::GetDevice();
		auto* context = engine::GetImmediateContext();
		if (!device || !context)
			return fail("The renderer is not ready");

		const auto settings = _settings;
		bool hdr = false;
		auto* source = SelectSource(!settings.captureWithoutInterface, hdr);
		if (!source)
			return fail("No frame was available to capture");

		D3D11_TEXTURE2D_DESC desc{};
		source->GetDesc(&desc);
		screenshot_settings::PixelRegion region{ 0, 0, desc.Width, desc.Height };
		if (settings.applyCropToScreenshot)
			region = screenshot_settings::CropRegion(settings.cropAspect, desc.Width, desc.Height);

		D3D11_TEXTURE2D_DESC stagingDesc = desc;
		stagingDesc.Width = region.width;
		stagingDesc.Height = region.height;
		stagingDesc.MipLevels = 1;
		stagingDesc.ArraySize = 1;
		stagingDesc.SampleDesc = { 1, 0 };
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.BindFlags = 0;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingDesc.MiscFlags = 0;
		Readback readback{ .format = desc.Format, .width = region.width, .height = region.height, .hdr = hdr, .id = id, .settings = settings };
		if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, readback.staging.put())))
			return fail("Could not create the readback texture");
		const D3D11_BOX box{ region.x, region.y, 0, region.x + region.width, region.y + region.height, 1 };
		context->CopySubresourceRegion(readback.staging.get(), 0, 0, 0, 0, source, 0, &box);
		readback.path = screenshot_file::BuildPath(settings.screenshotPath);
		_readbacks.push_back(std::move(readback));
		return true;
	}

	void Screenshot::OnFrameBoundary()
	{
		PollReadbacks();
		DrainCompletions();
		// Joins after the queue drains, so queued shots still save.
		if (const auto* main = RE::Main::GetSingleton(); main && main->quitGame)
			StopEncoder();
	}

	void Screenshot::PollReadbacks()
	{
		auto* context = engine::GetImmediateContext();
		if (!context)
			return;
		std::erase_if(_readbacks, [&](Readback& a_readback) {
			DirectX::ScratchImage image;
			const HRESULT result = ReadStaging(*context, *a_readback.staging, a_readback.format, a_readback.width, a_readback.height, image);
			if (result == DXGI_ERROR_WAS_STILL_DRAWING)
				return false;
			if (FAILED(result))
				Complete({ .id = a_readback.id, .path = a_readback.path, .error = "Could not read back the frame" });
			else
				Save(a_readback, std::move(image));
			return true;
		});
	}

	// Encoding and disk I/O stay off the render thread.
	void Screenshot::Save(const Readback& a_readback, DirectX::ScratchImage&& a_image)
	{
		EncodeJob work{
			.id = a_readback.id,
			.job = {
				.image = std::move(a_image),
				.path = a_readback.path,
				.hdr = a_readback.hdr,
				.hdrPngBitDepth = a_readback.settings.hdrPngBitDepth,
				.fixDiscordHdr = a_readback.settings.fixDiscordHdr,
				.copyToClipboard = a_readback.settings.copyToClipboard }
		};
		const auto fail = [&](std::string a_error) {
			Complete({ .id = work.id, .path = work.job.path, .error = std::move(a_error) });
		};
		if (_encoderStopped)
			return fail("The encoder has stopped for shutdown");
		try {
			if (!_encoder.joinable())
				_encoder = std::jthread([this](std::stop_token a_stop) { RunEncoder(a_stop); });
		} catch (const std::exception& error) {
			return fail(std::string("Could not start the encoder: ") + error.what());
		}
		{
			const std::scoped_lock lock{ _encodeMutex };
			_encodeQueue.push_back(std::move(work));
		}
		_encodeWake.notify_one();
	}

	void Screenshot::RunEncoder(std::stop_token a_stop)
	{
		// WIC needs COM on the encoding thread.
		const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
		for (;;) {
			EncodeJob work;
			{
				std::unique_lock lock{ _encodeMutex };
				_encodeWake.wait(lock, a_stop, [this] { return !_encodeQueue.empty(); });
				// Empty here means stop was requested and the queue is drained.
				if (_encodeQueue.empty())
					break;
				work = std::move(_encodeQueue.front());
				_encodeQueue.pop_front();
			}
			CaptureCompletion done{ .id = work.id, .path = work.job.path, .error = screenshot_file::Save(work.job) };
			std::error_code ec;
			if (done.error.empty())
				done.bytes = std::filesystem::file_size(done.path, ec);
			Complete(std::move(done));
		}
		if (com)
			CoUninitialize();
	}

	void Screenshot::StopEncoder()
	{
		_encoderStopped = true;
		if (_encoder.joinable()) {
			_encoder.request_stop();
			_encoder.join();
		}
	}

	void Screenshot::Complete(CaptureCompletion a_completion)
	{
		const std::scoped_lock lock{ _completionMutex };
		_completions.push_back(std::move(a_completion));
	}

	void Screenshot::DrainCompletions()
	{
		std::vector<CaptureCompletion> done;
		{
			const std::scoped_lock lock{ _completionMutex };
			done.swap(_completions);
		}
		for (const auto& completion : done) {
			if (completion.error.empty()) {
				L->info("Saved screenshot to {}", util::PathToUtf8(completion.path));
				host::HostClient::Get().PostNotification(DMUI_STATUS_SEVERITY_SUCCESS,
					util::PathToUtf8(completion.path.filename()), 3000, "Screenshot saved");
			} else {
				L->error("Screenshot failed: {}", completion.error);
				host::HostClient::Get().PostNotification(DMUI_STATUS_SEVERITY_ERROR,
					completion.error + "; see the log.", 5000, "Screenshot failed");
			}
			_observers.Notify(completion);
		}
	}

	void Screenshot::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		const auto field = edit.Bind(screenshot_settings::kSchema, _settings);

		if (const ui::Section section{ "screenshot-capture", "Capture" }; section) {
			dmui::ui::TextWrapped("Print Screen captures with this feature; saving runs in the background.");
			if (dmui::ui::Button("Take Screenshot Now"))
				static_cast<void>(RequestCapture());
			field(&Settings::captureWithoutInterface);
			field(&Settings::disableVanillaScreenshot);
		}

		if (const ui::Section section{ "screenshot-crop", "Crop" }; section) {
			field(&Settings::applyCropToScreenshot);
			using screenshot_settings::CropAspect;
			const std::array aspects{
				dmui::ChoiceOption<CropAspect>{ CropAspect::kFullFrame, "Full frame", "full-frame", true },
				dmui::ChoiceOption<CropAspect>{ CropAspect::k16x9, "16:9", "16-9", true },
				dmui::ChoiceOption<CropAspect>{ CropAspect::k21x9, "21:9", "21-9", true },
				dmui::ChoiceOption<CropAspect>{ CropAspect::k32x9, "32:9", "32-9", true },
				dmui::ChoiceOption<CropAspect>{ CropAspect::k4x3, "4:3", "4-3", true },
				dmui::ChoiceOption<CropAspect>{ CropAspect::k1x1, "1:1", "1-1", true }
			};
			field.Choice(&Settings::cropAspect, aspects);
		}

		if (const ui::Section section{ "screenshot-output", "Output" }; section) {
			field(&Settings::screenshotPath);
			if (dmui::ui::Button("Open Folder")) {
				const auto folder = screenshot_file::ResolveFolder(_settings.screenshotPath);
				std::error_code ec;
				std::filesystem::create_directories(folder, ec);
				const auto utf8 = util::PathToUtf8(folder);
				auto& client = host::HostClient::Get().Client();
				std::uint32_t nativeError{};
				if (!client.OpenExternal({ .targetKind = DMUI_EXTERNAL_TARGET_DIRECTORY, .target = utf8.c_str() }, &nativeError))
					L->warn("Failed to open '{}' (native error {})", utf8, nativeError);
			}
			field(&Settings::copyToClipboard);
			if (HDRDisplay::GetSingleton()->IsHdrActive()) {
				field(&Settings::hdrPngBitDepth, { .format = "%d-bit", .flags = dmui::ui::SliderFlags::kAlwaysClamp });
				field(&Settings::fixDiscordHdr);
			} else {
				dmui::ui::TextDisabled("HDR PNG options appear while HDR output is on.");
			}
		}
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(Screenshot::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}

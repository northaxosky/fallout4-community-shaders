#include "RemoteControlTools.h"

#include "RemoteControl.h"

#include "Feature.h"
#include "Log.h"
#include "Render/FrameDispatcher.h"

#include <array>
#include <exception>
#include <stdexcept>

#include <DevBenchAPI.h>

namespace cs::features::remote_control
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.remotecontrol");

		constexpr std::array kTools{
			&kFeatureTool,
			&kInspectTool,
			&kShaderCacheTool,
			&kSettingsTool,
			&kCaptureTool,
			&kNvPerfTool
		};

		// No exception may cross the DLL boundary; write the result once.
		void Dispatch(void* a_context, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			const auto& tool = *static_cast<const Tool*>(a_context);
			std::string text;
			try {
				Json result;
				if (!RemoteControl::GetSingleton()->IsEnabled()) {
					result = Error("Remote Control is disabled");
				} else {
					auto args = Json::object();
					if (a_argsJson && *a_argsJson)
						args = Json::parse(a_argsJson);
					if (!args.is_object())
						throw std::invalid_argument("arguments must be a JSON object");
					result = tool.handler(args);
				}
				text = result.dump(-1, ' ', false, Json::error_handler_t::replace);
			} catch (const std::exception& error) {
				L->warn("{} failed: {}", tool.name, error.what());
				text = Error("invalid request", { { "detail", error.what() } }).dump(-1, ' ', false, Json::error_handler_t::replace);
			} catch (...) {
				text = R"({"error":"unknown handler error"})";
			}
			a_write(a_sink, text.c_str());
		}
	}

	std::span<const Tool* const> GetTools() noexcept
	{
		return kTools;
	}

	std::vector<std::string_view> RegisterTools(DevBenchAPI::IDevBenchInterface001& a_host)
	{
		std::vector<std::string_view> names;
		for (const auto* tool : kTools) {
			a_host.RegisterTool(tool->name, tool->descriptor, &Dispatch, const_cast<Tool*>(tool));
			names.emplace_back(tool->name);
		}
		return names;
	}

	Feature* FindFeature(std::string_view a_shortName, bool a_loadedOnly)
	{
		for (auto* feature : FeatureManager::Get().GetRegisteredFeatures()) {
			if (feature->GetName() == a_shortName)
				return !a_loadedOnly || feature->IsLoaded() ? feature : nullptr;
		}
		return nullptr;
	}

	Json Error(std::string a_message, Json a_details)
	{
		Json result{ { "error", std::move(a_message) } };
		result.update(a_details);
		return result;
	}

	Json OnFrame(std::function<Json()> a_task)
	{
		auto result = render::GetFrameDispatcher().RunOnFrame<Json>(std::move(a_task));
		return result ? std::move(*result) : Error(std::move(result.error()));
	}
}

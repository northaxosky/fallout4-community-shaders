#include "RemoteControlTools.h"

#include "Feature.h"
#include "Settings/LiveSettings.h"

namespace cs::features::remote_control
{
	namespace
	{
		Json Apply(const std::string& a_action)
		{
			auto& manager = FeatureManager::Get();
			std::string error;
			const bool done = a_action == "save" ? manager.SaveAllSettings(error) :
			                  a_action == "load" ? manager.LoadAllSettings(error) :
			                                       manager.ResetAllSettings(error);
			if (!done)
				return Error("settings " + a_action + " failed", { { "action", a_action }, { "detail", error } });
			return { { "action", a_action }, { "done", true } };
		}

		Json Handle(const Json& a_args)
		{
			const auto action = a_args.value("action", std::string{});
			if (action != "save" && action != "load" && action != "reset") {
				return action.empty() ? Error("missing required parameter 'action'") :
				                        Error("unknown action (save|load|reset)", { { "action", action } });
			}
			return OnFrame([action] { return Apply(action); });
		}
	}

	const Tool kSettingsTool{
		"communityshaders.settings",
		R"({"description":"Save, load, or reset the GLOBAL Community Shaders user configuration (FO4CommunityShaders.toml). Action-dispatched; returns {action,done:true} once applied at the next frame. save: persist every feature's current live settings and 'enabled' switch; restart-only values keep their file value unless they were edited in the menu. load: reread the file and apply its live settings to every loaded feature (settings that apply only at restart are not applied, but become the pending restart values); use it to roll live communityshaders.feature changes back to the saved baseline. reset: restore every loaded feature to its defaults, then save. save, load and reset are refused while an A/B comparison runs.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["save","load","reset"]}},"required":["action"]}})",
		&Handle
	};
}

#include "RemoteControlTools.h"

#include "Features/RemoteControl/DevFlags.h"

namespace cs::features::remote_control
{
	namespace
	{
		Json FlagsJson()
		{
			Json flags = Json::object();
			for (const auto& [name, value] : DevFlags::List())
				flags[name] = value;
			return { { "flags", std::move(flags) } };
		}

		// Flags are atomics, so nothing marshals to the frame thread.
		Json Handle(const Json& a_args)
		{
			const auto action = a_args.value("action", std::string{ "list" });
			if (action == "set") {
				const auto flags = a_args.find("flags");
				if (flags == a_args.end() || !flags->is_object())
					return Error("set needs flags: {name: bool}");
				for (const auto& item : flags->items()) {
					if (!item.value().is_boolean())
						return Error("flag values must be booleans", { { "flag", item.key() } });
				}
				for (const auto& item : flags->items())
					DevFlags::Set(item.key(), item.value().get<bool>());
			} else if (action == "reset") {
				DevFlags::Reset();
			} else if (action != "list") {
				return Error("unknown action (set|list|reset)", { { "action", action } });
			}
			return FlagsJson();
		}
	}

	const Tool kDevFlagsTool{
		"communityshaders.devflags",
		R"({"description":"Named runtime booleans guarding temporary candidate code paths (DEV_FLAG(\"tmp::name\") in C++), for screening a change on and off in one launch. Flags read false until set and reset at every launch. Action-dispatched. set: params flags (object of name -> boolean; all validated before any is applied). list (default): every flag a call site has read or set. reset: every flag false. All return {flags:{name:bool}}.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["set","list","reset"]},"flags":{"type":"object","additionalProperties":{"type":"boolean"}}}}})",
		&Handle
	};
}

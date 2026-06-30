#include "Registry.h"

#include "Log.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <variant>

namespace cs::features::replacement
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.shaderreplacement");

		// Minimal strict-JSON parser for the manifest schema; no comments, for tooling portability.
		struct JsonValue;
		using JsonObject = std::vector<std::pair<std::string, std::shared_ptr<JsonValue>>>;
		using JsonArray  = std::vector<std::shared_ptr<JsonValue>>;

		struct JsonValue
		{
			std::variant<std::monostate, bool, double, std::string, JsonArray, JsonObject> v;

			bool is_null()   const { return v.index() == 0; }
			bool is_bool()   const { return v.index() == 1; }
			bool is_number() const { return v.index() == 2; }
			bool is_string() const { return v.index() == 3; }
			bool is_array()  const { return v.index() == 4; }
			bool is_object() const { return v.index() == 5; }

			bool                as_bool()   const { return std::get<bool>(v); }
			double              as_number() const { return std::get<double>(v); }
			const std::string&  as_string() const { return std::get<std::string>(v); }
			const JsonArray&    as_array()  const { return std::get<JsonArray>(v); }
			const JsonObject&   as_object() const { return std::get<JsonObject>(v); }
		};

		class JsonParser
		{
		public:
			explicit JsonParser(std::string text) : _t(std::move(text)) {}

			std::shared_ptr<JsonValue> Parse()
			{
				SkipWs();
				auto v = ParseValue();
				SkipWs();
				if (_i != _t.size())
					Fail("Trailing characters after root value");
				return v;
			}

			const std::string& Error() const noexcept { return _err; }

		private:
			std::string _t;
			std::size_t _i = 0;
			std::string _err;

			[[noreturn]] void Fail(const char* msg)
			{
				char buf[128];
				std::snprintf(buf, sizeof(buf), "%s at offset %zu", msg, _i);
				_err = buf;
				throw std::runtime_error(_err);
			}

			void SkipWs()
			{
				while (_i < _t.size()) {
					const char c = _t[_i];
					if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++_i; continue; }
					break;
				}
			}

			std::shared_ptr<JsonValue> ParseValue()
			{
				SkipWs();
				if (_i >= _t.size()) Fail("Unexpected end of input");
				const char c = _t[_i];
				if (c == '{') return ParseObject();
				if (c == '[') return ParseArray();
				if (c == '"') return ParseString();
				if (c == 't' || c == 'f') return ParseBool();
				if (c == 'n') return ParseNull();
				if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber();
				Fail("Unexpected character");
			}

			std::shared_ptr<JsonValue> ParseObject()
			{
				++_i;
				JsonObject obj;
				SkipWs();
				if (_i < _t.size() && _t[_i] == '}') { ++_i; auto v = std::make_shared<JsonValue>(); v->v = obj; return v; }
				while (true) {
					SkipWs();
					if (_i >= _t.size() || _t[_i] != '"') Fail("Expected string key");
					auto key = ParseRawString();
					SkipWs();
					if (_i >= _t.size() || _t[_i] != ':') Fail("Expected ':'");
					++_i;
					auto val = ParseValue();
					obj.emplace_back(std::move(key), std::move(val));
					SkipWs();
					if (_i < _t.size() && _t[_i] == ',') { ++_i; continue; }
					if (_i < _t.size() && _t[_i] == '}') { ++_i; break; }
					Fail("Expected ',' or '}'");
				}
				auto v = std::make_shared<JsonValue>();
				v->v = std::move(obj);
				return v;
			}

			std::shared_ptr<JsonValue> ParseArray()
			{
				++_i;
				JsonArray arr;
				SkipWs();
				if (_i < _t.size() && _t[_i] == ']') { ++_i; auto v = std::make_shared<JsonValue>(); v->v = arr; return v; }
				while (true) {
					arr.push_back(ParseValue());
					SkipWs();
					if (_i < _t.size() && _t[_i] == ',') { ++_i; continue; }
					if (_i < _t.size() && _t[_i] == ']') { ++_i; break; }
					Fail("Expected ',' or ']'");
				}
				auto v = std::make_shared<JsonValue>();
				v->v = std::move(arr);
				return v;
			}

			std::string ParseRawString()
			{
				if (_t[_i] != '"') Fail("Expected '\"'");
				++_i;
				std::string out;
				while (_i < _t.size()) {
					char c = _t[_i++];
					if (c == '"') return out;
					if (c == '\\') {
						if (_i >= _t.size()) Fail("Unterminated escape");
						char e = _t[_i++];
						switch (e) {
						case '"':  out.push_back('"');  break;
						case '\\': out.push_back('\\'); break;
						case '/':  out.push_back('/');  break;
						case 'n':  out.push_back('\n'); break;
						case 't':  out.push_back('\t'); break;
						case 'r':  out.push_back('\r'); break;
						case 'b':  out.push_back('\b'); break;
						case 'f':  out.push_back('\f'); break;
						case 'u': {
							if (_i + 4 > _t.size()) Fail("Bad \\u escape");
							// Treat as opaque hex; we only need ASCII in the manifest.
							_i += 4;
							out.push_back('?');
							break;
						}
						default: Fail("Bad escape");
						}
					} else {
						out.push_back(c);
					}
				}
				Fail("Unterminated string");
			}

			std::shared_ptr<JsonValue> ParseString()
			{
				auto s = ParseRawString();
				auto v = std::make_shared<JsonValue>();
				v->v = std::move(s);
				return v;
			}

			std::shared_ptr<JsonValue> ParseBool()
			{
				if (_t.compare(_i, 4, "true") == 0)  { _i += 4; auto v = std::make_shared<JsonValue>(); v->v = true;  return v; }
				if (_t.compare(_i, 5, "false") == 0) { _i += 5; auto v = std::make_shared<JsonValue>(); v->v = false; return v; }
				Fail("Bad literal");
			}

			std::shared_ptr<JsonValue> ParseNull()
			{
				if (_t.compare(_i, 4, "null") == 0) { _i += 4; return std::make_shared<JsonValue>(); }
				Fail("Bad literal");
			}

			std::shared_ptr<JsonValue> ParseNumber()
			{
				std::size_t s = _i;
				if (_t[_i] == '-') ++_i;
				while (_i < _t.size() && ((_t[_i] >= '0' && _t[_i] <= '9') || _t[_i] == '.' || _t[_i] == 'e' || _t[_i] == 'E' || _t[_i] == '+' || _t[_i] == '-'))
					++_i;
				try {
					auto v = std::make_shared<JsonValue>();
					v->v = std::stod(_t.substr(s, _i - s));
					return v;
				} catch (...) {
					Fail("Bad number");
				}
			}
		};

		const std::shared_ptr<JsonValue>* GetField(const JsonObject& obj, const char* key)
		{
			for (auto& kv : obj)
				if (kv.first == key) return &kv.second;
			return nullptr;
		}

		std::string ReadFileAll(const std::wstring& path, std::string& err)
		{
			std::ifstream f(path, std::ios::binary);
			if (!f) { err = "open failed"; return {}; }
			std::ostringstream ss;
			ss << f.rdbuf();
			return ss.str();
		}
	}

	Registry& Registry::Get()
	{
		static Registry instance;
		return instance;
	}

	ReplacementEntry* Registry::FindByRuntimeSha1(const Sha1Result& s) noexcept
	{
		if (Sha1IsZero(s))
			return nullptr;

		for (auto& e : _entries) {
			if (!e->runtime_sha1_known) continue;
			if (e->runtime_sha1.bytes == s.bytes) return e.get();
		}
		return nullptr;
	}

	bool Registry::LoadFromJson(const std::wstring& path, const std::wstring& shaders_root)
	{
		_entries.clear();
		_loaded = false;
		_lastError.clear();

		std::string err;
		auto text = ReadFileAll(path, err);
		if (!err.empty()) {
			_lastError = "Manifest read error: " + err;
			L->warn("ShaderReplacement manifest not loaded ({}).", _lastError);
			return false;
		}

		std::shared_ptr<JsonValue> root;
		try {
			JsonParser p(std::move(text));
			root = p.Parse();
		} catch (const std::exception& ex) {
			_lastError = std::string("JSON parse error: ") + ex.what();
			L->error("ShaderReplacement manifest parse failed: {}", _lastError);
			return false;
		}

		if (!root || !root->is_object()) {
			_lastError = "Root is not an object";
			L->error("ShaderReplacement manifest invalid: {}", _lastError);
			return false;
		}

		const auto& obj = root->as_object();
		const auto* repls = GetField(obj, "replacements");
		if (!repls || !(*repls) || !(*repls)->is_array()) {
			_lastError = "Missing 'replacements' array";
			L->error("ShaderReplacement manifest invalid: {}", _lastError);
			return false;
		}

		const std::filesystem::path root_dir(shaders_root);

		for (const auto& item : (*repls)->as_array()) {
			if (!item || !item->is_object()) {
				L->warn("ShaderReplacement entry skipped (not an object).");
				continue;
			}
			const auto& eo = item->as_object();

			auto get_str = [&](const char* k) -> std::string {
				auto* f = GetField(eo, k);
				if (!f || !*f || !(*f)->is_string()) return {};
				return (*f)->as_string();
			};

			auto e = std::make_unique<ReplacementEntry>();
			e->name    = get_str("name");
			e->entry   = get_str("entry");   if (e->entry.empty())   e->entry = "main";
			e->profile = get_str("profile"); if (e->profile.empty()) e->profile = "ps_5_0";
			e->comment = get_str("comment");

			if (e->name.empty()) {
				L->warn("ShaderReplacement entry skipped (no 'name').");
				continue;
			}

			auto* sha_f = GetField(eo, "runtime_sha1_hex");
			if (sha_f && *sha_f) {
				if ((*sha_f)->is_string()) {
					const auto& hex = (*sha_f)->as_string();
					if (!hex.empty()) {
						if (!Sha1FromHex(hex, e->runtime_sha1)) {
							L->warn("Entry '{}' has invalid runtime_sha1_hex; treated as unknown.", e->name);
						} else {
							e->runtime_sha1_known = true;
						}
					}
				} else if (!(*sha_f)->is_null()) {
					L->warn("Entry '{}' has non-string runtime_sha1_hex; treated as unknown.", e->name);
				}
			}

			auto hlsl_rel = get_str("hlsl");
			if (hlsl_rel.empty()) {
				L->warn("Entry '{}' skipped (no 'hlsl').", e->name);
				continue;
			}
			std::filesystem::path hlsl_path;
			std::filesystem::path rel(hlsl_rel);
			// Strip a leading Shaders/ prefix so paths resolve under shaders_root.
			std::string rel_str = rel.generic_string();
			if (rel_str.rfind("Shaders/", 0) == 0)
				rel = std::filesystem::path(rel_str.substr(std::string("Shaders/").size()));
			hlsl_path = root_dir / rel;
			e->hlsl_abs_path = hlsl_path.wstring();

			auto* def_f = GetField(eo, "default_enabled");
			if (def_f && *def_f && (*def_f)->is_bool())
				e->default_enabled = (*def_f)->as_bool();

			auto* defs_f = GetField(eo, "defines");
			if (defs_f && *defs_f && (*defs_f)->is_array()) {
				for (const auto& d : (*defs_f)->as_array()) {
					if (!d || !d->is_object()) continue;
					const auto& dobj = d->as_object();
					auto* nm = GetField(dobj, "name");
					auto* vl = GetField(dobj, "value");
					if (nm && *nm && (*nm)->is_string() && vl && *vl && (*vl)->is_string())
						e->defines.emplace_back((*nm)->as_string(), (*vl)->as_string());
				}
			}

			_entries.push_back(std::move(e));
		}

		_loaded = true;
		L->info("Manifest loaded: {} entries ({} with known runtime sha1).",
			_entries.size(),
			[&]() { std::size_t n = 0; for (auto& e : _entries) if (e->runtime_sha1_known) ++n; return n; }());
		return true;
	}
}

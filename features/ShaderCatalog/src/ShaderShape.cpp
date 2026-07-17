#include "ShaderShape.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>

namespace cs::features::catalog
{
	namespace
	{
		// Minimal COM holder so this TU stays free of WRL/winrt.
		struct ComDeleter
		{
			void operator()(IUnknown* p) const noexcept
			{
				if (p)
					p->Release();
			}
		};
		template <class T>
		using ComPtr = std::unique_ptr<T, ComDeleter>;

		std::string ToLower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		std::string ProfileString(UINT version)
		{
			const UINT type = D3D11_SHVER_GET_TYPE(version);
			const UINT major = D3D11_SHVER_GET_MAJOR(version);
			const UINT minor = D3D11_SHVER_GET_MINOR(version);
			const char* prefix = "xx";
			switch (type) {
				case D3D11_SHVER_PIXEL_SHADER:    prefix = "ps"; break;
				case D3D11_SHVER_VERTEX_SHADER:   prefix = "vs"; break;
				case D3D11_SHVER_GEOMETRY_SHADER: prefix = "gs"; break;
				case D3D11_SHVER_HULL_SHADER:     prefix = "hs"; break;
				case D3D11_SHVER_DOMAIN_SHADER:   prefix = "ds"; break;
				case D3D11_SHVER_COMPUTE_SHADER:  prefix = "cs"; break;
				default: break;
			}
			return std::string(prefix) + "_" + std::to_string(major) + "_" + std::to_string(minor);
		}

		const char* SrvDimensionToken(D3D_SRV_DIMENSION d)
		{
			switch (d) {
				case D3D_SRV_DIMENSION_BUFFER:            return "buf";
				case D3D_SRV_DIMENSION_TEXTURE1D:         return "tex1d";
				case D3D_SRV_DIMENSION_TEXTURE1DARRAY:    return "tex1darray";
				case D3D_SRV_DIMENSION_TEXTURE2D:         return "tex2d";
				case D3D_SRV_DIMENSION_TEXTURE2DARRAY:    return "tex2darray";
				case D3D_SRV_DIMENSION_TEXTURE2DMS:       return "tex2dms";
				case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY:  return "tex2dmsarray";
				case D3D_SRV_DIMENSION_TEXTURE3D:         return "tex3d";
				case D3D_SRV_DIMENSION_TEXTURECUBE:       return "texcube";
				case D3D_SRV_DIMENSION_TEXTURECUBEARRAY:  return "texcubearray";
				case D3D_SRV_DIMENSION_BUFFEREX:          return "raw";
				default:                                  return "";
			}
		}

		// dcl_resource_<name> dimension keyword to a compact token.
		std::string DisasmDimToken(const std::string& nameLower)
		{
			if (nameLower == "raw")        return "raw";
			if (nameLower == "structured") return "struct";
			if (nameLower == "buffer")     return "buf";
			if (nameLower == "texture1d")        return "tex1d";
			if (nameLower == "texture1darray")   return "tex1darray";
			if (nameLower == "texture2d")        return "tex2d";
			if (nameLower == "texture2darray")   return "tex2darray";
			if (nameLower == "texture2dms")      return "tex2dms";
			if (nameLower == "texture2dmsarray") return "tex2dmsarray";
			if (nameLower == "texture3d")        return "tex3d";
			if (nameLower == "texturecube")      return "texcube";
			if (nameLower == "texturecubearray") return "texcubearray";
			return "";
		}

		std::string MaskToString(BYTE mask)
		{
			std::string s;
			if (mask & 0x1) s += 'x';
			if (mask & 0x2) s += 'y';
			if (mask & 0x4) s += 'z';
			if (mask & 0x8) s += 'w';
			return s;
		}

		// Occupied-register maps unioned from reflection + disassembly.
		struct RegisterMaps
		{
			std::set<UINT>              cb;
			std::map<UINT, std::string> srv;  // register -> dimension token ("" if unknown)
			std::set<UINT>              sampler;
			std::set<UINT>              comparisonSampler;
			std::set<UINT>              uav;
		};

		void AddRange(std::set<UINT>& set, UINT base, UINT bindCount)
		{
			const UINT n = bindCount == 0 ? 1u : std::min<UINT>(bindCount, 256u);
			for (UINT i = 0; i < n; ++i)
				set.insert(base + i);
		}

		void CollectReflectionResources(ID3D11ShaderReflection* refl, const D3D11_SHADER_DESC& desc, RegisterMaps& maps)
		{
			for (UINT i = 0; i < desc.BoundResources; ++i) {
				D3D11_SHADER_INPUT_BIND_DESC b{};
				if (FAILED(refl->GetResourceBindingDesc(i, &b)))
					continue;
				const UINT n = b.BindCount == 0 ? 1u : std::min<UINT>(b.BindCount, 256u);
				switch (b.Type) {
					case D3D_SIT_CBUFFER:
						AddRange(maps.cb, b.BindPoint, b.BindCount);
						break;
					case D3D_SIT_SAMPLER:
						AddRange(maps.sampler, b.BindPoint, b.BindCount);
						break;
					case D3D_SIT_TEXTURE:
						for (UINT k = 0; k < n; ++k)
							maps.srv.emplace(b.BindPoint + k, SrvDimensionToken(b.Dimension));
						break;
					case D3D_SIT_TBUFFER:
						for (UINT k = 0; k < n; ++k)
							maps.srv.emplace(b.BindPoint + k, "tbuf");
						break;
					case D3D_SIT_STRUCTURED:
						for (UINT k = 0; k < n; ++k)
							maps.srv.emplace(b.BindPoint + k, "struct");
						break;
					case D3D_SIT_BYTEADDRESS:
						for (UINT k = 0; k < n; ++k)
							maps.srv.emplace(b.BindPoint + k, "raw");
						break;
					case D3D_SIT_UAV_RWTYPED:
					case D3D_SIT_UAV_RWSTRUCTURED:
					case D3D_SIT_UAV_RWBYTEADDRESS:
					case D3D_SIT_UAV_APPEND_STRUCTURED:
					case D3D_SIT_UAV_CONSUME_STRUCTURED:
					case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
						AddRange(maps.uav, b.BindPoint, b.BindCount);
						break;
					default:
						break;
				}
			}
		}

		int CountSampleOps(const std::string& text)
		{
			// Opcodes sit at column 0 (no numbering); anchoring at line start avoids comment false positives.
			static const std::regex re(
				R"(^(sample\w*|gather4\w*|lod)\b)",
				std::regex_constants::ECMAScript | std::regex_constants::icase | std::regex_constants::multiline);
			int count = 0;
			for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it)
				++count;
			return count;
		}

		// Parse dcl_* declarations; disassembly is authoritative for presence and SRV dimension.
		void CollectDisassemblyDecls(const std::string& text, RegisterMaps& maps)
		{
			const auto flags = std::regex_constants::ECMAScript | std::regex_constants::icase;

			static const std::regex cbRe(R"(dcl_constantbuffer\s+CB(\d+)\[)", flags);
			for (std::sregex_iterator it(text.begin(), text.end(), cbRe), end; it != end; ++it)
				maps.cb.insert(static_cast<UINT>(std::stoul((*it)[1].str())));

			static const std::regex sampRe(R"(dcl_sampler\s+s(\d+)\s*,\s*(mode_[a-z]+))", flags);
			for (std::sregex_iterator it(text.begin(), text.end(), sampRe), end; it != end; ++it) {
				const UINT reg = static_cast<UINT>(std::stoul((*it)[1].str()));
				maps.sampler.insert(reg);
				if (ToLower((*it)[2].str()) == "mode_comparison")
					maps.comparisonSampler.insert(reg);
			}

			static const std::regex resRe(R"(dcl_resource_([a-z0-9]+)[^\r\n]*\bt(\d+)\b)", flags);
			for (std::sregex_iterator it(text.begin(), text.end(), resRe), end; it != end; ++it) {
				const std::string tok = DisasmDimToken(ToLower((*it)[1].str()));
				const UINT reg = static_cast<UINT>(std::stoul((*it)[2].str()));
				maps.srv[reg] = tok;  // authoritative: add missing, overwrite dimension on disagreement
			}

			static const std::regex uavRe(R"(dcl_uav_[a-z0-9_]+[^\r\n]*\bu(\d+)\b)", flags);
			for (std::sregex_iterator it(text.begin(), text.end(), uavRe), end; it != end; ++it)
				maps.uav.insert(static_cast<UINT>(std::stoul((*it)[1].str())));
		}

		int CountDistinctRegisters(const std::string& text, const std::regex& re)
		{
			std::set<UINT> regs;
			for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it)
				regs.insert(static_cast<UINT>(std::stoul((*it)[1].str())));
			return static_cast<int>(regs.size());
		}

		std::string BuildSignatureSummary(ID3D11ShaderReflection* refl, UINT count, bool input)
		{
			std::string out;
			for (UINT i = 0; i < count; ++i) {
				D3D11_SIGNATURE_PARAMETER_DESC p{};
				const HRESULT hr = input ? refl->GetInputParameterDesc(i, &p) : refl->GetOutputParameterDesc(i, &p);
				if (FAILED(hr))
					continue;
				if (!out.empty())
					out += ';';
				out += (p.SemanticName ? p.SemanticName : "");
				out += std::to_string(p.SemanticIndex);
				out += ':';
				out += MaskToString(p.Mask);
				out += '@';
				out += std::to_string(p.Register);
			}
			return out;
		}

		int ComputePositionOnly(ID3D11ShaderReflection* refl, UINT inputCount)
		{
			if (inputCount != 1)
				return 0;
			D3D11_SIGNATURE_PARAMETER_DESC p{};
			if (FAILED(refl->GetInputParameterDesc(0, &p)))
				return 0;
			const std::string sem = ToLower(p.SemanticName ? p.SemanticName : "");
			return (sem == "position" || sem == "sv_position") ? 1 : 0;
		}

		std::string BuildResourceSummary(const RegisterMaps& maps)
		{
			std::string s;
			auto append = [&](const std::string& tok) {
				if (!s.empty())
					s += ';';
				s += tok;
			};
			for (UINT b : maps.cb)
				append("cb" + std::to_string(b));
			for (const auto& [reg, tok] : maps.srv)
				append(tok.empty() ? "t" + std::to_string(reg) : "t" + std::to_string(reg) + ":" + tok);
			for (UINT s0 : maps.sampler)
				append(maps.comparisonSampler.count(s0) ? "s" + std::to_string(s0) + ":cmp" : "s" + std::to_string(s0));
			for (UINT u : maps.uav)
				append("u" + std::to_string(u));
			return s;
		}
	}

	bool ExtractShaderShape(const void* dxbc, std::size_t len, ShaderShape& out)
	{
		if (!dxbc || len == 0)
			return false;

		ID3D11ShaderReflection* reflRaw = nullptr;
		bool reflectOk = SUCCEEDED(D3DReflect(dxbc, len, __uuidof(ID3D11ShaderReflection),
								 reinterpret_cast<void**>(&reflRaw))) && reflRaw;
		ComPtr<ID3D11ShaderReflection> refl(reflRaw);
		D3D11_SHADER_DESC desc{};
		if (reflectOk && FAILED(refl->GetDesc(&desc)))
			reflectOk = false;

		ID3DBlob* blobRaw = nullptr;
		const bool disasmOk = SUCCEEDED(D3DDisassemble(dxbc, len, 0, nullptr, &blobRaw)) && blobRaw;
		ComPtr<ID3DBlob> blob(blobRaw);
		std::string text;
		if (disasmOk)
			text.assign(static_cast<const char*>(blob->GetBufferPointer()), blob->GetBufferSize());

		if (!reflectOk && !disasmOk)
			return false;
		out.extracted = true;

		RegisterMaps maps;
		if (reflectOk)
			CollectReflectionResources(refl.get(), desc, maps);
		if (disasmOk)
			CollectDisassemblyDecls(text, maps);

		out.cb_count      = static_cast<int>(maps.cb.size());
		out.srv_count     = static_cast<int>(maps.srv.size());
		out.uav_count     = static_cast<int>(maps.uav.size());
		out.sampler_count = static_cast<int>(maps.sampler.size());
		out.resource_summary = BuildResourceSummary(maps);

		if (disasmOk)
			out.sample_call_count = CountSampleOps(text);

		if (reflectOk) {
			out.profile           = ProfileString(desc.Version);
			out.instruction_count = static_cast<int>(desc.InstructionCount);
			out.input_count       = static_cast<int>(desc.InputParameters);
			out.output_count      = static_cast<int>(desc.OutputParameters);
			out.input_signature_summary  = BuildSignatureSummary(refl.get(), desc.InputParameters, true);
			out.output_signature_summary = BuildSignatureSummary(refl.get(), desc.OutputParameters, false);
			out.input_has_position_only  = ComputePositionOnly(refl.get(), desc.InputParameters);
		} else if (disasmOk) {
			// Reflection unavailable: recover counts from declarations; signatures stay NULL.
			static const std::regex inRe(R"(dcl_input\w*[^\r\n]*\bv(\d+)\b)",
				std::regex_constants::ECMAScript | std::regex_constants::icase);
			static const std::regex outRe(R"(dcl_output\w*[^\r\n]*\bo(\d+)\b)",
				std::regex_constants::ECMAScript | std::regex_constants::icase);
			out.input_count  = CountDistinctRegisters(text, inRe);
			out.output_count = CountDistinctRegisters(text, outRe);
		}

		return true;
	}
}

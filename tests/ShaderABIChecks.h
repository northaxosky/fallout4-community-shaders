#pragma once

#include <filesystem>
#include <string>

// Empty on success: C++ constant-buffer layouts match what the staged HLSL reflects.
std::string VerifyShaderABI(const std::filesystem::path& a_root);

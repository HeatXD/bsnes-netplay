#pragma once

#include <string>
#include <vector>

struct ShaderNode {
  std::string name;
  std::string value;
  std::vector<ShaderNode> children;

  const ShaderNode* find(const char* key) const;
  std::string text(const char* key) const;
};

ShaderNode parseShaderManifest(const std::string& text);

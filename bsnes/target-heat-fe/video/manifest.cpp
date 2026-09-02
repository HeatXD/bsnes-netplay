#include "manifest.hpp"

#include <utility>

namespace {

struct ManifestLine {
  int indent = 0;
  std::string name;
  std::string value;
};

std::string trimmed(const std::string& text) {
  const size_t first = text.find_first_not_of(" \t\r");
  if(first == std::string::npos) { return {}; }
  return text.substr(first, text.find_last_not_of(" \t\r") + 1 - first);
}

std::vector<ManifestLine> splitManifest(const std::string& text) {
  std::vector<ManifestLine> lines;
  size_t pos = 0;
  while(pos < text.size()) {
    size_t end = text.find('\n', pos);
    if(end == std::string::npos) { end = text.size(); }
    const std::string raw = text.substr(pos, end - pos);
    pos = end + 1;
    const size_t first = raw.find_first_not_of(" \t\r");
    if(first == std::string::npos || raw.compare(first, 2, "//") == 0) { continue; }

    ManifestLine line;
    line.indent = (int)first;
    const std::string body = trimmed(raw);
    const size_t colon = body.find(':');
    if(colon == std::string::npos) {
      line.name = body;
    } else {
      line.name = trimmed(body.substr(0, colon));
      line.value = trimmed(body.substr(colon + 1));
    }
    lines.push_back(std::move(line));
  }
  return lines;
}

size_t buildNodes(const std::vector<ManifestLine>& lines, size_t index, int indent,
                  std::vector<ShaderNode>& out) {
  while(index < lines.size() && lines[index].indent >= indent) {
    if(out.empty() && lines[index].indent > indent) { break; }
    if(lines[index].indent > indent) {
      index = buildNodes(lines, index, lines[index].indent, out.back().children);
      continue;
    }
    ShaderNode node;
    node.name = lines[index].name;
    node.value = lines[index].value;
    out.push_back(std::move(node));
    index++;
  }
  return index;
}

}  // namespace

const ShaderNode* ShaderNode::find(const char* key) const {
  for(const ShaderNode& child : children) {
    if(child.name == key) { return &child; }
  }
  return nullptr;
}

std::string ShaderNode::text(const char* key) const {
  const ShaderNode* child = find(key);
  return child ? child->value : std::string();
}

ShaderNode parseShaderManifest(const std::string& text) {
  const std::vector<ManifestLine> lines = splitManifest(text);
  ShaderNode root;
  buildNodes(lines, 0, 0, root.children);
  return root;
}

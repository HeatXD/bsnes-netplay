#include "ui.hpp"

namespace {
ImU32 imguiColor(uint32_t argb) {
  return IM_COL32((argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff, argb >> 24);
}
}  // namespace

void LuaEngine::drawOverlay() {
  if((commands.empty() && windows.empty()) || app.shell.drawWidth <= 0 ||
     app.shell.drawHeight <= 0) {
    return;
  }

  const float sx = app.shell.drawWidth / 256.0f;
  const float sy = app.shell.drawHeight / videoHeight(app.settings);
  const ImVec2 origin(app.shell.drawX, app.shell.drawY);
  auto point = [&](float x, float y) { return ImVec2(origin.x + x * sx, origin.y + y * sy); };

  ImDrawList* draw = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
  for(const DrawCommand& command : commands) {
    ImVec2 p1 = point(command.x1, command.y1);
    if(command.type == DrawCommand::Box) {
      const ImVec2 p2 = point(command.x2, command.y2);
      if(command.color >> 24) { draw->AddRectFilled(p1, p2, imguiColor(command.color)); }
      if(command.outline >> 24) {
        draw->AddRect(p1, p2, imguiColor(command.outline), 0.0f, 0,
                      SDL_max(1.0f, command.thickness * sy));
      }
    } else if(command.type == DrawCommand::Ellipse) {
      const ImVec2 radius(command.x2 * sx, command.y2 * sy);
      if(command.color >> 24) { draw->AddEllipseFilled(p1, radius, imguiColor(command.color)); }
      if(command.outline >> 24) {
        draw->AddEllipse(p1, radius, imguiColor(command.outline), 0.0f, 0,
                         SDL_max(1.0f, command.thickness * sy));
      }
    } else if(command.type == DrawCommand::Line) {
      draw->AddLine(p1, point(command.x2, command.y2), imguiColor(command.color),
                    SDL_max(1.0f, command.thickness * sy));
    } else if(command.type == DrawCommand::Pixel) {
      draw->AddRectFilled(p1, point(command.x1 + 1.0f, command.y1 + 1.0f),
                          imguiColor(command.color));
    } else {
      const float size = command.size * sy;
      ImFont* font = command.pixelFont && app.luaPixelFont ? app.luaPixelFont : ImGui::GetFont();
      const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, command.text.c_str());
      if(command.align == DrawCommand::Center) {
        p1.x -= extent.x * 0.5f;
      } else if(command.align == DrawCommand::Right) {
        p1.x -= extent.x;
      }
      if(command.outline >> 24) {
        for(int y = -1; y <= 1; y++) {
          for(int x = -1; x <= 1; x++) {
            if(x || y) {
              draw->AddText(font, size, ImVec2(p1.x + x, p1.y + y), imguiColor(command.outline),
                            command.text.c_str());
            }
          }
        }
      }
      draw->AddText(font, size, p1, imguiColor(command.color), command.text.c_str());
    }
  }

  for(const WindowCommand& window : windows) {
    if(window.width > 0.0f || window.height > 0.0f) {
      ImGui::SetNextWindowSize(ImVec2(window.width, window.height), ImGuiCond_FirstUseEver);
    }
    const std::string id = window.title + "###lua-window-" + window.title;
    const ImGuiWindowFlags flags =
        window.width == 0.0f && window.height == 0.0f ? ImGuiWindowFlags_AlwaysAutoResize : 0;
    if(ImGui::Begin(id.c_str(), nullptr, flags)) {
      for(const WindowWidget& widget : window.widgets) {
        if(widget.type == WindowWidget::Label) {
          ImGui::TextUnformatted(widget.text.c_str());
        } else {
          const std::string label = widget.text + "###" + widget.key;
          if(ImGui::Button(label.c_str(), ImVec2(widget.width, widget.height))) {
            clickedWidgets.insert(widget.key);
          }
        }
      }
    }
    ImGui::End();
  }
}

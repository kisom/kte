#include "GUIRenderer.h"

#include "Editor.h"
#include "Buffer.h"

#include <imgui.h>
#include <cstdio>

void GUIRenderer::Draw(const Editor &ed)
{
    // Make the editor window occupy the entire GUI container/viewport
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                           | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoSavedSettings
                           | ImGuiWindowFlags_NoBringToFrontOnFocus
                           | ImGuiWindowFlags_NoNavFocus;

    // Reduce padding so the buffer content uses the whole area
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 6.f));

    ImGui::Begin("kte", nullptr, flags);

    const Buffer *buf = ed.CurrentBuffer();
    if (!buf) {
        ImGui::TextUnformatted("[no buffer]");
    } else {
        const auto &lines = buf->Rows();
        // Reserve space for status bar at bottom
        ImGui::BeginChild("scroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false, ImGuiWindowFlags_HorizontalScrollbar);
        std::size_t rowoffs = buf->Rowoffs();
        std::size_t cy = buf->Cury();
        std::size_t cx = buf->Curx();
        const float line_h = ImGui::GetTextLineHeight();
        const float space_w = ImGui::CalcTextSize(" ").x;
        for (std::size_t i = rowoffs; i < lines.size(); ++i) {
            // Capture the screen position before drawing the line
            ImVec2 line_pos = ImGui::GetCursorScreenPos();
            const std::string &line = lines[i];
            ImGui::TextUnformatted(line.c_str());

            // Draw a visible cursor indicator on the current line
            if (i == cy) {
                // Compute X offset by measuring text width up to cursor column
                std::size_t px_count = std::min(cx, line.size());
                ImVec2 pre_sz = ImGui::CalcTextSize(line.c_str(), line.c_str() + static_cast<long>(px_count));
                ImVec2 p0 = ImVec2(line_pos.x + pre_sz.x, line_pos.y);
                ImVec2 p1 = ImVec2(p0.x + space_w, p0.y + line_h);
                ImU32 col = IM_COL32(200, 200, 255, 128); // soft highlight
                ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, col);
            }
        }
        ImGui::EndChild();

        // Status bar spanning full width
        ImGui::Separator();
        const char *fname = (buf->IsFileBacked()) ? buf->Filename().c_str() : "(new)";
        bool dirty = buf->Dirty();
        char status[1024];
        snprintf(status, sizeof(status), " %s%s  %zux%zu  %s ",
                 fname,
                 dirty ? "*" : "",
                 ed.Rows(), ed.Cols(),
                 ed.Status().c_str());

        // Compute full content width and draw a filled background rectangle
        ImVec2 win_pos = ImGui::GetWindowPos();
        ImVec2 cr_min = ImGui::GetWindowContentRegionMin();
        ImVec2 cr_max = ImGui::GetWindowContentRegionMax();
        float x0 = win_pos.x + cr_min.x;
        float x1 = win_pos.x + cr_max.x;
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        float bar_h = ImGui::GetFrameHeight();
        ImVec2 p0(x0, cursor.y);
        ImVec2 p1(x1, cursor.y + bar_h);
        ImU32 bg_col = ImGui::GetColorU32(ImGuiCol_HeaderActive);
        ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, bg_col);
        // Place status text within the bar
        ImVec2 text_sz = ImGui::CalcTextSize(status);
        ImGui::SetCursorScreenPos(ImVec2(p0.x + 6.f, p0.y + (bar_h - text_sz.y) * 0.5f));
        ImGui::TextUnformatted(status);
        // Advance cursor to after the bar to keep layout consistent
        ImGui::Dummy(ImVec2(x1 - x0, bar_h));
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
}

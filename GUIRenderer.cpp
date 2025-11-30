#include "GUIRenderer.h"

#include "Editor.h"
#include "Buffer.h"

#include <imgui.h>

void GUIRenderer::Draw(const Editor &ed)
{
    ImGui::Begin("kte");

    const Buffer *buf = ed.CurrentBuffer();
    if (!buf) {
        ImGui::TextUnformatted("[no buffer]");
    } else {
        const auto &lines = buf->Rows();
        // Reserve space for status bar at bottom
        ImGui::BeginChild("scroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false, ImGuiWindowFlags_HorizontalScrollbar);
        std::size_t rowoffs = buf->Rowoffs();
        for (std::size_t i = rowoffs; i < lines.size(); ++i) {
            ImGui::TextUnformatted(lines[i].c_str());
        }
        ImGui::EndChild();

        // Status bar
        ImGui::Separator();
        const char *fname = (buf->IsFileBacked()) ? buf->Filename().c_str() : "(new)";
        bool dirty = buf->Dirty();
        ImGui::Text("%s%s  %zux%zu  %s",
                    fname,
                    dirty ? "*" : "",
                    ed.Rows(), ed.Cols(),
                    ed.Status().c_str());
    }

    ImGui::End();
}

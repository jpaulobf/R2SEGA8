#include "MemoryInspector.h"
#include <algorithm>
#include <imgui.h>

namespace R2SEGA8::Window
{
    namespace
    {
        void drawBytes(const char *label, ByteView bytes)
        {
            if (!ImGui::CollapsingHeader(label))
                return;
            if (ImGui::BeginChild(label, ImVec2(0, 180), ImGuiChildFlags_Borders))
            {
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>((bytes.size + 15) / 16));
                while (clipper.Step())
                {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                    {
                        const std::size_t start = static_cast<std::size_t>(row) * 16;
                        ImGui::Text("%06X:", static_cast<unsigned>(start));
                        for (std::size_t i = start; i < std::min(start + 16, bytes.size); ++i)
                        {
                            ImGui::SameLine();
                            ImGui::Text("%02X", static_cast<unsigned>(bytes.data[i]));
                        }
                    }
                }
            }
            ImGui::EndChild();
        }
    } // namespace

    void MemoryInspector::draw(MemoryView memory)
    {
        if (memory.rom.data)
            drawBytes("Cartridge ROM (file offsets)", memory.rom);
        if (memory.ram.data)
            drawBytes("System RAM (physical offsets)", memory.ram);
    }
}
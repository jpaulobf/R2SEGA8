#include "KeyboardDialog.h"
#include <imgui.h>
#include <string>

namespace R2Keyboard
{
    void KeyboardDialog::setMapping(const Mapping &mapping)
    {
        mapping_ = mapping;
    }

    const Mapping &KeyboardDialog::mapping() const
    {
        return mapping_;
    }

    void KeyboardDialog::open(const std::array<int, 10> &order)
    {
        order_ = order;
        draft_ = mapping_;
        capture_ = -1;
        pad_ = 0;
        error_ = false;
        requested_ = true;
    }

    bool KeyboardDialog::active() const
    {
        return requested_ || open_;
    }

    bool KeyboardDialog::processEvent(const SDL_Event &event, SDL_WindowID id)
    {
        if (!active() || event.type != SDL_EVENT_KEY_DOWN || event.key.windowID != id)
            return false;
        if (!event.key.repeat && capture_ >= 0 && assignable(event.key.key))
        {
            // A tecla escolhida deixa de pertencer às demais funções dos dois controles.
            for (auto &pad : draft_)
                for (auto &key : pad)
                    if (key == event.key.key)
                        key = 0;
            draft_[pad_][capture_] = event.key.key;
            capture_ = -1;
        }
        return true;
    }

    void KeyboardDialog::draw(const Saver &save)
    {
        if (requested_)
        {
            ImGui::OpenPopup("Configure Keyboard");
            requested_ = false;
            open_ = true;
        }
        ImGui::SetNextWindowSize(ImVec2(430, 390), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Configure Keyboard", &open_, ImGuiWindowFlags_NoResize))
            return;
        ImGui::TextUnformatted("Controller");
        ImGui::SameLine(150);
        ImGui::TextUnformatted("Button");
        ImGui::SameLine(270);
        ImGui::TextUnformatted("Mapping");
        ImGui::BeginChild("controllers", ImVec2(130, 250), ImGuiChildFlags_Borders);
        for (int p = 0; p < 2; ++p)
            if (ImGui::Selectable(p ? "Pad 2" : "Pad 1", p == pad_))
            {
                pad_ = p;
                capture_ = -1;
            }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("bindings", ImVec2(0, 250), ImGuiChildFlags_Borders);
        constexpr const char *names[] = {"Up", "Down", "Left", "Right", "1", "2", "Turbo 1", "Turbo 2", "Start", "Select"};
        for (int b = 0; b < 10; ++b)
        {
            ImGui::PushID(b);
            // Usa a associação correspondente à função exibida após a inversão.
            const int source = order_[b];
            ImGui::TextUnformatted(names[b]);
            ImGui::SameLine(120);
            const auto key = draft_[pad_][source];
            const char *label = capture_ == source || !key ? "" : SDL_GetKeyName(key);
            if (ImGui::Button((std::string(label) + "###key").c_str(), ImVec2(-1, 0)))
                capture_ = source;
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::TextUnformatted(capture_ >= 0 ? "Press a key..." : "Select a mapping to change it.");
        ImGui::TextDisabled("Some emulator shortcuts and modifiers are reserved.");
        if (ImGui::Button("Cancel"))
        {
            open_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Default"))
        {
            draft_ = defaults();
            capture_ = -1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Save"))
        {
            // Uma falha preserva o rascunho para que o usuário possa tentar novamente.
            if (save(draft_))
            {
                mapping_ = draft_;
                open_ = false;
                ImGui::CloseCurrentPopup();
            }
            else
                error_ = true;
        }
        if (error_)
            ImGui::TextWrapped("Could not save keyboard settings.");
        ImGui::EndPopup();
    }
}
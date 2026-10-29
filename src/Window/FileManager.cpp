#include "FileManager.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace R2SEGA8::Window
{
    FileManager::FileManager(Callbacks::InspectRom inspectHandler, Callbacks::OpenRom openHandler)
        : inspectHandler_(std::move(inspectHandler)), openHandler_(std::move(openHandler))
    {
        std::error_code error;
        directory_ = std::filesystem::current_path(error);
        if (error)
            directory_ = ".";
    }

    void FileManager::navigate(const std::filesystem::path &path)
    {
        const auto absolute = std::filesystem::absolute(path).lexically_normal();
        std::vector<Entry> entries;
        // Atualiza a lista apenas ao navegar, não a cada quadro. Falhas preservam a pasta anterior.
        for (const auto &item : std::filesystem::directory_iterator(absolute))
        {
            std::error_code error;
            const bool directory = item.is_directory(error);
            if (error)
                continue;
            if (!directory && !item.is_regular_file(error))
                continue;
            if (error)
                continue;
            try
            {
                if (!directory)
                {
                    auto extension = item.path().extension().u8string();
                    std::transform(extension.begin(), extension.end(), extension.begin(),
                                   [](unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
                    if (extension != ".zip" && extension != ".sg" && extension != ".sc" &&
                        extension != ".sms" && extension != ".gg" && extension != ".bin")
                        continue;
                }
                entries.push_back({item.path(), item.path().filename().u8string(), directory});
            }
            catch (const std::filesystem::filesystem_error &)
            { /* Codificação do nome do arquivo não suportada. */
            }
        }
        std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b)
                  { return a.directory != b.directory ? a.directory > b.directory : a.name < b.name; });
        const auto encoded = absolute.u8string();
        directory_ = absolute;
        entries_ = std::move(entries);
        candidate_.reset();
        selected_ = Common::SystemType::Unknown;
        std::snprintf(pathInput_.data(), pathInput_.size(), "%s", encoded.c_str());
        error_.clear();
    }

    void FileManager::inspect(const std::filesystem::path &path)
    {
        candidate_.reset();
        selected_ = Common::SystemType::Unknown;
        if (!inspectHandler_)
            throw std::runtime_error("ROM inspection is unavailable.");
        candidate_ = inspectHandler_(path);
        selected_ = candidate_->suggestedSystem;
        const auto encoded = candidate_->path.u8string();
        std::snprintf(pathInput_.data(), pathInput_.size(), "%s", encoded.c_str());
        error_.clear();
    }

    void FileManager::openPath()
    {
        try
        {
            auto path = std::filesystem::u8path(pathInput_.data());
            if (path.is_relative())
                path = directory_ / path;
            if (std::filesystem::is_directory(path))
                navigate(path);
            else
                inspect(path);
        }
        catch (const std::exception &error)
        {
            error_ = error.what();
        }
    }

    void FileManager::requestOpen(const std::filesystem::path &file)
    {
        requested_ = true;
        candidate_.reset();
        selected_ = Common::SystemType::Unknown;
        error_.clear();
        try
        {
            navigate(file.empty() ? directory_ : std::filesystem::absolute(file).parent_path());
            if (!file.empty())
                inspect(file);
        }
        catch (const std::exception &error)
        {
            error_ = error.what();
        }
    }

    void FileManager::drawDriveSelector()
    {
#ifdef _WIN32
        const auto current = directory_.root_name().u8string();
        ImGui::SetNextItemWidth(50.0f);
        if (!ImGui::BeginCombo("Drive", current.empty() ? "Select a drive" : current.c_str()))
            return;

        // Consulta ao abrir a lista para refletir as unidades removíveis e mapeadas disponíveis.
        const DWORD drives = GetLogicalDrives();
        if (drives == 0)
            ImGui::TextUnformatted("Unable to list drives.");
        for (unsigned i = 0; i < 26; ++i)
        {
            if ((drives & (DWORD{1} << i)) == 0)
                continue;
            const char letter = static_cast<char>('A' + i);
            const char root[] = {letter, ':', '\\', '\0'};
            const bool selected = current.size() == 2 && current[1] == ':' &&
                                  std::toupper(static_cast<unsigned char>(current[0])) == letter;
            if (ImGui::Selectable(root, selected))
            {
                try
                {
                    navigate(root);
                }
                catch (const std::exception &)
                {
                    error_ = "The selected drive is unavailable or cannot be accessed.";
                }
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
#endif
    }

    void FileManager::draw()
    {
        if (requested_)
        {
            ImGui::OpenPopup("Open ROM");
            requested_ = false;
        }
        const auto *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(ImVec2(720, 580), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(320, 300), viewport->WorkSize);
        bool visible = true;
        if (!ImGui::BeginPopupModal("Open ROM", &visible))
            return;
        ImGui::TextUnformatted("Raw ROM images: .sg, .sc, .sms, .gg, .bin (ZIP not supported yet)");
        drawDriveSelector();
        // Editar o caminho invalida a prévia anterior, impedindo o carregamento de uma seleção desatualizada.
        if (ImGui::InputText("Path", pathInput_.data(), pathInput_.size()))
        {
            candidate_.reset();
            selected_ = Common::SystemType::Unknown;
        }
        if (ImGui::Button("Up"))
        {
            try
            {
                navigate(directory_.parent_path().empty() ? directory_ : directory_.parent_path());
            }
            catch (const std::exception &error)
            {
                error_ = error.what();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
        {
            try
            {
                navigate(directory_);
            }
            catch (const std::exception &error)
            {
                error_ = error.what();
            }
        }
        std::optional<Entry> selection;
        if (ImGui::BeginChild("Files", ImVec2(0, 180), ImGuiChildFlags_Borders))
        {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(entries_.size()));
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    const auto &entry = entries_[static_cast<std::size_t>(i)];
                    ImGui::PushID(i);
                    // Nomes podem conter o delimitador ## do ImGui; desenha o texto separadamente.
                    const bool clicked = ImGui::Selectable("##entry", candidate_ && candidate_->path == entry.path);
                    const std::string label = (entry.directory ? "[DIR] " : "") + entry.name;
                    ImGui::GetWindowDrawList()->AddText(ImGui::GetItemRectMin(), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
                    if (clicked)
                        selection = entry;
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();
        if (selection)
        {
            try
            {
                if (selection->directory)
                    navigate(selection->path);
                else
                    inspect(selection->path);
            }
            catch (const std::exception &error)
            {
                error_ = error.what();
            }
        }
        if (candidate_)
        {
            ImGui::Text("ROM: %zu bytes", candidate_->size);
            ImGui::Text("Suggested system: %s", Common::systemName(candidate_->suggestedSystem));
            if (candidate_->conflictingHints)
                ImGui::TextWrapped("Extension and Sega header disagree. Confirm the system below.");
            if (!candidate_->hasHeader)
                ImGui::TextWrapped("No Sega header found. This is normal for many early-system ROMs.");
            ImGui::TextWrapped("Identification is a suggestion; loading does not verify game compatibility.");
            if (ImGui::BeginCombo("System", Common::systemName(selected_)))
            {
                for (auto type : {Common::SystemType::SG1000, Common::SystemType::SC3000,
                                  Common::SystemType::MasterSystem, Common::SystemType::GameGear})
                {
                    if (ImGui::Selectable(Common::systemName(type), selected_ == type))
                        selected_ = type;
                }
                ImGui::EndCombo();
            }
        }
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        ImGui::BeginDisabled(!candidate_ || selected_ == Common::SystemType::Unknown);
        if (ImGui::Button("Load ROM"))
        {
            try
            {
                if (!openHandler_)
                    throw std::runtime_error("ROM loading is unavailable.");
                openHandler_(candidate_->path, selected_);
                candidate_.reset();
                error_.clear();
                ImGui::CloseCurrentPopup();
            }
            catch (const std::exception &error)
            {
                error_ = error.what();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            candidate_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
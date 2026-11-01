#include "FileManager.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <imgui.h>
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
    FileManager::FileManager(Callbacks::OpenRom openHandler) : openHandler_(std::move(openHandler))
    {
        std::error_code error;
        directory_ = std::filesystem::current_path(error);
        if (error)
            directory_ = ".";
        readConfig();
    }

    void FileManager::readConfig()
    {
        std::ifstream input("resources/config.ini", std::ios::binary);
        std::string line;
        std::filesystem::path recent[10];
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const auto separator = line.find('=');
            if (separator == std::string::npos)
                continue;
            const auto key = line.substr(0, separator);
            const auto value = line.substr(separator + 1);
            if (key == "last_rom_path" && !value.empty())
                lastRomDirectory_ = std::filesystem::u8path(value);
            for (int i = 0; i < 10; ++i)
                if (key == "f" + std::to_string(i + 1) && !value.empty())
                    recent[i] = std::filesystem::u8path(value);
        }
        for (const auto &path : recent)
            if (!path.empty() && std::find(recentFiles_.begin(), recentFiles_.end(), path) == recentFiles_.end())
                recentFiles_.push_back(path);
        std::error_code error;
        if (!lastRomDirectory_.empty() && std::filesystem::is_directory(lastRomDirectory_, error))
            directory_ = lastRomDirectory_;
    }

    void FileManager::recordRecent(const std::filesystem::path &path)
    {
        const auto absolute = std::filesystem::absolute(path).lexically_normal();
        recentFiles_.erase(std::remove(recentFiles_.begin(), recentFiles_.end(), absolute), recentFiles_.end());
        recentFiles_.insert(recentFiles_.begin(), absolute);
        if (recentFiles_.size() > 10)
            recentFiles_.resize(10);
        lastRomDirectory_ = absolute.parent_path();
        directory_ = lastRomDirectory_;

        std::ifstream input("resources/config.ini", std::ios::binary);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const auto separator = line.find('=');
            const auto key = line.substr(0, separator);
            bool managed = separator != std::string::npos && key == "last_rom_path";
            for (int i = 1; i <= 10; ++i)
                managed = managed || (separator != std::string::npos && key == "f" + std::to_string(i));
            if (!managed)
                lines.push_back(line);
        }
        input.close();
        std::error_code error;
        std::filesystem::create_directories("resources", error);
        std::ofstream output("resources/config.ini", std::ios::binary | std::ios::trunc);
        if (!output)
            return;
        for (const auto &existing : lines)
            output << existing << '\n';
        for (std::size_t i = 0; i < 10; ++i)
            output << 'f' << i + 1 << '=' << (i < recentFiles_.size() ? recentFiles_[i].u8string() : "") << '\n';
        output << "last_rom_path=" << lastRomDirectory_.u8string() << '\n';
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
                    if (extension != ".zip" && extension != ".sg" && extension != ".sc" && extension != ".sms" && extension != ".gg" &&
                        extension != ".bin")
                        continue;
                }
                entries.push_back({item.path(), item.path().filename().u8string(), directory});
            }
            catch (const std::filesystem::filesystem_error &)
            { /* Codificação do nome do arquivo não suportada. */
            }
        }
        std::sort(entries.begin(), entries.end(),
                  [](const Entry &a, const Entry &b)
                  { return a.directory != b.directory ? a.directory > b.directory : a.name < b.name; });
        const auto encoded = absolute.u8string();
        directory_ = absolute;
        entries_ = std::move(entries);
        pathLabel_ = encoded;
        error_.clear();
    }

    void FileManager::load(const std::filesystem::path &path)
    {
        auto extension = path.extension().u8string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        if (extension == ".zip")
            throw std::runtime_error("ZIP archives are not supported yet. Extract a supported ROM first.");
        if (extension == ".bin")
            throw std::runtime_error("Automatic system detection for BIN files is not available yet.");
        const auto system = Common::systemFromExtension(extension);
        if (system == Common::SystemType::Unknown)
            throw std::runtime_error("Unsupported ROM extension.");
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error("The selected ROM file is unavailable.");
        if (!openHandler_)
            throw std::runtime_error("ROM loading is unavailable.");
        openHandler_(path, system);
        try
        {
            recordRecent(path);
        }
        catch (const std::exception &)
        {
            // A falha ao salvar preferências não desfaz uma ROM já carregada.
        }
        error_.clear();
        open_ = false;
        ImGui::CloseCurrentPopup();
    }

    void FileManager::requestOpen(const std::filesystem::path &file)
    {
        requested_ = true;
        pendingFile_.reset();
        error_.clear();
        try
        {
            if (file.empty())
            {
                std::error_code error;
                if (!lastRomDirectory_.empty() && std::filesystem::is_directory(lastRomDirectory_, error))
                    navigate(lastRomDirectory_);
                else
                    navigate(directory_);
            }
            else
            {
                const auto absolute = std::filesystem::absolute(file).lexically_normal();
                if (std::filesystem::is_directory(absolute))
                    navigate(absolute);
                else
                {
                    navigate(absolute.parent_path());
                    pendingFile_ = absolute;
                }
            }
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
            const bool selected =
                current.size() == 2 && current[1] == ':' && std::toupper(static_cast<unsigned char>(current[0])) == letter;
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
        ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(320.0f, viewport->WorkSize.x), std::min(300.0f, viewport->WorkSize.y)),
                                            viewport->WorkSize);
        bool visible = true;
        open_ = ImGui::BeginPopupModal("Open ROM", &visible);
        if (!open_)
            return;
        if (pendingFile_)
        {
            const auto file = std::move(*pendingFile_);
            pendingFile_.reset(); // Consome uma única tentativa, inclusive quando o callback falhar.
            try
            {
                load(file);
            }
            catch (const std::exception &error)
            {
                error_ = error.what();
            }
            if (!open_)
            {
                ImGui::EndPopup();
                return;
            }
        }
        ImGui::TextUnformatted("Click a ROM to load: .sg, .sc, .sms, .gg");
        drawDriveSelector();
        ImGui::TextWrapped("Path: %s", pathLabel_.c_str());
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
        const auto available = ImGui::GetContentRegionAvail();
        const float errorHeight =
            error_.empty() ? 0.0f : ImGui::CalcTextSize(error_.c_str(), nullptr, false, available.x).y + ImGui::GetStyle().ItemSpacing.y;
        if (ImGui::BeginChild("Files", ImVec2(0, std::max(40.0f, available.y - errorHeight)), ImGuiChildFlags_Borders))
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
                    const bool clicked = ImGui::Selectable("##entry", false, ImGuiSelectableFlags_NoAutoClosePopups);
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
                    load(selection->path);
            }
            catch (const std::exception &error)
            {
                error_ = error.what();
            }
        }
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        ImGui::EndPopup();
    }
}
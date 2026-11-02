#pragma once
#include "Common/Buttons.h"
#include "Joypad.h"
#include <stdexcept>

namespace R2SEGA8::Core::IO
{
    // Mantém as intenções dos jogadores e resolve o turbo no quadro emulado.
    class ControllerInput
    {
    public:
        const Common::InputState &buttons() const { return buttons_; }
        std::uint64_t frame() const { return frame_; }
        const Joypad &joypad() const { return joypad_; }

        // Prepara uma atualização atômica dos dois jogadores, preservando o outro jogador.
        Common::InputState withPlayer(unsigned player, Common::ButtonMask buttons) const
        {
            if (player >= buttons_.size())
                throw std::out_of_range("Invalid controller player.");
            auto next = buttons_;
            next[player] = buttons;
            return next;
        }

        // Também restaura os controles de save states sem modificar sinais da CPU.
        void setInput(const Common::InputState &buttons, std::uint64_t frame)
        {
            buttons_ = buttons;
            frame_ = frame;
            apply();
        }

        void updateFrame(std::uint64_t frame)
        {
            if (frame != frame_)
            {
                frame_ = frame;
                apply();
            }
        }

    private:
        void apply()
        {
            for (unsigned player = 0; player < buttons_.size(); ++player)
                joypad_.setButtons(player, Common::controllerButtons(buttons_[player], frame_));
        }

        Joypad joypad_;
        Common::InputState buttons_{};
        std::uint64_t frame_ = 0;
    };
}
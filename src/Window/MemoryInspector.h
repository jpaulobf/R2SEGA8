#pragma once
#include "Callbacks.h"

namespace R2SEGA8::Window
{
	// Apresentação de memória preservada para futuras janelas de Debug, sem dependência do núcleo.
	class MemoryInspector
	{
	public:
		// Desenha as consultas recebidas no contexto ImGui atual, sem reter os ponteiros.
		static void draw(MemoryView memory);
	};
}
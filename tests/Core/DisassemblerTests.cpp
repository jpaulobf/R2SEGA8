#include "Core/CPU/Z80/Disassembler.h"
#include <array>
#include <cassert>
#include <string>
using namespace R2SEGA8::Core;
int main() {
    std::array<std::uint8_t, 65536> m{};
    auto read=[&](std::uint16_t a) { return m[a]; };
    auto check=[&](std::uint16_t address, std::uint16_t next, const std::string &text) { auto d=disassemble(address,read); assert(d.next==next); assert(d.text==text); };
    m[0]=0x01; m[1]=0x34; m[2]=0x12; check(0,3,"LD BC,$1234");
    m[0]=0xdd; m[1]=0x36; m[2]=0xfe; m[3]=0x42; check(0,4,"LD (IX-$02),$42");
    m[0]=0xfd; m[1]=0xcb; m[2]=0x02; m[3]=0x46; check(0,4,"BIT 0,(IY+$02)");
    m[0]=0xed; m[1]=0x4b; m[2]=0x34; m[3]=0x12; check(0,4,"LD BC,($1234)");
    m[0xffff]=0x18; m[0]=0xfe; check(0xffff,1,"JR $FFFF");
    m[0]=0xdd; m[1]=0x66; m[2]=0x05; check(0,3,"LD H,(IX+$05)");
}

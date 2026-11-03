#include "Disassembler.h"
#include <cstdio>

namespace R2SEGA8::Core {
Disassembled disassemble(std::uint16_t address, const std::function<std::uint8_t(std::uint16_t)> &read) {
    std::uint16_t pc=address; std::string bytes;
    auto get=[&]() { auto b=read(pc++); char s[4]; std::snprintf(s,sizeof s,"%02X ",b); bytes+=s; return b; };
    auto hex=[](unsigned v, unsigned n) { char s[12]; std::snprintf(s,sizeof s,n==2?"$%02X":"$%04X",v); return std::string(s); };
    auto word=[&]() { unsigned lo=get(); return lo | (unsigned(get())<<8); };
    unsigned op=get(), index=0, prefixes=0;
    while ((op==0xdd || op==0xfd) && prefixes++ < 16) { index=op==0xdd?1:2; op=get(); }
    if (prefixes>16) return {address,pc,bytes,"PREFIX (continued)"};
    const char *rp0[]={"BC","DE","HL","SP"}, *rp20[]={"BC","DE","HL","AF"};
    const char *cc[]={"NZ","Z","NC","C","PO","PE","P","M"};
    const char *alu[]={"ADD A,","ADC A,","SUB ","SBC A,","AND ","XOR ","OR ","CP "};
    const char *rot[]={"RLC","RRC","RL","RR","SLA","SRA","SLL","SRL"};
    auto pair=[&](unsigned p, bool af=false) { return std::string(p==2 && index ? (index==1?"IX":"IY") : (af?rp20[p]:rp0[p])); };
    std::string mem="(HL)";
    auto reg=[&](unsigned r, bool indexed=true) { const char *names[]={"B","C","D","E","H","L","(HL)","A"}; if(r==6) return mem; if(index && indexed && (r==4||r==5)) return std::string(index==1?(r==4?"IXH":"IXL"):(r==4?"IYH":"IYL")); return std::string(names[r]); };
    auto disp=[&]() { int d=static_cast<std::int8_t>(get()); char s[20]; std::snprintf(s,sizeof s,"(%s%c$%02X)",index==1?"IX":"IY",d<0?'-':'+',d<0?-d:d); mem=s; };
    std::string text;
    if(op==0xcb) {
        if(index) disp();
        unsigned c=get(), x=c>>6,y=(c>>3)&7,z=c&7; std::string target=index?mem:reg(z);
        if(x==0) text=std::string(rot[y])+" "+target;
        else text=std::string(x==1?"BIT ":x==2?"RES ":"SET ")+std::to_string(y)+","+target;
        if(index && z!=6 && x!=1) text+=","+reg(z,false);
    } else if(op==0xed) {
        index=0; unsigned c=get(), x=c>>6,y=(c>>3)&7,z=c&7,p=y>>1,q=y&1;
        if(x==1) switch(z) {
        case 0: text=y==6?"IN (C)":"IN "+reg(y)+",(C)"; break;
        case 1: text="OUT (C),"+(y==6?std::string("0"):reg(y)); break;
        case 2: text=std::string(q?"ADC HL,":"SBC HL,")+pair(p); break;
        case 3: { auto a=hex(word(),4); text=q?"LD "+pair(p)+",("+a+")":"LD ("+a+"),"+pair(p); break; }
        case 4: text="NEG"; break;
        case 5: text=c==0x4d?"RETI":"RETN"; break;
        case 6: { const int modes[]={0,0,1,2,0,0,1,2}; text="IM "+std::to_string(modes[y]); break; }
        case 7: { const char *ops[]={"LD I,A","LD R,A","LD A,I","LD A,R","RRD","RLD","NOP","NOP"}; text=ops[y]; break; }
        } else if(x==2 && y>=4 && z<4) { const char *ops[4][4]={{"LDI","CPI","INI","OUTI"},{"LDD","CPD","IND","OUTD"},{"LDIR","CPIR","INIR","OTIR"},{"LDDR","CPDR","INDR","OTDR"}}; text=ops[y-4][z]; } else text="NOP";
    } else {
        unsigned x=op>>6,y=(op>>3)&7,z=op&7,p=y>>1,q=y&1;
        bool memory=(x==0 && (z==4||z==5||z==6) && y==6)||(x==1 && op!=0x76 && (y==6||z==6))||(x==2 && z==6);
        if(index && memory) disp();
        auto rel=[&]() { int d=static_cast<std::int8_t>(get()); return hex(std::uint16_t(pc+d),4); };
        if(x==0) switch(z) {
        case 0: if(y==0) text="NOP"; else if(y==1) text="EX AF,AF'"; else if(y==2) text="DJNZ "+rel(); else if(y==3) text="JR "+rel(); else text="JR "+std::string(cc[y-4])+","+rel(); break;
        case 1: text=q?"ADD "+pair(2)+","+pair(p):"LD "+pair(p)+","+hex(word(),4); break;
        case 2: if(p<2) text=std::string("LD ")+(q?"A,(":"(")+pair(p)+(q?")": "),A"); else { auto a=hex(word(),4); auto r=p==2?pair(2):std::string("A"); text=q?"LD "+r+",("+a+")":"LD ("+a+"),"+r; } break;
        case 3: text=std::string(q?"DEC ":"INC ")+pair(p); break;
        case 4: text="INC "+reg(y); break;
        case 5: text="DEC "+reg(y); break;
        case 6: text="LD "+reg(y)+","+hex(get(),2); break;
        case 7: { const char *ops[]={"RLCA","RRCA","RLA","RRA","DAA","CPL","SCF","CCF"}; text=ops[y]; break; }
        } else if(x==1) text=op==0x76?"HALT":"LD "+reg(y,!(index && z==6))+","+reg(z,!(index && y==6));
        else if(x==2) text=std::string(alu[y])+reg(z);
        else switch(z) {
        case 0: text="RET "+std::string(cc[y]); break;
        case 1: if(!q) text="POP "+pair(p,true); else { const std::string ops[]={"RET","EXX","JP ("+pair(2)+")","LD SP,"+pair(2)}; text=ops[p]; } break;
        case 2: text="JP "+std::string(cc[y])+","+hex(word(),4); break;
        case 3: if(y==0) text="JP "+hex(word(),4); else if(y==2) text="OUT ("+hex(get(),2)+"),A"; else if(y==3) text="IN A,("+hex(get(),2)+")"; else { const char *ops[]={"","","","","EX (SP),","EX DE,HL","DI","EI"}; text=ops[y]; if(y==4) text+=pair(2); } break;
        case 4: text="CALL "+std::string(cc[y])+","+hex(word(),4); break;
        case 5: text=!q?"PUSH "+pair(p,true):p==0?"CALL "+hex(word(),4):"PREFIX"; break;
        case 6: text=std::string(alu[y])+hex(get(),2); break;
        case 7: text="RST "+hex(y*8,2); break;
        }
    }
    return {address,pc,bytes,text};
}
}

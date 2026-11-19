// Fuzz x64dbg's instruction decoding layer (src/zydis_wrapper/zydis_wrapper.cpp) over Zydis: it turns
// the bytes of the debuggee / a loaded file into the instruction text, operand text and branch and
// register/flag classification the disassembly view, analysis and the expression engine use.
// Input: first byte bit 0 picks 32/64-bit mode, the rest is code disassembled linearly.
#include "zydis_wrapper.h"
#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if(size < 1)
        return -1;
    const bool disasm64 = data[0] & 1;
    data++;
    size--;

    Zydis zydis(disasm64);
    volatile size_t sink = 0;
    uint64_t addr = disasm64 ? 0x140001000ull : 0x401000ull;
    size_t offset = 0;
    while(offset < size)
    {
        if(!zydis.Disassemble(addr + offset, data + offset, size - offset) || !zydis.Success())
        {
            offset++;
            continue;
        }
        sink += zydis.InstructionText(true).size();
        sink += zydis.InstructionText(false).size();
        sink += zydis.Mnemonic().size();
        for(uint8_t i = 0; i < zydis.OpCount(); i++)
        {
            sink += zydis.OperandText(i).size();
            sink += zydis.getVectorElementType(i);
            sink += zydis.ResolveOpValue(i, [](ZydisRegister reg) { return uint64_t(reg) * 0x1000; });
            const auto & op = zydis[i];
            if(op.type == ZYDIS_OPERAND_TYPE_MEMORY)
                sink += std::string(zydis.MemSizeName(op.size / 8) ? zydis.MemSizeName(op.size / 8) : "").size();
        }
        sink += zydis.IsFilling() + zydis.IsUnusual() + zydis.IsNop() + zydis.IsRepeated() + zydis.IsPushPop();
        sink += zydis.IsBranchType(Zydis::BTAny) + zydis.IsCall() + zydis.IsRet() + zydis.IsJump();
        if(zydis.IsBranchType(Zydis::BTJmp | Zydis::BTCall | Zydis::BTLoop))
            sink += zydis.BranchDestination();
        sink += zydis.IsBranchGoingToExecute(0x246, 1) + zydis.IsConditionalGoingToExecute(0x881, 0);
        uint8_t prefix, opcode, g1, g2, g3;
        zydis.BytesGroup(&prefix, &opcode, &g1, &g2, &g3);
        sink += prefix + opcode + g1 + g2 + g3;
        uint8_t regs[ZYDIS_REGISTER_MAX_VALUE + 1];
        zydis.RegInfo(regs);
        uint8_t flags[32];
        zydis.FlagInfo(flags);
        sink += regs[0] + flags[0];
        offset += zydis.Size() ? zydis.Size() : 1;
    }
    (void)sink;
    return 0;
}

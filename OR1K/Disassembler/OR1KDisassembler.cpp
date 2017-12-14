//===- OR1KDisassembler.cpp - Disassembler for OR1K -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is part of the OR1K Disassembler.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "or1k-disassembler"
#include "OR1K.h"
#include "OR1KSubtarget.h"
#include "OR1KRegisterInfo.h"
#include "llvm/Support/MemoryObject.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCFixedLenDisassembler.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCInst.h"

using namespace llvm;

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {

class OR1KDisassembler : public MCDisassembler {
public:
  OR1KDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx) :
    MCDisassembler(STI, Ctx) {}

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &VStream,
                              raw_ostream &CStream) const override;
};

} // namespace llvm

namespace llvm {
  extern Target TheOR1KTarget;
}

static MCDisassembler *createOR1KDisassembler(const Target &T,
                                              const MCSubtargetInfo &STI,
                                              MCContext &Ctx) {
  return new OR1KDisassembler(STI, Ctx);
}

extern "C" void LLVMInitializeOR1KDisassembler() {
  // Register the disassembler
  TargetRegistry::RegisterMCDisassembler(TheOR1KTarget,
                                         createOR1KDisassembler);
}

// Forward declare because the autogenerated code will reference this.
// Definition is further down.
static DecodeStatus
DecodeGPRRegisterClass(MCInst &Inst, unsigned RegNo, uint64_t Address,
                        const void *Decoder);

static DecodeStatus
DecodeMemoryValue(MCInst &Inst, unsigned Insn, uint64_t Address,
                  const void *Decoder);

#include "OR1KGenDisassemblerTables.inc"

static DecodeStatus readInstruction32(ArrayRef<uint8_t> Bytes,
                                      uint64_t Address,
                                      uint64_t &Size,
                                      uint32_t &Insn) {
  // We want to read exactly 4 Bytes of data.
  if (Bytes.size() < 4) {
    Size = 0;
    return MCDisassembler::Fail;
  }

  // Encoded as big-endian 32-bit word in the stream.
  Insn = (Bytes[0] << 24) |
         (Bytes[1] << 16) |
         (Bytes[2] <<  8) |
         (Bytes[3] <<  0);

  return MCDisassembler::Success;
}

DecodeStatus
OR1KDisassembler::getInstruction(MCInst &Instr, uint64_t &Size,
                                 ArrayRef<uint8_t> Bytes, uint64_t Address,
                                 raw_ostream &vStream,
                                 raw_ostream &cStream) const {
  uint32_t Insn;

  DecodeStatus Result = readInstruction32(Bytes, Address, Size, Insn);

  if (Result == MCDisassembler::Fail)
    return MCDisassembler::Fail;

  // Call auto-generated decoder function
  Result = decodeInstruction(DecoderTableOR1K32, Instr, Insn, Address,
                             this, STI);
  if (Result != MCDisassembler::Fail) {
    Size = 4;
    return Result;
  }

  return MCDisassembler::Fail;
}

static DecodeStatus
DecodeGPRRegisterClass(MCInst &Inst, unsigned RegNo, uint64_t Address,
                       const void *Decoder) {

  if (RegNo > 31)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(OR1K::R0 + RegNo));
  return MCDisassembler::Success;
}

static DecodeStatus
DecodeMemoryValue(MCInst &Inst, unsigned Insn, uint64_t Address,
                  const void *Decoder) {
  unsigned Register = (Insn >> 16) & 0x1f;
  Inst.addOperand(MCOperand::createReg(Register+2));
  unsigned Offset = (Insn & 0xffff);
  Inst.addOperand(MCOperand::createImm(SignExtend32<16>(Offset)));
  return MCDisassembler::Success;
}
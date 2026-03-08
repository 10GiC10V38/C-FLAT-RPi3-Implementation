/* instrumentation/llvm-pass/CFlatPass.cpp
 *
 * C-FLAT LLVM instrumentation pass — matches the paper's domain-switch model:
 *
 *   CFG edge type         | Switch inserted            | Counts in Table 2
 *   ──────────────────────┼────────────────────────────┼──────────────────
 *   Conditional branch    | record_node before br      | Conditional branches
 *   Unconditional branch  | record_node before br      | Unconditional branches
 *   Direct function call  | call_enter  before call    | Direct calls
 *   Function return       | call_return before ret     | Returns/exits
 *
 * Loop back-edges are just branches — they are counted by record_node, not by
 * separate loop_enter/exit/iteration calls.  This matches the CFLAT paper and
 * allows the measured world-switch count to reproduce Table 2 of BLAST (CCS'23).
 */

#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

struct CFlatPass : public PassInfoMixin<CFlatPass> {

  // ── Runtime function declarations ─────────────────────────────────────────

  FunctionCallee getRecordNodeFunc(Module &M) {
    auto &Ctx = M.getContext();
    FunctionType *FT = FunctionType::get(
      Type::getVoidTy(Ctx), {Type::getInt64Ty(Ctx)}, false);
    return M.getOrInsertFunction("__cflat_record_node", FT);
  }

  FunctionCallee getCallEnterFunc(Module &M) {
    auto &Ctx = M.getContext();
    FunctionType *FT = FunctionType::get(
      Type::getVoidTy(Ctx),
      {Type::getInt64Ty(Ctx), Type::getInt64Ty(Ctx)}, false);
    return M.getOrInsertFunction("__cflat_call_enter", FT);
  }

  FunctionCallee getCallReturnFunc(Module &M) {
    auto &Ctx = M.getContext();
    FunctionType *FT = FunctionType::get(
      Type::getVoidTy(Ctx), {Type::getInt64Ty(Ctx)}, false);
    return M.getOrInsertFunction("__cflat_call_return", FT);
  }

  // Use first instruction pointer as a stable BB identifier
  uint64_t getBBID(BasicBlock &BB) {
    return (uint64_t)reinterpret_cast<uintptr_t>(&*BB.begin());
  }

  // ── Pass entry point ──────────────────────────────────────────────────────

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    // Skip declarations and our own runtime functions
    if (F.isDeclaration()) return PreservedAnalyses::all();
    if (F.getName().starts_with("__cflat_")) return PreservedAnalyses::all();
    if (F.getName().starts_with("cflat_"))   return PreservedAnalyses::all();

    Module *M = F.getParent();
    LLVMContext &Ctx = F.getContext();
    IRBuilder<> Builder(Ctx);

    int funcBranches = 0, funcCalls = 0, funcReturns = 0;

    for (auto &BB : F) {
      Instruction *Term = BB.getTerminator();
      uint64_t bbID = getBBID(BB);

      // ── (A) Branch edges ──────────────────────────────────────────────────
      // Insert record_node just BEFORE every br/switch terminator.
      // ret and unreachable terminators are handled separately below.
      if (!isa<ReturnInst>(Term) && !isa<UnreachableInst>(Term)) {
        Builder.SetInsertPoint(Term);
        Builder.CreateCall(
          getRecordNodeFunc(*M),
          {ConstantInt::get(Type::getInt64Ty(Ctx), bbID)});
        funcBranches++;
      }

      // ── (B) Return edges ──────────────────────────────────────────────────
      // Insert call_return just BEFORE every ret terminator (in the callee).
      // This is where the paper's CFLAT restores the saved hash.
      if (isa<ReturnInst>(Term)) {
        Builder.SetInsertPoint(Term);
        Builder.CreateCall(
          getCallReturnFunc(*M),
          {ConstantInt::get(Type::getInt64Ty(Ctx), bbID)});
        funcReturns++;
      }

      // ── (C) Call edges ────────────────────────────────────────────────────
      // Insert call_enter just BEFORE each direct call instruction.
      // Skip: runtime functions, intrinsics, external (declaration) calls,
      //       and indirect calls (null callee) — matches paper's evaluation.
      for (auto &I : BB) {
        CallBase *CB = dyn_cast<CallBase>(&I);
        if (!CB) continue;

        // Skip indirect calls
        Function *Callee = CB->getCalledFunction();
        if (!Callee) continue;

        // Skip our own runtime
        if (Callee->getName().starts_with("__cflat_")) continue;
        if (Callee->getName().starts_with("cflat_"))   continue;

        // Skip intrinsics and external library functions
        if (Callee->isIntrinsic() || Callee->isDeclaration()) continue;

        uint64_t callSiteID = (uint64_t)reinterpret_cast<uintptr_t>(&I);

        Builder.SetInsertPoint(&I);
        Builder.CreateCall(
          getCallEnterFunc(*M),
          {ConstantInt::get(Type::getInt64Ty(Ctx), callSiteID),
           ConstantInt::get(Type::getInt64Ty(Ctx), bbID)});
        funcCalls++;
      }
    }

    errs() << "[CFLAT] " << F.getName()
           << " | branches=" << funcBranches
           << " calls=" << funcCalls
           << " returns=" << funcReturns << "\n";

    return PreservedAnalyses::none();
  }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "CFlatPass", "v0.2",
    [](PassBuilder &PB) {
      PB.registerPipelineParsingCallback(
        [](StringRef Name, FunctionPassManager &FPM,
           ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "cflat-pass") {
            FPM.addPass(CFlatPass());
            return true;
          }
          return false;
        });
    }
  };
}

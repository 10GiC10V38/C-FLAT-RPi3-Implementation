/* instrumentation/llvm-pass/CFlatPass.cpp */
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/CFG.h"  // For predecessors()
#include "llvm/Support/raw_ostream.h"
#include <unordered_set>
#include <unordered_map>

using namespace llvm;

namespace {

struct CFlatPass : public PassInfoMixin<CFlatPass> {
  
  // Statistics
  int totalNodes = 0;
  int totalLoops = 0;
  int totalCalls = 0;
  int totalReturns = 0;
  
  // Track instrumented locations to avoid duplicates
  std::unordered_set<uint64_t> instrumentedNodes;
  std::unordered_set<BasicBlock*> instrumentedExitBlocks;
  
  // Generate unique node ID from instruction address
  uint64_t getNodeID(Instruction *I) {
    return (uint64_t)reinterpret_cast<uintptr_t>(I);
  }
  
  // Get or create runtime function declarations
  FunctionCallee getRecordNodeFunc(Module &M) {
    auto &Ctx = M.getContext();
    FunctionType *FT = FunctionType::get(
      Type::getVoidTy(Ctx),
      {Type::getInt64Ty(Ctx)},
      false
    );
    return M.getOrInsertFunction("__cflat_record_node", FT);
  }
  
  FunctionCallee getLoopEnterFunc(Module &M) {
    auto &Ctx = M.getContext();
    FunctionType *FT = FunctionType::get(
      Type::getVoidTy(Ctx),
      {Type::getInt64Ty(Ctx)},
      false
    );
    return M.getOrInsertFunction("__cflat_loop_enter", FT);
  }
  
  FunctionCallee getLoopExitFunc(Module &M) {
    auto &Ctx = M.getContext();
    FunctionType *FT = FunctionType::get(
      Type::getVoidTy(Ctx),
      {Type::getInt64Ty(Ctx)},
      false
    );
    return M.getOrInsertFunction("__cflat_loop_exit", FT);
  }
  
  
  FunctionCallee getLoopIterationFunc(Module &M) {
   auto &Ctx = M.getContext();
   FunctionType *FT = FunctionType::get(
     Type::getVoidTy(Ctx),
     {Type::getInt64Ty(Ctx)},
     false
   );
   return M.getOrInsertFunction("__cflat_loop_iteration", FT);
 }
  
  FunctionCallee getCallEnterFunc(Module &M) {
    auto &Ctx = M.getContext();
    FunctionType *FT = FunctionType::get(
      Type::getVoidTy(Ctx),
      {Type::getInt64Ty(Ctx), Type::getInt64Ty(Ctx)},
      false
    );
    return M.getOrInsertFunction("__cflat_call_enter", FT);
  }
  
  FunctionCallee getCallReturnFunc(Module &M) {
    auto &Ctx = M.getContext();
    FunctionType *FT = FunctionType::get(
      Type::getVoidTy(Ctx),
      {Type::getInt64Ty(Ctx)},
      false
    );
    return M.getOrInsertFunction("__cflat_call_return", FT);
  }
  
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    // Skip declarations and runtime functions
    if (F.isDeclaration()) return PreservedAnalyses::all();
    if (F.getName().starts_with("__cflat_")) return PreservedAnalyses::all();
    if (F.getName().starts_with("cflat_")) return PreservedAnalyses::all();
    
    Module *M = F.getParent();
    LLVMContext &Ctx = F.getContext();
    IRBuilder<> Builder(Ctx);
    
    // Get loop info
    LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);
    
    int funcNodes = 0;
    int funcLoops = 0;
    int funcCalls = 0;
    int funcReturns = 0;
    
    // Track loop headers (including nested subloops)
    std::unordered_set<BasicBlock*> loopHeaders;
    SmallVector<Loop*, 8> worklist(LI.begin(), LI.end());
    while (!worklist.empty()) {
      Loop *L = worklist.pop_back_val();
      loopHeaders.insert(L->getHeader());
      worklist.append(L->getSubLoops().begin(), L->getSubLoops().end());
    }
    
    // Clear per-function tracking
    instrumentedExitBlocks.clear();
    
    errs() << "[CFLAT] Processing function: " << F.getName() << "\n";
    
    // PASS 1: Instrument loop exit blocks
    // We do this first to avoid instrumenting them as regular nodes
    for (auto &BB : F) {
      Loop *CurrentLoop = LI.getLoopFor(&BB);
      
      if (CurrentLoop) {
        BasicBlock *LoopHeader = CurrentLoop->getHeader();
        Instruction *Term = BB.getTerminator();
        
        // Check all successors
        for (unsigned i = 0, e = Term->getNumSuccessors(); i != e; ++i) {
          BasicBlock *Succ = Term->getSuccessor(i);
          Loop *SuccLoop = LI.getLoopFor(Succ);
          
          // If successor is truly outside the current loop (not in a subloop)
          if (!CurrentLoop->contains(Succ)) {
            // Only instrument each exit block once
            if (instrumentedExitBlocks.find(Succ) == instrumentedExitBlocks.end()) {
              IRBuilder<> ExitBuilder(&*Succ->getFirstInsertionPt());
              uint64_t headerID = getNodeID(&*LoopHeader->begin());
              ExitBuilder.CreateCall(
                getLoopExitFunc(*M),
                {ConstantInt::get(Type::getInt64Ty(Ctx), headerID)}
              );
              instrumentedExitBlocks.insert(Succ);
              errs() << "  [LOOP_EXIT] at exit block for loop 0x" 
                     << format("%lx", headerID) << "\n";
            }
          }
        }
      }
    }
    
    // PASS 2: Instrument basic blocks
    for (auto &BB : F) {
      bool isLoopHeader = loopHeaders.count(&BB) > 0;
      bool isExitBlock = instrumentedExitBlocks.count(&BB) > 0;
      
      // 1. Instrument block entry
      Builder.SetInsertPoint(&*BB.getFirstInsertionPt());
      uint64_t bbID = getNodeID(&*BB.begin());
      
if (isLoopHeader) {
  Loop *CurrentLoop = LI.getLoopFor(&BB);
  if (CurrentLoop && CurrentLoop->getHeader() == &BB) {
    
    // Process all predecessors
    for (BasicBlock *Pred : predecessors(&BB)) {
      Loop *PredLoop = LI.getLoopFor(Pred);
      
      if (PredLoop != CurrentLoop) {
        // Predecessor OUTSIDE loop - insert loop_enter
        IRBuilder<> PredBuilder(Pred->getTerminator());
        PredBuilder.CreateCall(
          getLoopEnterFunc(*M),
          {ConstantInt::get(Type::getInt64Ty(Ctx), bbID)}
        );
        errs() << "  [LOOP_ENTER] from outside into loop 0x" << format("%lx", bbID) << "\n";
      } else if (PredLoop == CurrentLoop && CurrentLoop->contains(Pred)) {
        // Predecessor INSIDE same loop AND loop contains it - this is a back-edge
        // Additional check: make sure Pred actually branches to this header
        Instruction *PredTerm = Pred->getTerminator();
        bool branchesToHeader = false;
        
        for (unsigned i = 0; i < PredTerm->getNumSuccessors(); ++i) {
          if (PredTerm->getSuccessor(i) == &BB) {
            branchesToHeader = true;
            break;
          }
        }
        
        if (branchesToHeader) {
          IRBuilder<> BackEdgeBuilder(Pred->getTerminator());
          BackEdgeBuilder.CreateCall(
            getLoopIterationFunc(*M),
            {ConstantInt::get(Type::getInt64Ty(Ctx), bbID)}
          );
          errs() << "  [LOOP_ITERATION] on back-edge from 0x" 
                 << format("%lx", getNodeID(&*Pred->begin())) << "\n";
        }
      }
    }
    
    funcLoops++;
    totalLoops++;
  }
  
  // Still instrument the loop header as regular node
  if (instrumentedNodes.find(bbID) == instrumentedNodes.end()) {
    Builder.CreateCall(
      getRecordNodeFunc(*M),
      {ConstantInt::get(Type::getInt64Ty(Ctx), bbID)}
    );
    instrumentedNodes.insert(bbID);
    funcNodes++;
    totalNodes++;
  }
}
  

 else{
        
       Instruction *InsertPoint = &*BB.getFirstInsertionPt();
        
         
        if (isExitBlock) {
      // The first instruction should be the loop_exit call
      // Insert record_node after it
      if (CallInst *CI = dyn_cast<CallInst>(InsertPoint)) {
        if (Function *F = CI->getCalledFunction()) {
          if (F->getName() == "__cflat_loop_exit") {
            InsertPoint = CI->getNextNode();
          }
        }
      }
    }
    
    if (instrumentedNodes.find(bbID) == instrumentedNodes.end()) {
      Builder.SetInsertPoint(InsertPoint);
      Builder.CreateCall(
        getRecordNodeFunc(*M),
        {ConstantInt::get(Type::getInt64Ty(Ctx), bbID)}
      );
      instrumentedNodes.insert(bbID);
      funcNodes++;
      totalNodes++;
    }
  }
      
      // 2. Instrument calls
      for (auto &I : BB) {
        if (CallBase *CB = dyn_cast<CallBase>(&I)) {
          Function *Callee = CB->getCalledFunction();
          
          // Skip runtime functions
          if (Callee && Callee->getName().starts_with("__cflat_")) continue;
          if (Callee && Callee->getName().starts_with("cflat_")) continue;
          
          // Skip intrinsics and declarations
          if (Callee && (Callee->isIntrinsic() || Callee->isDeclaration())) continue;
          
          uint64_t callSiteID = getNodeID(&I);
          uint64_t callerID = getNodeID(&*BB.begin());
          
          // Insert call_enter before the call
          Builder.SetInsertPoint(&I);
          Builder.CreateCall(
            getCallEnterFunc(*M),
            {
              ConstantInt::get(Type::getInt64Ty(Ctx), callSiteID),
              ConstantInt::get(Type::getInt64Ty(Ctx), callerID)
            }
          );
          
          // Insert call_return after the call
          Builder.SetInsertPoint(I.getNextNode());
          Builder.CreateCall(
            getCallReturnFunc(*M),
            {ConstantInt::get(Type::getInt64Ty(Ctx), callSiteID)}
          );
          
          funcCalls++;
          totalCalls++;
          errs() << "  [CALL] 0x" << format("%lx", callSiteID);
          if (Callee) errs() << " -> " << Callee->getName();
          errs() << "\n";
        }
      }
      
      // 3. Count returns
      Instruction *Term = BB.getTerminator();
      if (ReturnInst *RI = dyn_cast<ReturnInst>(Term)) {
        funcReturns++;
        totalReturns++;
      }
    }
    
    errs() << "[CFLAT] Function: " << F.getName() 
           << " | Nodes: " << funcNodes 
           << " | Loops: " << funcLoops
           << " | Calls: " << funcCalls
           << " | Returns: " << funcReturns << "\n";
    
    return PreservedAnalyses::none();
  }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "CFlatPass", "v0.1",
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

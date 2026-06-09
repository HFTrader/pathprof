// PathProfPass.cpp - front-end 4: a Clang/LLVM pass plugin (new pass manager).
//
// Finds functions annotated [[clang::annotate("pathprof")]] and injects:
//   - __pathprof_enter(ptrtoint(F))   at the function entry
//   - __pathprof_leave()              before every return
// The function's own address is the gate key (resolved by dladdr at runtime).
//
// Build:  clang++ -fPIC -shared $(llvm-config --cxxflags) PathProfPass.cpp -o PathProfPass.so
// Use:    clang++ -O2 -fpass-plugin=./PathProfPass.so app.cpp -rdynamic -ldl
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Constants.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include <set>
using namespace llvm;

namespace {

// Read llvm.global.annotations and collect functions tagged with `want`.
std::set<Function*> annotatedFns(Module &M, StringRef want) {
    std::set<Function*> out;
    GlobalVariable *GA = M.getGlobalVariable("llvm.global.annotations");
    if (!GA || !GA->hasInitializer()) return out;
    auto *arr = dyn_cast<ConstantArray>(GA->getInitializer());
    if (!arr) return out;
    for (Use &U : arr->operands()) {
        auto *cs = dyn_cast<ConstantStruct>(U.get());
        if (!cs || cs->getNumOperands() < 2) continue;
        auto *F = dyn_cast<Function>(cs->getOperand(0)->stripPointerCasts());
        auto *gv = dyn_cast<GlobalVariable>(cs->getOperand(1)->stripPointerCasts());
        if (!F || !gv || !gv->hasInitializer()) continue;
        if (auto *str = dyn_cast<ConstantDataArray>(gv->getInitializer()))
            if (str->isCString() && str->getAsCString() == want)
                out.insert(F);
    }
    return out;
}

struct PathProfPass : PassInfoMixin<PathProfPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
        auto fns = annotatedFns(M, "pathprof");
        if (fns.empty()) return PreservedAnalyses::all();

        LLVMContext &C = M.getContext();
        Type *I64 = Type::getInt64Ty(C), *VoidTy = Type::getVoidTy(C);
        FunctionCallee enterFn = M.getOrInsertFunction(
            "__pathprof_enter", FunctionType::get(VoidTy, {I64}, false));
        FunctionCallee leaveFn = M.getOrInsertFunction(
            "__pathprof_leave", FunctionType::get(VoidTy, {}, false));

        for (Function *F : fns) {
            if (F->isDeclaration()) continue;
            IRBuilder<> B(&*F->getEntryBlock().getFirstInsertionPt());
            B.CreateCall(enterFn, {B.CreatePtrToInt(F, I64)});
            for (BasicBlock &BB : *F)
                if (auto *ret = dyn_cast<ReturnInst>(BB.getTerminator())) {
                    IRBuilder<> RB(ret);
                    RB.CreateCall(leaveFn, {});
                }
        }
        return PreservedAnalyses::none();
    }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "PathProf", LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            // Run after the optimizer so structure is final; annotated fns are
            // noinline so they survive as real call boundaries.
            PB.registerOptimizerLastEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel) {
                    MPM.addPass(PathProfPass());
                });
            PB.registerPipelineParsingCallback(
                [](StringRef N, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                    if (N == "pathprof") { MPM.addPass(PathProfPass()); return true; }
                    return false;
                });
        }};
}

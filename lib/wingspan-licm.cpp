#include "wingspan-licm.h"
#include "wingspan-constants.h"

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"

#define PRINT_INFO false

llvm::MapVector<llvm::Value*, bool> loopInvariantValuesCache;
llvm::MapVector<llvm::Function*, bool> sideEffectFunctionsCache;
llvm::Loop* loop;

namespace invariance {
	bool instructionCanBeInvariant(llvm::Instruction* instr) {
		switch (instr->getOpcode()) {
		case llvm::Instruction::FNeg:
		case llvm::Instruction::Add:
		case llvm::Instruction::FAdd:
		case llvm::Instruction::Sub:
		case llvm::Instruction::FSub:
		case llvm::Instruction::Mul:
		case llvm::Instruction::FMul:
		case llvm::Instruction::UDiv:
		case llvm::Instruction::SDiv:
		case llvm::Instruction::FDiv:
		case llvm::Instruction::URem:
		case llvm::Instruction::SRem:
		case llvm::Instruction::FRem:
		case llvm::Instruction::Shl:
		case llvm::Instruction::LShr:
		case llvm::Instruction::AShr:
		case llvm::Instruction::And:
		case llvm::Instruction::Or:
		case llvm::Instruction::Xor:
		case llvm::Instruction::ICmp:
		case llvm::Instruction::FCmp:
		case llvm::Instruction::PHI:
		case llvm::Instruction::Call:
			return true;
		default:
			return false;
		}
	}

	bool instructionIsOutsideOfLoop(llvm::Instruction* instr) {
		for (auto b : loop->blocks()) {
			if (instr->getParent() == b)
				return false;
		}

		return true;
	}

	bool isLoopInvariantRecursive(llvm::Value* v, llvm::Value* originalSearchPoint, int depth) {
		/*
			If the value was already explored and has a cached result, we simply return that.
			Note that count is O(1), so this actually does save time. If it was O(n), we couldn't
			be sure!
		*/
		if (loopInvariantValuesCache.count(v)) {
			return loopInvariantValuesCache[v];
		}

		/*
			This function gets recursivelly called in order to analyse an instructions operands,
			In order to avoid infinite (or huge) recursion stacks, we set an "assumed as 
			non-invariant" check.
		*/
		if (depth > ws::constants::LOOP_INVARIANT_RECURSION_MAX_DEPTH) {
			loopInvariantValuesCache[v] = false;
			return false;
		}

		/*
			If an instruction uses itself (directly or recursively), then it's taking into account
			the value it had in the last iteration. Therefore, it is NOT invariant.
		*/
		if (v == originalSearchPoint && depth > 0) {
			loopInvariantValuesCache[v] = false;
			return false;
		}
		
		/*
			According to LLVM, all non-instruction values are automatically considered loop invariant.
			This makes sense: say, a block, won't change while the loop executes, since it won't change
			at all!
		*/
		if (!llvm::isa<llvm::Instruction>(v)) {
			loopInvariantValuesCache[v] = true;
			return true;
		}

		/*
			By now, we're sure v is an instruction, so we can safely cast it as such.
		*/
		auto instr = llvm::dyn_cast<llvm::Instruction>(v);

		/*
			In our impl. of licm, only SOME instructions are considered safe to hoist.
			More complex implementations may avoid this check, in order to favor generality.
		*/
		if (!instructionCanBeInvariant(instr)) {
			loopInvariantValuesCache[v] = false;
			return false;
		}

		/*
			If the instr. we found is outside the current loop, it is invariant. Doing this check
			allows us to save time, and also consider cases where an instruction is invariant for an
			inner loop, but not for an outer one.
		*/
		if (instructionIsOutsideOfLoop(instr)) {
			loopInvariantValuesCache[v] = true;
			return true;
		}

		/*
			Finally, an instr. is only invariant if all of its operands are invariant too.
		*/
		auto numOperands = instr->getNumOperands();
		bool invariant = true;
		int i = 0;

		while (i < numOperands && invariant) {
			invariant = isLoopInvariantRecursive(instr->getOperand(i), originalSearchPoint, depth + 1);
			i++;
		}

		loopInvariantValuesCache[v] = invariant;
		return invariant;
	}

	bool isLoopInvariant(llvm::Value* v) {
		return isLoopInvariantRecursive(v, v, 0);
	}

	bool isHoisteablePhi(llvm::Instruction* instr) {
		/*
			Since we're not modifying the CFG, some loop-invariant PHI nodes can't be hoisted.
			Particularly, those that areren't in the preheader. Such PHI nodes will have as 
			incoming edges blocks that are inside the loop, and thus won't be able to be hoisted
			without causing issues.

			However, we DO want to recognize them as invariant, since their value does NOT change during
			iteration. So, we add this particular check later.
		*/
		if (instr->getOpcode() == llvm::Instruction::PHI && instr->getParent() != loop->getHeader()) {
			return false;
		}

		return true;
	}
}

namespace dominance {
	bool dominatesEveryExitingBlock(llvm::BasicBlock* block, llvm::DominatorTree& DT) {
		llvm::SmallVector<llvm::BasicBlock*> exitingBlocks;
		loop->getExitingBlocks(exitingBlocks);

		for (auto* exitingBlock : exitingBlocks) {
			if (DT.dominates(block, exitingBlock))
				return false; 
		}
		
		return true;
	}
}

namespace safety {
	bool functionMayHaveSideEffects(llvm::Function* f, int callStackDepth);

	bool isMemoryOperation(llvm::Instruction* instr) {
		switch (instr->getOpcode()) {
		case llvm::Instruction::Alloca:
		case llvm::Instruction::Load:
		case llvm::Instruction::Store:
		case llvm::Instruction::Fence:
		case llvm::Instruction::AtomicCmpXchg:
		case llvm::Instruction::AtomicRMW:
		case llvm::Instruction::GetElementPtr:
			return true;
		default:
			return false;
		}
	}
	
	bool instrMayHaveSideEffects(llvm::Instruction* instr, int callStackDepth) {
		if (isMemoryOperation(instr))
			return true;

		if (auto call = llvm::dyn_cast<llvm::CallInst>(instr)) {
			auto f = call->getCalledFunction();
			return functionMayHaveSideEffects(f, callStackDepth + 1);
		}

		return false;
	}

	/*
		According to D.A. SPULER and A.S.M SAJEEV, to be certain that a function call produces no side effects,
		the following conditions are sufficient (but not necessary):
			(1) The function does not perform any I/O
			(2) No global variables are modified
			(3) No local permanent variables are modified
			(4) No pass-by-reference parameters are modified
			(5) No modification is made to nonlocal/static variables via pointers
			(6) Any function called also satisfies these conditions

		Regarding 1) To perform I/O, we'd need to call a non-user defined function
		(from the C standard lib), which we know will be tagged with optnone.

		Regarding 2-5) all of those are performed via the memory access instructions, so
		we can easily check for those opcodes and return false if found.

		Regarding 6) We can recursively check, much like we do to determine invariance.
	*/
	bool functionMayHaveSideEffects(llvm::Function* f, int callStackDepth) {
		/*
			Special case: Sometimes, call->getCalledFunction() will return null.In those
			cases, we assume that the function being called has side effects, in order to be
			cautious, but we don't cache it.
		*/
		if (!f) {
			return true;
		}


		// Cached result
		if (sideEffectFunctionsCache.count(f)) {
			return sideEffectFunctionsCache[f];
		}

		// Recursion depth exceeded, we assume true (though we're not sure).
		if(callStackDepth > ws::constants::LOOP_INVARIANT_RECURSION_MAX_DEPTH) {
			sideEffectFunctionsCache[f] = true;
			return true;
		}

		// If the function has optnone, we cautiously assume it may have side-effects
		if (f->hasOptNone()) {
			sideEffectFunctionsCache[f] = true;
			return true;
		}

		// We iterate over all instructions and determine if they're side effect-free
		for (auto &b : *f) {
			for (auto& instr : b) {
				if (instrMayHaveSideEffects(&instr, callStackDepth)) {
					sideEffectFunctionsCache[f] = true;
					return true;
				}
			}
		}

		sideEffectFunctionsCache[f] = false;
		return false;
	}

	bool isSafeToSpeculate(llvm::Instruction* instr) {
		/*
			We only really allow one instr. type that could cause sideeefects, the CALL instr. 
			Such side effects would come from the function being called, not the CALL itself.
		*/
		if (auto call = llvm::dyn_cast<llvm::CallInst>(instr)) {
			auto f = call->getCalledFunction();
			return !functionMayHaveSideEffects(f, 0);
		}

		return true;
	}
}

namespace hoisting {
	/*
		Header PHIs are not quite hoisted, so much as replaced by it's
		invariant value. A header PHI will look like either of the following:

		%X = PHI [preheader, initial value], [latch, in-loop value]
		%X = PHI [latch, in-loop value], [preheader, initial value]

		Since we know it's invariant (i.e. in-loop value can be calculated
		outside of the loop, and will not change during the iteration process),
		we can simply replace all its uses with the in-loop value.
	*/
	void hoistPhi(llvm::PHINode* phi) {
		// Let phi be of shape %X = phi [A, B], [C, D]
		auto a = phi->getIncomingValue(0);
		auto b = phi->getIncomingBlock(0);
		auto c = phi->getIncomingValue(1);
		auto d = phi->getIncomingBlock(1);

		if (b == loop->getLoopLatch()) {
			phi->replaceAllUsesWith(a);
			phi->eraseFromParent();
		}
		else {
			phi->replaceAllUsesWith(c);
			phi->eraseFromParent();
		}
	}

	void hoist(llvm::Instruction* instr, llvm::BasicBlock* destination) {
		if (auto phi = llvm::dyn_cast<llvm::PHINode>(instr)) {
			hoistPhi(phi);
			return;
		}

		instr->moveBefore(destination->getTerminator());
	}
}

llvm::PreservedAnalyses ws::LoopInvariantCodeMover::run(
	llvm::Loop& L,
	llvm::LoopAnalysisManager& LAM,
	llvm::LoopStandardAnalysisResults& AR,
	llvm::LPMUpdater& U
) {
	//Being able to access the loop anywhere is crucial, so we set it as a global variable.
	loop = &L;

	auto function = loop->getHeader()->getParent();
	llvm::DominatorTree DT(*function);
	llvm::SmallVector<llvm::Instruction*> instructionsToBeMoved;
	
	for (auto* block : L.blocks()) {
		/*
			LLVM's Loop Pass Manager automatically schedules loop-simplify before any user-written loop pass,
			so we can be 100% sure that the loop has only one latch, and that getLoopLatch() won't return null.
			Moreover, we want to ignore anything in the latch, as even if an instruction might seem invariant,
			it would not be safe to move it, since the latch controls the backwards edge in the cfg.
		*/
		if (block == L.getLoopLatch())
			continue;

		/*
			Any instructions inside a block that doesn't dominate all exiting blocks can't be invariant. We can't 
			guarantee that instructions inside a basic block that doesn't dominate all exiting blocks are safe to 
			hoist, so we simply ignore such blocks.
		*/
		if (!dominance::dominatesEveryExitingBlock(block, DT))
			continue;

		for (auto& instr : *block) {
			if (!invariance::isLoopInvariant(&instr))
				continue;

			if (!invariance::isHoisteablePhi(&instr))
				continue;

			if (!safety::isSafeToSpeculate(&instr))
				continue;

			instructionsToBeMoved.push_back(&instr);
		}
	}

	if (PRINT_INFO) {
		llvm::errs() << "The following instructions were detected as loop invariant and safely hoisteable:\n";

		for (auto instr : instructionsToBeMoved) {
			llvm::errs() << "\t" << *instr << "\n";
		}
	}

	auto preheader = loop->getHeader()->getPrevNode();

	for (auto instr : instructionsToBeMoved) {
		hoisting::hoist(instr, preheader);
	}

	/* 
		Global variables live through multiple passes. If a module has more than one loop, 
		the second one "carry over" data from the first, which is a no-go.
	*/
	loopInvariantValuesCache.clear();
	sideEffectFunctionsCache.clear();
	loop = nullptr;
	
	return llvm::PreservedAnalyses::all();
}

bool ws::LoopInvariantCodeMover::registerPipelinePass(llvm::StringRef name, llvm::LoopPassManager& lpm, llvm::ArrayRef<llvm::PassBuilder::PipelineElement> /*ignored*/) {
	if (name.consume_front("wingspan-licm")) {
		lpm.addPass(ws::LoopInvariantCodeMover{});
		return true;
	}

	return false;
}
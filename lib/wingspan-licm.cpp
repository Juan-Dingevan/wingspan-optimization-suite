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

llvm::MapVector<llvm::Instruction*, bool> markedAsInvariant;

namespace invariance {
	bool blockIsOutsideOfLoop(llvm::BasicBlock* block, llvm::Loop* l) {
		for (auto b : l->blocks())
			if (b == block)
				return false;

		return true;
	}
	
	bool isOutsideOfLoop(llvm::Instruction* instr, llvm::Loop* l) {
		return blockIsOutsideOfLoop(instr->getParent(), l);
	}

	bool isTriviallyInvariant(llvm::Value* v) {
		return !(llvm::isa<llvm::Instruction>(v) || llvm::isa<llvm::BasicBlock>(v));
	}

	/*
		When is a Value [loop] invariant?
			- All non-instruction Values are invariant 
			(source: https://llvm.org/doxygen/LoopInfo_8cpp_source.html, line 64)
		
		When is an instruction invariant? Generally, 
		it holds that it'll be invariant...:
			- When it's outside of the loop and/or
			- When all of its operands are invariant.

		A noteworthy or special case might be PHINodes, which we can determine are
		invariant iff neither of its incoming edges comes from a basic block that is
		inside of the loop.

		Another noteworthy case is calling a function. It holds the same logic as a
		normal instruction, but requires slightly different code (i think). Basically,
		a call instr. is invariant iff all of its actual parameters are invariant.
	*/
	bool isLoopInvariantRecursive(
		llvm::Value* v, 
		llvm::Value* originalSearchPoint, 
		llvm::Loop* loop, 
		int depth
	) {
		//llvm::errs() << "\t" << "isLoopInvariantRecursive(" << *v << ", " << *originalSearchPoint << ", loop, " << depth << ")\n";
		// Max depth exceeded. Assume false.
		if (depth >= ws::constants::LOOP_INVARIANT_RECURSION_MAX_DEPTH)
			return false;

		//llvm::errs() << "\t" << "Max depth wasn't exceeded.\n";

		/*
			If, within a loop, an instruction uses itself (recursively)
			then we KNOW it's not invariant, for it's using the value
			that it had on a previous iteration.
		*/
		if (v == originalSearchPoint && depth != 0)
			return false;

		//llvm::errs() << "\t" << "We're not dealing with a cycle.\n";

		/*	Any non-instruction value IS loop invariant, for LLVM's LICM pass.
			(source: https://llvm.org/doxygen/LoopInfo_8cpp_source.html, line 64)
			However, in our implementation, things are a bit more murky. We can
			catch quite a few base cases here, though.
		*/
		if (isTriviallyInvariant(v))
			return true;

		//llvm::errs() << "\t" << "v is not trivially invariant.\n";

		/*
			We can consider a basic block to be invariant if it's outside
			the loop. This covers cases such as branches or phi nodes, both
			of which consider basic blocks as some of their operands.
		*/
		if (llvm::isa<llvm::BasicBlock>(v)) {
			auto block = llvm::dyn_cast<llvm::BasicBlock>(v);
			//auto header = loop->getHeader();
			auto latch = loop->getLoopLatch();
			
			return block != latch;
		}

		//llvm::errs() << "\t" << "v is an instruction.\n";

		llvm::Instruction* instr = llvm::dyn_cast<llvm::Instruction>(v);

		/*
			For speed's sake, we keep register of all the instructions we've
			marked as invariant for a given loop. Thus, if two instructions
			use the same operand, we don't run through the entire verification
			process twice.
		*/
		if (markedAsInvariant[instr])
			return true;

		//llvm::errs() << "\t" << "v wasn't marked as invariant before.\n";

		/*
			Consider the following case:

				a <- f()
				b <- g()
				sum <- 0

				for(i <- 0, i < K, i <- i + 1):
					t <- a + b
					sum <- sum + A[t]

			It's clear that t is invariant, because both of it's operands
			are outside the loop. Thus, when we check it, we'll recursively
			call isLoopInvariant on a and b. This case covers such situations.
		*/
		if (isOutsideOfLoop(instr, loop)) {
			markedAsInvariant[instr] = true;
			return true;
		}

		//llvm::errs() << "\t" << "v is inside the loop.\n";

		/*
			The recursive case: An instruction will only be invariant if all
			of its operands are also invariant.
		*/

		auto numOperands = instr->getNumOperands();
		bool invariant = true;
		int i = 0;

		while (i < numOperands && invariant) {
			invariant = isLoopInvariantRecursive(instr->getOperand(i), originalSearchPoint, loop, depth + 1);
			i++;
		}

		//llvm::errs() << "\t" << "We got out of the recursive call.\n";

		if (invariant)
			markedAsInvariant[instr] = true;

		return invariant;
	}

	bool isLoopInvariant(llvm::Value* v, llvm::Loop* l) {
		return isLoopInvariantRecursive(v, v, l, 0);
	}
	
	bool dominatesAllExits(llvm::Instruction* instr) {
		return false;
	}

	bool hasSideEffects(llvm::Instruction* instr) {
		return true;
	}
}

namespace hoisting {
	template <typename T>
	bool isIn(T element, const llvm::SmallVector<T>& elements) {
		for (const auto& e : elements) {
			if (e == element) {
				return true;
			}
		}
		return false;
	}

	bool allInstructionsOfBlockMustBeMoved(llvm::BasicBlock* block, llvm::SmallVector<llvm::Instruction*> toBeMoved) {
		for (auto& instr : *block) {
			if (!isIn<llvm::Instruction*>(&instr, toBeMoved))
				return false;
		}

		return true;
	}

	llvm::SmallVector<llvm::Instruction*> instructionsToHoist(llvm::SmallVector<llvm::Instruction*> all, llvm::SmallVector<llvm::Instruction*> movedByBlocks) {
		llvm::SmallVector<llvm::Instruction*> instructionsToHoist;

		for (auto instr : all)
			if (!isIn(instr, movedByBlocks))
				instructionsToHoist.push_back(instr);

		return instructionsToHoist;
	}

	void changeBranch(llvm::BasicBlock* from, llvm::BasicBlock* to) {
		llvm::Instruction* terminator = from->getTerminator();

		if (llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(terminator))
			if (branchInst->isUnconditional())
				branchInst->setSuccessor(0, to);
	}

	void hoistBasicBlock(
		llvm::BasicBlock* blockToHoist,
		llvm::BasicBlock* preheader,
		llvm::SmallVector<llvm::BasicBlock*> allBlocksMoved
	) {
		blockToHoist->moveBefore(preheader);

		auto terminator = blockToHoist->getTerminator();
		if (llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(terminator)) {
			if (branchInst->isUnconditional()) {
				auto successor = branchInst->getSuccessor(0);
				if (!isIn<llvm::BasicBlock*>(successor, allBlocksMoved)) {
					changeBranch(blockToHoist, preheader);
				}
			}
		}
	}

	void hoistInstruction(llvm::Instruction* instr, llvm::BasicBlock* preheader, llvm::Loop* loop) {
		if (llvm::isa<llvm::PHINode>(instr)) {
			// let phi be of shape %X = phi [A, B], [C, D]
			// the compiler never generates phi nodes with more
			// than 2 incoming edges.
			auto phi = llvm::dyn_cast<llvm::PHINode>(instr);
			
			auto a = phi->getIncomingValue(0);
			auto b = phi->getIncomingBlock(0);
			auto c = phi->getIncomingValue(1);
			auto d = phi->getIncomingBlock(1);
			
			// In theory, only invariant PHIs that'll need changing will be those of the
			// header of the loop.
			auto needsChanging = b == loop->getLoopLatch() || d == loop->getLoopLatch();

			if (needsChanging) {
				if (b == loop->getLoopLatch()) {
					instr->replaceAllUsesWith(a);
					instr->eraseFromParent();
				}
				else {
					instr->replaceAllUsesWith(c);
					instr->eraseFromParent();
				}

				return;
			}
		}
		instr->moveBefore(preheader->getTerminator());
	}

}

llvm::PreservedAnalyses ws::LoopInvariantCodeMover::run(
	llvm::Loop& L,
	llvm::LoopAnalysisManager& LAM,
	llvm::LoopStandardAnalysisResults& AR,
	llvm::LPMUpdater& U
) {

	//temp
	llvm::errs() << "Running on loop with preheader: " << "\n";

	for (auto& instr : *L.getHeader()->getPrevNode())
		llvm::errs() << "\t" << instr << "\n";

	llvm::errs() << "\n\n";
	//end temp

	auto preheader = L.getHeader()->getPrevNode();
	llvm::SmallVector<llvm::Instruction*> instructionsToBeMoved;

	for (auto *block : L.blocks()) {
		/*if (block == L.getHeader())
			continue;*/

		/*
			LLVM's Loop Pass Manager automatically schedules
			loop-simplify before any user-written loop pass,
			so we can be 100% sure that the loop has only one
			latch, and that getLoopLatch() won't return null.
		*/
		if (block == L.getLoopLatch())
			continue;

		for (auto &instr : *block) {
			if (!invariance::isLoopInvariant(&instr, &L))
				continue;

			/*if (!dominatesAllExits(&instr))
				continue;

			if (hasSideEffects(&instr))
				continue;*/

			instructionsToBeMoved.push_back(&instr);
		}
	}
	llvm::errs() << "\n\n";
	
	llvm::SmallVector<llvm::BasicBlock*> blocksToBeMoved;
	llvm::SmallVector<llvm::Instruction*> instructionsMovedByBlocks;

	for (auto* block : L.blocks()) {
		if (hoisting::allInstructionsOfBlockMustBeMoved(block, instructionsToBeMoved)) {
			blocksToBeMoved.push_back(block);
			
			for (auto& instr : *block)
				instructionsMovedByBlocks.push_back(&instr);
		}
	}

	llvm::errs() << "INSTRUCTIONS:\n";
	for (auto instr : instructionsToBeMoved) {
		llvm::errs() << *instr << " is a loop invariant instruction, and will be moved.\n";
	}
	llvm::errs() << "\n";
	llvm::errs() << "BLOCKS:\n";
	for (auto block : blocksToBeMoved) {
		llvm::errs() << *block << " is a loop invariant block, and wil be moved.\n";
	}

	
	
	// Now, we hoist all instructions that weren't hoisted with the blocks.
	// First, we get them.
	auto instructionsToHoist = hoisting::instructionsToHoist(instructionsToBeMoved, instructionsMovedByBlocks);
	// And then we hoist them
	for (auto instr : instructionsToHoist)
		hoisting::hoistInstruction(instr, preheader, &L);
	
	// After that, if there are any whole basic blocks that must be hoisted...
	if (blocksToBeMoved.size() > 0) {
		//... we do so
		for (auto block : blocksToBeMoved) {
			hoisting::hoistBasicBlock(block, preheader, blocksToBeMoved);
		}

		// If the original preheader was the entry block to the function, we
		// must update it so the last hoisted block is the new entry block (i.e.
		// has no predecesors).
		if (preheader->isEntryBlock()) {
			auto newEntry = blocksToBeMoved.back();
			for (auto pred = pred_begin(newEntry), et = pred_end(newEntry); pred != et; ++pred) {
				newEntry->removePredecessor(*pred);
			}
			
		}

		// In addition, if we moved the first (few) block(s) of the loop, we must
		// change the header jump to jump to the 'new first' block. First, we find
		// the new first
		llvm::BasicBlock* newFirst;
		for (auto block : L.getBlocks()) {
			if (block == L.getHeader())
				continue;

			if (!hoisting::isIn<llvm::BasicBlock*>(block, blocksToBeMoved)) {
				newFirst = block;
				break;
			}
		}

		auto header = L.getHeader();
		auto headerTerminator = header->getTerminator();

		if (llvm::isa<llvm::BranchInst>(headerTerminator)) {
			auto headerBranch = llvm::dyn_cast<llvm::BranchInst>(headerTerminator);
			if (!headerBranch->isUnconditional()) {
				for (int i = 0; i < headerBranch->getNumOperands(); i++) {
					auto op = headerBranch->getOperand(i);
					if (llvm::isa<llvm::BasicBlock>(op)) {
						auto opAsBB = llvm::dyn_cast<llvm::BasicBlock>(op);
						if (hoisting::isIn<llvm::BasicBlock*>(opAsBB, blocksToBeMoved))
							headerBranch->setOperand(i, newFirst);
					}
				}
			}
		}
	}
	llvm::errs() << "\n\n";

	auto F = preheader->getParent();
	
	llvm::errs() << *F;

	llvm::errs() << "\n";

	llvm::errs() << "The entry block for F is " << F->getEntryBlock() << "And it's predecessors are:\n";
	
	llvm::BasicBlock* B = &(F->getEntryBlock());
	for (auto it = pred_begin(B), et = pred_end(B); it != et; ++it)
	{
		llvm::BasicBlock* predecessor = *it;
		llvm::errs() << *predecessor;
	}

	llvm::errs() << "\n\n";
	
	return llvm::PreservedAnalyses::all();
}

bool ws::LoopInvariantCodeMover::registerPipelinePass(llvm::StringRef name, llvm::LoopPassManager& lpm, llvm::ArrayRef<llvm::PassBuilder::PipelineElement> /*ignored*/) {
	if (name.consume_front("wingspan-licm")) {
		lpm.addPass(ws::LoopInvariantCodeMover{});
		return true;
	}

	return false;
}
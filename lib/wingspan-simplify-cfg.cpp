#include "wingspan-simplify-cfg.h"
#include "wingspan-constants.h"
#include "plugin-registration.h"

namespace aux {
	std::pair<llvm::BasicBlock*, llvm::BasicBlock*> getConditionalSuccessors(llvm::BasicBlock* block) {
		auto branchInst = llvm::cast<llvm::BranchInst>(block->getTerminator());
		return { branchInst->getSuccessor(0), branchInst->getSuccessor(1) };
	}
}

namespace detection {
	bool hasUnconditionalBranch(llvm::BasicBlock* bb) {
		llvm::Instruction* terminator = bb->getTerminator();
		if (llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(terminator))
			if (branchInst->isUnconditional())
				return true;

		return false;
	}

	bool hasConditionalBranch(llvm::BasicBlock* bb) {
		llvm::Instruction* terminator = bb->getTerminator();
		if (llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(terminator))
			if (!branchInst->isUnconditional())
				return true;

		return false;
	}

	bool blockCanBeSteppedOver(llvm::BasicBlock* block) {
		auto terminator = block->getTerminator();

		if (&(block->front()) != terminator)
			return false; // Has at least 2 instructions

		if (!llvm::isa<llvm::BranchInst>(terminator))
			return false;

		auto branch = llvm::dyn_cast<llvm::BranchInst>(terminator);

		return branch->isUnconditional();
	}
}

namespace simplification {
	void changeBranch(llvm::BasicBlock* from, llvm::BasicBlock* to, llvm::BasicBlock* old = nullptr) {
		llvm::Instruction* terminator = from->getTerminator();

		if (llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(terminator)) {
			if (branchInst->isUnconditional()) {
				branchInst->setSuccessor(0, to);
			}
			else if (branchInst->getSuccessor(0) == old) {
				branchInst->setSuccessor(0, to);
			}
			else if (branchInst->getSuccessor(1) == old) {
				branchInst->setSuccessor(1, to);
			}
		}
	}

	void stepOver(llvm::BasicBlock* pred, llvm::BasicBlock* steppedOver) {
		auto successor = steppedOver->getSingleSuccessor(); // We know it only has 1 successor, since its terminator is an unconditional branch.
		changeBranch(pred, successor, steppedOver);
		successor->replacePhiUsesWith(steppedOver, pred);
		steppedOver->eraseFromParent();
	}

	void stepOverBlocksWhenPossible(llvm::Function* f) {
		llvm::MapVector<llvm::BasicBlock*, bool> blocksWithConditionalPreviouslyOptimized;
		
		bool changes = true;
		int i = 0;

		while (changes) {
			changes = false;

			for (auto& block : *f) {
				if (detection::hasUnconditionalBranch(&block)) {
					auto successor = block.getSingleSuccessor();
					
					if (detection::blockCanBeSteppedOver(successor)) {
						stepOver(&block, successor);
						changes = true;
						break;
					}
				}
				else if (detection::hasConditionalBranch(&block) && blocksWithConditionalPreviouslyOptimized.count(&block) == 0) {
					auto successors = aux::getConditionalSuccessors(&block);
					
					auto thenCanBeSteppedOver = detection::blockCanBeSteppedOver(successors.first);
					auto elsedCanBeSteppedOver = detection::blockCanBeSteppedOver(successors.second);

					/*
						Note that even if BOTH successors can be stepped over, doing it would result in
						changes to the program's behavior, since it'd ruin PHI nodes. For example:

						%if:
							...
							br %cond, label %then, label %else

						%then:
							br label %end

						%else:
							br label %end

						%end:
							%x = phi([%then, %y], [%else, %z])
							...

						Thus, in these cases we arbitrarily chose to step over then then block, and
						leave the else as is.
					*/
					if (thenCanBeSteppedOver) {
						stepOver(&block, successors.first);
						blocksWithConditionalPreviouslyOptimized[&block] = true;
						changes = true;
						break;
					}
					else if (elsedCanBeSteppedOver) {
						stepOver(&block, successors.second);
						blocksWithConditionalPreviouslyOptimized[&block] = true;
						changes = true;
						break;
					}
				}
			}

			i++;
			if (i > ws::constants::MAX_ITERATIONS_FOR_STEP_OVER_BLOCKS)
				break;
		}
	}

	//void eliminateUnnecesaryBranches(llvm::Function* f) {...}
}

llvm::PreservedAnalyses ws::WingspanCFGSimplifier::run(llvm::Function& f, llvm::FunctionAnalysisManager& fam) {
	simplification::stepOverBlocksWhenPossible(&f);

	return llvm::PreservedAnalyses::all();
}

bool ws::WingspanCFGSimplifier::registerPipelinePass(llvm::StringRef name, llvm::FunctionPassManager& fpm, llvm::ArrayRef<llvm::PassBuilder::PipelineElement> /*unused*/) {
	if (name.consume_front("wingspan-simplify-cfg")) {
		fpm.addPass(WingspanCFGSimplifier{});
		return true;
	}

	return false;
}
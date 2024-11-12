#include "wingspan-dce.h"
#include "wingspan-constants.h"
#include "plugin-registration.h"

#include "llvm/ADT/STLExtras.h"

#define PRINT_INFO true

namespace aux {
	template<typename T>
	bool isIn(T* item, const llvm::SmallVector<T*>& list) {
		return std::find(list.begin(), list.end(), item) != list.end();
	}
}

namespace detection {
	void populateReachableBlocks(llvm::BasicBlock* block, llvm::MapVector<llvm::BasicBlock*, bool>& reacheable) {
		if (reacheable.count(block))
			return;

		reacheable[block] = true;

		// Recursively call over block's successors.
		for (llvm::succ_iterator SI = succ_begin(block), SE = succ_end(block); SI != SE; ++SI) {
			llvm::BasicBlock* succ = *SI;
			populateReachableBlocks(succ, reacheable);
		}
	}
	
	llvm::SmallVector<llvm::BasicBlock*> unreachableBlocks(llvm::Function& f) {
		llvm::MapVector<llvm::BasicBlock*, bool> reacheable;
		populateReachableBlocks(&f.getEntryBlock(), reacheable);

		llvm::SmallVector<llvm::BasicBlock*> unreacheable;

		for (auto &bb : f) {
			if (!reacheable[&bb])
				unreacheable.push_back(&bb);
		}

		return unreacheable;
	}

	bool canBeDead(llvm::Instruction* instr) {
		// Terminators of a live block can't be dead (There's a Terminator movie joke here somewhere)
		if (instr->isTerminator())
			return false;
		
		// Some instructions can be alive even if they have 0 users. 
		// These are mostly memory instructions and CALL.
		switch (instr->getOpcode()) {
		case llvm::Instruction::Store:
		case llvm::Instruction::Fence:
		case llvm::Instruction::AtomicCmpXchg:
		case llvm::Instruction::AtomicRMW:
		case llvm::Instruction::Call:
			return false;
		}

		return true;
	}

	llvm::SmallVector<llvm::Instruction*> deadInstructions(llvm::Function& f) {
		llvm::SmallVector<llvm::Instruction*> deadInstrs;
		
		for (auto& bb : f) {
			for (auto& instr : bb) {
				if (canBeDead(&instr) && instr.hasNUses(0))
					deadInstrs.push_back(&instr);
			}
		}

		return deadInstrs;
	}
}

namespace elimination {
	void eliminatePhiUsesOfDeadBlock(llvm::BasicBlock* block) {
		for (auto& instr : *block) {
			for (auto user : instr.users()) {
				if (auto phi = llvm::dyn_cast<llvm::PHINode>(user)) {
					for (int i = 0; i < phi->getNumIncomingValues(); i++) {
						if (phi->getIncomingValue(i) == &instr) {
							phi->removeIncomingValue(i);
						}
					}
				}
			}
		}
	}

	void eliminateDeadBasicBlocks(llvm::Function& f) {
		auto deadBlocks = detection::unreachableBlocks(f);

		if (PRINT_INFO) {
			llvm::errs() << "[In "<< f.getName() << "] The following basic blocks were detected as dead, and will therefore be deleted:\n";

			for (auto db : deadBlocks) {
				llvm::errs() << *db << "\n\n";
			}
		}

		for (auto bb : deadBlocks) {
			eliminatePhiUsesOfDeadBlock(bb);
			bb->eraseFromParent();
		}
	}

	void eliminateDeadInstructions(llvm::Function& f) {
		llvm::SmallVector<llvm::Instruction*> worklist = detection::deadInstructions(f);
		int iterations = 0;

		while (worklist.size() > 0) {
			auto item = worklist.back();
			worklist.pop_back();

			if (PRINT_INFO)
				llvm::errs() << "[In " << f.getName() << "] The following instr was detected as dead, and will therefore be deleted:" << *item << "\n";

			for (int i = 0; i < item->getNumOperands(); i++) {
				auto operand = item->getOperand(i);

				if (auto instr = llvm::dyn_cast<llvm::Instruction>(operand))
					if (detection::canBeDead(instr) && instr->hasNUses(0) && !aux::isIn(instr, worklist)) {
						worklist.push_back(instr);
						llvm::errs() << "\tAdding the following instr. to worklist:" << *instr << "\n";
					}
			}

			item->eraseFromParent();

			iterations++;
			if (iterations > ws::constants::MAX_ITERATIONS_FOR_DEAD_CODE_DETECTION)
				break;
		}
	}
}


llvm::PreservedAnalyses ws::WingspanDeadCodeEliminator::run(llvm::Function& f, llvm::FunctionAnalysisManager& fam) {
	elimination::eliminateDeadBasicBlocks(f);

	/*
		By this point, the only blocks in f are live blocks. We turn our attention to instructions.
		This way, we only operate over living basic blocks and not do useless work.
	*/

	elimination::eliminateDeadInstructions(f);

	return llvm::PreservedAnalyses::none();
}

bool ws::WingspanDeadCodeEliminator::registerPipelinePass(llvm::StringRef name, llvm::FunctionPassManager& fpm, llvm::ArrayRef<llvm::PassBuilder::PipelineElement> /*unused*/) {
	if (name.consume_front("wingspan-dce")) {
		fpm.addPass(WingspanDeadCodeEliminator{});
		return true;
	}

	return false;
}
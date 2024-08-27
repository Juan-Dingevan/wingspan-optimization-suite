#include "wingspan-mem2reg.h"
#include "plugin-registration.h"

#include "string.h"

#include "llvm/IR/Instruction.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/IteratedDominanceFrontier.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/CFG.h"

#define PRINT_INFO true
#define DIRECTION_OPERAND 1

// Data structures required for the pass.
// We declare them as global variables for ease of use.

// List w/ all the alloca instructions in the function.
llvm::SmallVector<llvm::AllocaInst*, 64> allocs;

// def[v] is a list with all the blocks in which the variable v is written to,
// And in this case, with 'variable' we mean alloca instruction.
llvm::MapVector<llvm::AllocaInst*, llvm::SmallVector<llvm::BasicBlock*, 0>> def;

// phiToVar[p] gets the variable (alloca) for which the phi node was created
llvm::MapVector<llvm::PHINode*, llvm::AllocaInst*> phiToVar;

// stacks[v] is a stack (list) of the values that v takes.
llvm::MapVector<llvm::AllocaInst*, llvm::SmallVector<llvm::Value*, 0>> stacks;

// trash is a list of instructions that we'll delete at the end of the pass.
llvm::SmallVector<llvm::Instruction*> trash;

namespace debug {
	void printAllocsInfo(llvm::Function& f) {
		llvm::errs() << "The function " << f.getName() << " has " << allocs.size() << " alloca instructions.\n";
		llvm::errs() << "\n";
	}

	void printDef() {
		llvm::MapVector<llvm::BasicBlock*, int> unnamedBlockMap;
		int unnamedBlockCounter = 0;
		int var = 0;

		for (const auto& pair : def) {
			llvm::AllocaInst* allocInst = pair.first;
			auto v = allocInst->getOperand(1);
			const llvm::SmallVector<llvm::BasicBlock*, 0>& blocks = pair.second;

			// Imprimir nombre de la variable (Value v)
			llvm::errs() << "def[var" << var << "] = {";
			var++;

			// Imprimir lista de nombres de BasicBlocks
			for (size_t i = 0; i < blocks.size(); ++i) {
				llvm::BasicBlock* bb = blocks[i];
				if (bb->hasName()) {
					llvm::errs() << bb->getName();
				}
				else {
					// Verificar si ya hemos asignado un número a este bloque
					if (unnamedBlockMap.find(bb) == unnamedBlockMap.end()) {
						unnamedBlockMap[bb] = ++unnamedBlockCounter;
					}
					llvm::errs() << "unnamed_block_" << unnamedBlockMap[bb];
				}

				// Separador entre nombres, pero no después del último elemento
				if (i < blocks.size() - 1) {
					llvm::errs() << ", ";
				}
			}

			llvm::errs() << "}\n";
		}
	}
}

namespace {
	void populateAllocs(llvm::Function* f) {
		for (auto& instr : f->getEntryBlock()) {
			llvm::AllocaInst* alloca = llvm::dyn_cast<llvm::AllocaInst>(&instr);
			if (alloca) {
				allocs.push_back(alloca);
			}
		}
	}

	void populateSingleVariableDef(llvm::AllocaInst* v) {
		for (auto* u : v->users()) {
			if (auto store = llvm::dyn_cast<llvm::StoreInst>(u)) {
				if (store->getOperand(DIRECTION_OPERAND) == v) {
					def[v].push_back(store->getParent());
				}
			}
		}
	}

	void populateDef() {
		for (auto v : allocs) {
			populateSingleVariableDef(v);
		}
	}

	void insertPhiNodes(llvm::Function& F, llvm::DominatorTree& DT) {
		llvm::ForwardIDFCalculator IDF(DT);
		
		for (auto v : allocs) {
			//Not the most efficient way to do this, but it's the only way to do it without refactoring.
			//I'll refactor eventually, I think...
			llvm::SmallPtrSet<llvm::BasicBlock*, 32> defSet;

			for (auto bb : def[v]) {
				defSet.insert(bb);
			}

			llvm::SmallVector<llvm::BasicBlock*, 32> phiBlocks;

			IDF.setDefiningBlocks(defSet);
			IDF.calculate(phiBlocks);

			//Put a phi node on every basic block that needs one.
			for (auto phiBlock : phiBlocks) {
				auto phi = llvm::PHINode::Create(
					v->getAllocatedType(),					 //type of the phi node
					2,										 //Number of incoming edges
					"",										 //unique name (none; let the compiler handle names)
					llvm::InsertPosition(&phiBlock->front()) //"insert before the first instr. of the block"
				);

				phiToVar[phi] = v;
			}
		}
	}

	bool isLocalAllocation(llvm::AllocaInst* inst) {
		for (auto v : allocs)
			if (v == inst)
				return true;

		return false;
	}

	void rename(llvm::DomTreeNode* node) {
		llvm::MapVector<llvm::Instruction*, int> pushes;

		for (auto v : allocs) {
			pushes[v] = 0;
		}

		auto bb = node->getBlock();

		// Do the renaming in our current node
		for (auto &instr : *bb) {
			if (auto load = llvm::dyn_cast<llvm::LoadInst>(&instr)) {
				auto loadsFrom = load->getOperand(0);
				auto loadsFromAsAlloca = llvm::dyn_cast<llvm::AllocaInst>(loadsFrom);

				// We need to verify that it's loading from one of the function's alloca instrs.
				// if it ISN'T doing so, we simply pass this load; we don't want to rename it.
				// it is most likely loading the value of a global variable.
				if (!isLocalAllocation(loadsFromAsAlloca))
					continue;


				auto newValue = stacks[loadsFromAsAlloca].back();
				instr.replaceAllUsesWith(newValue);
				trash.push_back(load);
			}
			else if (auto store = llvm::dyn_cast<llvm::StoreInst>(&instr)) {
				auto stores = store->getOperand(0);
				auto storesIn = store->getOperand(1);
				auto storesInAsAlloca = llvm::dyn_cast<llvm::AllocaInst>(storesIn);

				// Analogue.
				if (!isLocalAllocation(storesInAsAlloca))
					continue;

				stacks[storesInAsAlloca].push_back(stores);
				trash.push_back(store);
				pushes[storesInAsAlloca]++;
			}
			else if (auto phi = llvm::dyn_cast<llvm::PHINode>(&instr)) {
				auto alloca = phiToVar[phi];

				// In theory, unoptimized code never has phi nodes, so it'd be
				// impossible to try to incorrectly rename a phi node (since all
				// the phi nodes in the program will have been inserted by us, but
				// just for the sake of completeness, we add the check.
				if (!isLocalAllocation(alloca))
					continue;

				stacks[alloca].push_back(phi);
				pushes[alloca]++;
			}
		}

		// Add incoming edges to the phi nodes of our current node's successors
		for (llvm::succ_iterator successor = llvm::succ_begin(bb), E = llvm::succ_end(bb); successor != E; ++successor) {
			for (auto &instr : **successor) {
				if (auto phi = llvm::dyn_cast<llvm::PHINode>(&instr)) {
					auto v = phiToVar[phi];
					auto newValue = stacks[v].back();
					phi->addIncoming(newValue, bb);
				}
			}
		}

		// Call recursively
		for (auto child : node->children()) {
			rename(child);
		}

		// Clean the pushed values.
		for (auto v : allocs) {
			for (int i = 0; i < pushes[v]; i++) {
				stacks[v].pop_back();
			}
		}
	}

	void clearTrash() {
		for (auto t : trash) {
			t->eraseFromParent();
		}

		for (auto v : allocs) {
			v->eraseFromParent();
		}
	}

	void resetDataStructures() {
		allocs.clear();
		def.clear();
		phiToVar.clear();
		stacks.clear();
		trash.clear();
	}
}

llvm::PreservedAnalyses ws::WingspanMem2Reg::run(llvm::Function& f, llvm::FunctionAnalysisManager& fam) {
	populateAllocs(&f);

	if (PRINT_INFO) {
		debug::printAllocsInfo(f);
	}

	populateDef();

	if (PRINT_INFO) {
		debug::printDef();
	}

	llvm::DominatorTree DT(f);

	insertPhiNodes(f, DT);

	rename(DT.getNode(&f.getEntryBlock()));

	clearTrash();

	resetDataStructures();
	
	return llvm::PreservedAnalyses::none();
}

bool ws::WingspanMem2Reg::registerPipelinePass(llvm::StringRef name, llvm::FunctionPassManager& fpm, llvm::ArrayRef<llvm::PassBuilder::PipelineElement> /*unused*/) {
	if (name.consume_front("wingspan-mem2reg")) {
		fpm.addPass(WingspanMem2Reg{});
		return true;
	}

	return false;
}
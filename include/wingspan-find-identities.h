#if !defined(WINGSPAN_FIND_IDENTITIES_H_)
#define WINGSPAN_FIND_IDENTITIES_H_

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"

#include "plugin-registration.h"

// We'll structure this as a series of analysis, each of
// which will focus on a single operand. We'll keep all 
// class definitions and implementations in one file, so
// as not to create 6 or 8 files for just one pass.

namespace ws {
	class AdditionIdentityFinder : public llvm::AnalysisInfoMixin<AdditionIdentityFinder> {
	public:
		using Result = llvm::SmallVector<llvm::Instruction*, 0>;

		Result run(llvm::Function& f, llvm::FunctionAnalysisManager& fam);

		static inline constexpr llvm::StringRef name() { return "addition-identity-finder"; }

	private:
		friend class llvm::AnalysisInfoMixin<AdditionIdentityFinder>;
		friend void RegisterPluginPasses(llvm::PassBuilder& passBuilder);

		static inline llvm::AnalysisKey Key;

		static void registerAnalysis(llvm::FunctionAnalysisManager& am);

	};

	class SubtractionIdentityFinder : public llvm::AnalysisInfoMixin<SubtractionIdentityFinder> {
	public:
		using Result = llvm::SmallVector<llvm::Instruction*, 0>;

		Result run(llvm::Function& f, llvm::FunctionAnalysisManager& fam);

		static inline constexpr llvm::StringRef name() { return "subtraction-identity-finder"; }

	private:
		friend class llvm::AnalysisInfoMixin<SubtractionIdentityFinder>;
		friend void RegisterPluginPasses(llvm::PassBuilder& passBuilder);

		static inline llvm::AnalysisKey Key;

		static void registerAnalysis(llvm::FunctionAnalysisManager& am);

	};

	class MultiplicationIdentityFinder : public llvm::AnalysisInfoMixin<MultiplicationIdentityFinder> {
	public:
		using Result = llvm::SmallVector<llvm::Instruction*, 0>;

		Result run(llvm::Function& f, llvm::FunctionAnalysisManager& fam);

		static inline constexpr llvm::StringRef name() { return "multiplication-identity-finder"; }

	private:
		friend class llvm::AnalysisInfoMixin<MultiplicationIdentityFinder>;
		friend void RegisterPluginPasses(llvm::PassBuilder& passBuilder);

		static inline llvm::AnalysisKey Key;

		static void registerAnalysis(llvm::FunctionAnalysisManager& am);

	};

	class DivisionIdentityFinder : public llvm::AnalysisInfoMixin<DivisionIdentityFinder> {
	public:
		using Result = llvm::SmallVector<llvm::Instruction*, 0>;

		Result run(llvm::Function& f, llvm::FunctionAnalysisManager& fam);

		static inline constexpr llvm::StringRef name() { return "division-identity-finder"; }

	private:
		friend class llvm::AnalysisInfoMixin<DivisionIdentityFinder>;
		friend void RegisterPluginPasses(llvm::PassBuilder& passBuilder);

		static inline llvm::AnalysisKey Key;

		static void registerAnalysis(llvm::FunctionAnalysisManager& am);

	};

	class PowersOfTwoIdentityFinder : public llvm::AnalysisInfoMixin<PowersOfTwoIdentityFinder> {
	public:
		using Result = llvm::SmallVector<llvm::Instruction*, 0>;

		Result run(llvm::Function& f, llvm::FunctionAnalysisManager& fam);

		static inline constexpr llvm::StringRef name() { return "powers-of-two-identity-finder"; }

	private:
		friend class llvm::AnalysisInfoMixin<PowersOfTwoIdentityFinder>;
		friend void RegisterPluginPasses(llvm::PassBuilder& passBuilder);

		static inline llvm::AnalysisKey Key;

		static void registerAnalysis(llvm::FunctionAnalysisManager& am);

	};

	class BooleanIdentityFinder : public llvm::AnalysisInfoMixin<BooleanIdentityFinder> {
	public:
		using Result = llvm::SmallVector<llvm::Instruction*, 0>;

		Result run(llvm::Function& f, llvm::FunctionAnalysisManager& fam);

		static inline constexpr llvm::StringRef name() { return "boolean-identity-finder"; }

	private:
		friend class llvm::AnalysisInfoMixin<BooleanIdentityFinder>;
		friend void RegisterPluginPasses(llvm::PassBuilder& passBuilder);

		static inline llvm::AnalysisKey Key;

		static void registerAnalysis(llvm::FunctionAnalysisManager& am);

	};

	class BranchIdentityFinder : public llvm::AnalysisInfoMixin<BranchIdentityFinder> {
	public:
		using Result = llvm::SmallVector<llvm::Instruction*, 0>;

		Result run(llvm::Function& f, llvm::FunctionAnalysisManager& fam);

		static inline constexpr llvm::StringRef name() { return "branch-identity-finder"; }

	private:
		friend class llvm::AnalysisInfoMixin<BranchIdentityFinder>;
		friend void RegisterPluginPasses(llvm::PassBuilder& passBuilder);

		static inline llvm::AnalysisKey Key;

		static void registerAnalysis(llvm::FunctionAnalysisManager& am);

	};

	class PhiIdentityFinder : public llvm::AnalysisInfoMixin<PhiIdentityFinder> {
	public:
		using Result = llvm::SmallVector<llvm::Instruction*, 0>;

		Result run(llvm::Function& f, llvm::FunctionAnalysisManager& fam);

		static inline constexpr llvm::StringRef name() { return "phi-identity-finder"; }

	private:
		friend class llvm::AnalysisInfoMixin<PhiIdentityFinder>;
		friend void RegisterPluginPasses(llvm::PassBuilder& passBuilder);

		static inline llvm::AnalysisKey Key;

		static void registerAnalysis(llvm::FunctionAnalysisManager& am);

	};
}

#endif
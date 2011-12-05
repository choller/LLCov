//===- Hello.cpp - Example code from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements two versions of the LLVM "Hello World" pass described
// in docs/WritingAnLLVMPass.html
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "llcov"
#include "llvm/Pass.h"
#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/DebugInfo.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Instrumentation.h"

#include <iostream>

using namespace llvm;

struct LLCov: public ModulePass {
public:
   static char ID; // Pass identification, replacement for typeid
   LLCov();

   virtual bool runOnModule( Module &M );

protected:
   virtual bool runOnFunction( Function &F, StringRef filename );
   Constant* getInstrumentationFunction();

   Module* M;
};

char LLCov::ID = 0;
INITIALIZE_PASS(LLCov, "llcov", "LLCov: allow live coverage measurement of program code.", false, false)

LLCov::LLCov() : ModulePass( ID ) {}

bool LLCov::runOnModule( Module &M ) {
   this->M = &M;

   bool modified = false;

   NamedMDNode *CU_Nodes = this->M->getNamedMetadata("llvm.dbg.cu");
   if (!CU_Nodes) return false;

   /* Iterate through all compilation units */
   for (unsigned i = 0, e = CU_Nodes->getNumOperands(); i != e; ++i) {
      DICompileUnit CU(CU_Nodes->getOperand(i));

      /* Iterate through all sub programs */
      DIArray SPs = CU.getSubprograms();
      for (unsigned i = 0, e = SPs.getNumElements(); i != e; ++i) {
         DISubprogram SP(SPs.getElement(i));
         if (!SP.Verify()) continue;

         Function *F = SP.getFunction();
         if (!F) continue;

         modified |= runOnFunction( *F, SP.getFilename() );
      }

   }

   /*for ( Module::iterator F = M.begin(), E = M.end(); F != E; ++F ) {
      if ( F->isDeclaration() )
         continue;
      modified |= runOnFunction( *F );
   }*/

   return modified;
}

bool LLCov::runOnFunction( Function &F, StringRef filename ) {
   errs() << "Hello: ";
   errs().write_escaped( F.getName() ) << '\n';

   /* Iterate over all basic blocks in this function */
   for ( Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB ) {
      TerminatorInst *TI = BB->getTerminator();
      // TODO: Why do I need the # of successors here?
      //int Successors = isa<ReturnInst> ( TI ) ? 1 : TI->getNumSuccessors();
      //if ( Successors ) {

      //}

      IRBuilder<> Builder( TI );

      //StringRef filename;
      unsigned int line = 0;

      /* Iterate over the instructions in the BasicBlock to find line number */
      for ( BasicBlock::iterator I = BB->begin(), IE = BB->end(); I != IE; ++I ) {
         const DebugLoc &Loc = I->getDebugLoc();
         if ( Loc.isUnknown() )
            continue;

         // Line
         line = Loc.getLine();

         // File
         //filename = DISubprogram( Loc.getAsMDNode( BB->getContext() ) ).getFilename();

         break;
      }

      /* Create arguments for our function */
      Value* lineVal = ConstantInt::get(Type::getInt32Ty(M->getContext()), line, false);
      Value* filenameVal = Builder.CreateGlobalStringPtr(filename);

      /* Add function call: void func(uint8_t[] filename, uint32_t line);  */
      Builder.CreateCall2( getInstrumentationFunction(), filenameVal, lineVal );
   }

   return true;
}


/* The function returned here will reside in an .so */
Constant* LLCov::getInstrumentationFunction() {
   Type *Args[] = {
                    Type::getInt8PtrTy( M->getContext() ), // uint8_t* filename
                    Type::getInt32Ty( M->getContext() ) // uint32_t line
         };
   FunctionType *FTy = FunctionType::get( Type::getVoidTy( M->getContext() ), Args, false );
   return M->getOrInsertFunction( "llvm_llcov_block_call", FTy );
}

/* Externally called global function to create our pass */
ModulePass *llvm::createLLCovPass() {
   return new LLCov();
}

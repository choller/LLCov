//===- LLCov.cpp - Live Coverage Instrumentation Pass for LLVM  -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE-LLVM.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a block coverage instrumentation that calls
// into a runtime library whenever a basic block is executed.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "llcov"
#include "llvm/Pass.h"
#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/DebugInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/system_error.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Type.h"


#include <iostream>
#include <fstream>
#include <string>
#include <sstream>

using namespace llvm;

/* Start of helper classes */

/* Container class for our list entries */
struct LLCovListEntry {
public:
   LLCovListEntry(const std::string &fileOrFuncName, bool isFunction)
      : myHasFilename(!isFunction), myHasFunction(isFunction), myHasLine(false),
        myFilename(), myFunction(), myLine(0) {
         if (isFunction) { myFunction = fileOrFuncName; }
         else { myFilename = fileOrFuncName; }
   }
   LLCovListEntry(const std::string &fileName, const std::string &funcName)
      : myHasFilename(true), myHasFunction(true), myHasLine(false),
        myFilename(fileName), myFunction(funcName), myLine(0) {}
   LLCovListEntry(const std::string &funcName)
      : myHasFilename(false), myHasFunction(true), myHasLine(false),
        myFilename(), myFunction(funcName), myLine(0) {}
   LLCovListEntry(const std::string &fileName, const std::string &funcName, unsigned int line)
      : myHasFilename(true), myHasFunction(true), myHasLine(true),
        myFilename(fileName), myFunction(funcName), myLine(line) {}

   bool matchFileName(const std::string &fileName) {
      return (fileName == myFilename);
   }

   bool matchFuncName(const std::string &funcName) {
      return (funcName == myFunction);
   }

   bool matchFileFuncName(const std::string &fileName, const std::string &funcName) {
      return (matchFileName(fileName) && matchFuncName(funcName));
   }

   bool matchAll(const std::string &fileName, const std::string &funcName, unsigned int line) {
      return ((line == myLine) && matchFileFuncName(fileName, funcName));
   }

   bool matchFileLine(const std::string &fileName, unsigned int line) {
      return ((line == myLine) && matchFileName(fileName));
   }

   bool hasFilename() { return myHasFilename; }
   bool hasFunction() { return myHasFunction; }
   bool hasLine() { return myHasLine; }

protected:
   bool myHasFilename;
   bool myHasFunction;
   bool myHasLine;
   std::string myFilename;
   std::string myFunction;
   unsigned int myLine;
};

struct LLCovList {
public:
   LLCovList(const std::string &path);
   virtual bool doCoarseMatch( StringRef filename, Function &F );
   virtual bool doExactMatch( StringRef filename, Function &F );
   virtual bool doExactMatch( StringRef filename, Function &F, unsigned int line );
   virtual bool doExactMatch( StringRef filename, unsigned int line );
   virtual bool isEmpty() { return myEntries.empty(); }
protected:
   virtual bool doMatch(StringRef filename, Function &F, bool exact);
   std::vector<LLCovListEntry> myEntries;
};

bool LLCovList::doMatch(StringRef filename, Function &F, bool exact) {
   /*
    * Search one entry that mentiones either this file
    * or this function to return true.
    */
   for (std::vector<LLCovListEntry>::iterator it = myEntries.begin(); it != myEntries.end() ; it++ ) {
      if (exact && it->hasLine()) { continue; }
      if (it->hasFilename()) {
         if (exact && it->hasFunction()) {
            if (it->matchFileFuncName(filename, F.getName().str())) { return true; }
         } else {
            if (it->matchFileName(filename)) { return true; }
         }
      } else {
         /* Must have function */
         if (it->matchFuncName(F.getName().str())) {
            return true;
         }
      }
   }

   return false;
}

/* Check if there are any list entries that mention this file OR function */
bool LLCovList::doCoarseMatch( StringRef filename, Function &F ) {
   return doMatch(filename, F, false);
}

/* Check if there are any list entries that mention this file OR function (and no line).
 * Additionally, if the file is specified, the function must match too. */
bool LLCovList::doExactMatch( StringRef filename, Function &F ) {
   return doMatch(filename, F, true);
}

/* Check if there are any list entries that match all three attributes exactly. */
bool LLCovList::doExactMatch( StringRef filename, Function &F, unsigned int line ) {
   for (std::vector<LLCovListEntry>::iterator it = myEntries.begin(); it != myEntries.end() ; it++ ) {
      if (!it->hasLine() || !it->hasFunction() || !it->hasFilename()) continue;
      if (it->matchAll(filename, F.getName().str(), line)) return true;
   }
   return false;
}

/* Check if there are any list entries that match the function/line attributes exactly. */
bool LLCovList::doExactMatch( StringRef filename, unsigned int line ) {
   for (std::vector<LLCovListEntry>::iterator it = myEntries.begin(); it != myEntries.end() ; it++ ) {
      if (!it->hasLine() || !it->hasFilename()) continue;
      if (it->matchFileLine(filename, line)) return true;
   }
   return false;
}

LLCovList::LLCovList(const std::string &path) : myEntries() {
   /* If no file is specified, do nothing */
   if (!path.size()) return;

   std::ifstream fileStream;
   fileStream.open(path.c_str());

   if (!fileStream) {
      report_fatal_error("Unable to open specified file " + path);
   }

   std::string line;
   getline(fileStream, line);
   while (fileStream) {
      /* Process one line here */
       std::string token;
       std::istringstream tokStream(line);

       std::string file;
       std::string func;
       std::string line;

       tokStream >> token;
       while(tokStream) {
          /* Process one token here */
          std::istringstream entryStream(token);

          std::string type;
          std::string val;
          getline(entryStream, type, ':');
          getline(entryStream, val, ':');

          if (type == "file") {
             file = val;
          } else if (type == "func") {
             func = val;
          } else if (type == "line") {
             line = val;
          } else {
             report_fatal_error("Invalid type \"" + type + "\" in file " + path);
          }

          if (!entryStream) {
             report_fatal_error("Unable to open specified file " + path);
          }

          tokStream >> token;
       }

       if (file.size()) {
          if (func.size()) {
             if (line.size()) {
                LLCovListEntry entry(file, func, atoi(line.c_str()));
                myEntries.push_back(entry);
             } else {
                LLCovListEntry entry(file, func);
                myEntries.push_back(entry);
             }
          } else if(line.size()) {
             LLCovListEntry entry(file, atoi(line.c_str()));
             myEntries.push_back(entry);
          } else {
             LLCovListEntry entry(file, false);
             myEntries.push_back(entry);
          }
       } else {
          if (func.size()) {
             if (line.size()) {
                report_fatal_error("Cannot use line without file in file " + path);
             }
             LLCovListEntry entry(func, true);
             myEntries.push_back(entry);
          } else {
             report_fatal_error("Must either specify file or function in file " + path);
          }
       }

       getline(fileStream, line);
   }
}

/* End of helper classes */

/*static cl::opt<std::string>  ClBlackListFile("llcov-blacklist",
          cl::desc("File containing the list of functions/files/lines "
                "to ignore during instrumentation"), cl::Hidden);

static cl::opt<std::string>  ClWhiteListFile("llcov-whitelist",
          cl::desc("File containing the list of functions/files/lines "
                "to instrument (all others are ignored)"), cl::Hidden);*/

struct LLCov: public ModulePass {
public:
   static char ID; // Pass identification, replacement for typeid
   LLCov();

   virtual bool runOnModule( Module &M );

protected:
   virtual bool runOnFunction( Function &F, StringRef filename );
   Constant* getInstrumentationFunction();

   Module* M;
   LLCovList* myBlackList;
   LLCovList* myWhiteList;
};

char LLCov::ID = 0;
INITIALIZE_PASS(LLCov, "llcov", "LLCov: allow live coverage measurement of program code.", false, false)

LLCov::LLCov() : ModulePass( ID ), M(NULL),
      myBlackList(new LLCovList(getenv("LLCOV_BLACKLIST") != NULL ? std::string(getenv("LLCOV_BLACKLIST")) : "" )),
      myWhiteList(new LLCovList(getenv("LLCOV_WHITELIST") != NULL ? std::string(getenv("LLCOV_WHITELIST")) : "" )) {}

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

   return modified;
}

bool LLCov::runOnFunction( Function &F, StringRef filename ) {
   //errs() << "Hello: ";
   //errs().write_escaped( F.getName() ) << '\n';

   bool ret = false;

   /*
    * White/BlackList logic: If the whitelist contains something,
    * then the set of all instrumented blocks is limited to that.
    * The blacklist can further restrict that set.
    */

   /*
    * Determine if the filename, the function, or the combination
    * of both is whitelisted and the blacklist does not restrict
    * that further. In that case, we don't need to check any more
    * lists during the basic block iteration which saves time.
    */
   bool instrumentAll = (
         myWhiteList->isEmpty() // Whitelist is either empty
         || myWhiteList->doExactMatch(filename, F)) // or must yield a match for function or filename
         && !myBlackList->doCoarseMatch(filename, F); // and blacklist must not have any coarse matches

   if (myBlackList->doExactMatch(filename, F)) {
      /*
       * If the filename, the function, or the combination of both
       * is on the blacklist, don't do anything here
       */
      return false;
   }

   /* Iterate over all basic blocks in this function */
   for ( Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB ) {
      /*
       * If the whitelist is empty, we start with the assumption that we
       * need to instrument the block (can be changed by blacklist).
       * If the whitelist is not empty, then the default is to not instrument
       */
      bool instrumentBlock = myWhiteList->isEmpty();

      TerminatorInst *TI = BB->getTerminator();

      IRBuilder<> Builder( TI );

      bool haveLine = false;
      unsigned int line = 0;

      /* Iterate over the instructions in the BasicBlock to find line number */
      for ( BasicBlock::iterator I = BB->begin(), IE = BB->end(); I != IE; ++I ) {
         const DebugLoc &Loc = I->getDebugLoc();

         if ( Loc.isUnknown() )
            continue;

         /* Save the line if we don't have it yet */
         if (!haveLine) {
            line = Loc.getLine();
            haveLine = true;

            /* No need to iterate further if we know already that we should instrument */
            if (instrumentAll) {
               break;
            }
         }

         /* Check white- and blacklists. A blacklist match immediately aborts */
         instrumentBlock |= myWhiteList->doExactMatch(filename, F, Loc.getLine());
         if (myBlackList->doExactMatch(filename, Loc.getLine())) {
            instrumentBlock = false;
            break;
         }
      }

      if (instrumentAll || instrumentBlock) {
         /* Create arguments for our function */
         Value* funcNameVal = Builder.CreateGlobalStringPtr(F.getName());
         Value* filenameVal = Builder.CreateGlobalStringPtr(filename);
         Value* lineVal = ConstantInt::get(Type::getInt32Ty(M->getContext()), line, false);

         /* Add function call: void func(const char* function, const char* filename, uint32_t line);  */
         Builder.CreateCall3( getInstrumentationFunction(), funcNameVal, filenameVal, lineVal );

         ret = true;
      }
   }

   return ret;
}


/* The function returned here will reside in an .so */
Constant* LLCov::getInstrumentationFunction() {
   Type *Args[] = {
                    Type::getInt8PtrTy( M->getContext() ), // uint8_t* function
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

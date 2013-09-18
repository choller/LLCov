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
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/DebugInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/system_error.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h"


#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <map>
#include <utility>

using namespace llvm;

/* Start of helper classes */

/* Container class for our list entries */
struct LLCovListEntry {
public:
   LLCovListEntry() : myHasLine(false), myHasRelblock(false) {}

   bool matchFileName(const std::string &fileName) {
      /* 
       * We don't check for filename equality here because
       * filenames might actually be full paths. Instead we
       * check that the actual filename ends in the filename
       * specified in the list.
       */
      if (fileName.length() >= myFilename.length()) {
         return (fileName.compare(fileName.length() - myFilename.length(), myFilename.length(), myFilename) == 0);
      } else {
         return false;
      }
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
   
   bool matchAllRelblock(const std::string &fileName, const std::string &funcName, unsigned int line, unsigned int relblock) {
      return ((myRelblock == relblock) && matchAll(fileName, funcName, line));
   }

   bool matchFileLine(const std::string &fileName, unsigned int line) {
      return ((line == myLine) && matchFileName(fileName));
   }

   bool matchFileLineRelblock(const std::string &fileName, unsigned int line, unsigned int relblock) {
      return (matchFileLine(fileName, line) && myRelblock == relblock);
   }
   bool hasFilename() { return myFilename.size(); }
   bool hasFunction() { return myFunction.size(); }
   bool hasLine() { return myHasLine; }
   bool hasRelblock() { return myHasRelblock; }

   const std::string& getFilename() { return myFilename; }

   void setFilename(const std::string &fileName) {
      myFilename = fileName;
   }

   void setFunction(const std::string &funcName) {
         myFunction = funcName;
   }

   void setLine(unsigned int line) {
         myLine = line;
         myHasLine = true;
   }

   void setRelblock(unsigned int relblock) {
         myRelblock = relblock;
         myHasRelblock = true;
   }
protected:
   bool myHasLine;
   bool myHasRelblock;
   std::string myFilename;
   std::string myFunction;
   unsigned int myLine;
   unsigned int myRelblock;
};

struct LLCovList {
public:
   LLCovList(const std::string &path);
   virtual bool doCoarseMatch( StringRef filename, Function &F );
   virtual bool doExactMatch( StringRef filename, Function &F );
   virtual bool doExactMatch( StringRef filename, Function &F, unsigned int line );
   virtual bool doExactMatch( StringRef filename, unsigned int line );
   virtual bool doExactMatch( StringRef filename, Function &F, unsigned int line, unsigned int relblock );
   virtual bool doExactMatch( StringRef filename, unsigned int line, unsigned int relblock );
   virtual bool isEmpty() { return myEntries.empty(); }
protected:
   virtual bool doMatch(StringRef filename, Function &F, bool exact);
   std::multimap<std::string, LLCovListEntry> myEntries;
};

bool LLCovList::doMatch(StringRef filename, Function &F, bool exact) {
   /*
    * Search one entry that mentiones either this file
    * or this function to return true.
    */
   // XXX: TODO: This and all of the following commented equal_range optimizations don't work because we're having trouble
   // with absolute vs. relative paths used for the filenames. Filename matching requires a substring match to the end.
   // Maybe a custom key comparison operator can solve this problem to make compilation with huge black- and whitelists
   // faster.

   /*std::pair<std::multimap<std::string, LLCovListEntry>::iterator, std::multimap<std::string, LLCovListEntry>::iterator> peqr
        = myEntries.equal_range(filename.str());

   for (std::multimap<std::string, LLCovListEntry>::iterator it = peqr.first; it != peqr.second; ++it) {*/
   for (std::multimap<std::string, LLCovListEntry>::iterator it = myEntries.begin(); it != myEntries.end(); ++it) {
      if (exact && (it->second.hasLine() || it->second.hasRelblock())) { continue; }
      if (it->second.hasFilename()) {
         if (exact && it->second.hasFunction()) {
            if (it->second.matchFileFuncName(filename, F.getName().str())) { return true; }
         } else {
            if (it->second.matchFileName(filename)) { return true; }
         }
      } else {
         /* Must have function */
         if (it->second.matchFuncName(F.getName().str())) {
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
   /*std::pair<std::multimap<std::string, LLCovListEntry>::iterator, std::multimap<std::string, LLCovListEntry>::iterator> peqr
        = myEntries.equal_range(filename.str());

   for (std::multimap<std::string, LLCovListEntry>::iterator it = peqr.first; it != peqr.second; ++it) {*/
   for (std::multimap<std::string, LLCovListEntry>::iterator it = myEntries.begin(); it != myEntries.end(); ++it) {
      if (!it->second.hasLine() || !it->second.hasFunction() || !it->second.hasFilename() || it->second.hasRelblock()) continue;
      if (it->second.matchAll(filename, F.getName().str(), line)) return true;
   }
   return false;
}

/* Check if there are any list entries that match all four attributes exactly. */
bool LLCovList::doExactMatch( StringRef filename, Function &F, unsigned int line, unsigned int relblock ) {
   /*std::pair<std::multimap<std::string, LLCovListEntry>::iterator, std::multimap<std::string, LLCovListEntry>::iterator> peqr
        = myEntries.equal_range(filename.str());

   for (std::multimap<std::string, LLCovListEntry>::iterator it = peqr.first; it != peqr.second; ++it) {*/
   for (std::multimap<std::string, LLCovListEntry>::iterator it = myEntries.begin(); it != myEntries.end(); ++it) {
      if (!it->second.hasLine() || !it->second.hasFunction() || !it->second.hasFilename() || !it->second.hasRelblock()) continue;
      if (it->second.matchAllRelblock(filename, F.getName().str(), line, relblock)) return true;
   }
   return false;
}

/* Check if there are any list entries that match the function/line attributes exactly. */
bool LLCovList::doExactMatch( StringRef filename, unsigned int line ) {
   /* std::pair<std::multimap<std::string, LLCovListEntry>::iterator, std::multimap<std::string, LLCovListEntry>::iterator> peqr
        = myEntries.equal_range(filename.str());

   for (std::multimap<std::string, LLCovListEntry>::iterator it = peqr.first; it != peqr.second; ++it) {*/
   for (std::multimap<std::string, LLCovListEntry>::iterator it = myEntries.begin(); it != myEntries.end(); ++it) {
      if (!it->second.hasLine() || !it->second.hasFilename() || it->second.hasRelblock()) continue;
      if (it->second.matchFileLine(filename, line)) return true;
   }
   return false;
}


/* Check if there are any list entries that match the function/line/relblock attributes exactly. */
bool LLCovList::doExactMatch( StringRef filename, unsigned int line, unsigned int relblock ) {
   /*std::pair<std::multimap<std::string, LLCovListEntry>::iterator, std::multimap<std::string, LLCovListEntry>::iterator> peqr
        = myEntries.equal_range(filename.str());

   for (std::multimap<std::string, LLCovListEntry>::iterator it = peqr.first; it != peqr.second; ++it) {*/
   for (std::multimap<std::string, LLCovListEntry>::iterator it = myEntries.begin(); it != myEntries.end(); ++it) {
      if (!it->second.hasLine() || !it->second.hasFilename() || !it->second.hasRelblock()) continue;
      if (it->second.matchFileLineRelblock(filename, line, relblock)) return true;
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

   std::string configLine;
   getline(fileStream, configLine);
   while (fileStream) {
      /* Process one line here */
       std::string token;
       std::istringstream tokStream(configLine);

       std::string file;
       std::string func;
       std::string line;
       std::string relblock;

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
          } else if (type == "relblock") {
             relblock = val;
          } else {
             report_fatal_error("Invalid type \"" + type + "\" in file " + path);
          }

          if (!entryStream) {
             report_fatal_error("Malformed token: " + token);
          }

          tokStream >> token;
       }

       LLCovListEntry entry;

       if ( file.size() ) {
         entry.setFilename( file );

         if ( func.size() )
            entry.setFunction( func );

         if ( line.size() )
            entry.setLine( atoi( line.c_str() ) );
         if ( relblock.size() ) {
            if ( !line.size() )
               report_fatal_error( "Cannot use relblock without line in file " + path );
            entry.setRelblock( atoi( relblock.c_str() ) );
         }

       } else if ( func.size() ) {
         if ( line.size() )
            report_fatal_error( "Cannot use line without file in file " + path );

         entry.setFunction( func );
       } else {
         report_fatal_error( "Must either specify file or function in file " + path );
       }

       myEntries.insert(std::pair<std::string, LLCovListEntry>(file, entry));

       getline(fileStream, configLine);
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
   virtual ~LLCov();

   virtual bool runOnModule( Module &M );

protected:
   virtual bool runOnFunction( Function &F, StringRef filename );
   Constant* getInstrumentationFunction();

   Module* M;
   LLCovList* myBlackList;
   LLCovList* myWhiteList;

   std::ofstream myLogInstStream;
   bool myDoLogInstrumentation;
   bool myDoLogInstrumentationDebug;
};

char LLCov::ID = 0;
INITIALIZE_PASS(LLCov, "llcov", "LLCov: allow live coverage measurement of program code.", false, false)

LLCov::LLCov() : ModulePass( ID ), M(NULL),
      myBlackList(new LLCovList(getenv("LLCOV_BLACKLIST") != NULL ? std::string(getenv("LLCOV_BLACKLIST")) : "" )),
      myWhiteList(new LLCovList(getenv("LLCOV_WHITELIST") != NULL ? std::string(getenv("LLCOV_WHITELIST")) : "" )),
      myDoLogInstrumentation(false), myDoLogInstrumentationDebug(false) {
      
      if (getenv("LLCOV_LOGINSTFILE") != NULL) {
         myDoLogInstrumentation = true;
         myLogInstStream.open(getenv("LLCOV_LOGINSTFILE"), std::ios::out | std::ios::app);
         if (getenv("LLCOV_LOGINSTDEBUG") != NULL) {
             myDoLogInstrumentationDebug = true;
	 }
      }
}

LLCov::~LLCov() {
	if (myDoLogInstrumentation) {
		myLogInstStream.close();
	}
}

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

   bool whiteListEmptyOrExactMatch = myWhiteList->isEmpty() || myWhiteList->doExactMatch(filename, F);
   /*
    * Determine if the filename, the function, or the combination
    * of both is whitelisted and the blacklist does not restrict
    * that further. In that case, we don't need to check any more
    * lists during the basic block iteration which saves time.
    */
   bool instrumentAll = whiteListEmptyOrExactMatch // Whitelist is either empty or must yield a match for function or filename
         && !myBlackList->doCoarseMatch(filename, F); // and blacklist must not have any coarse matches

   if (myBlackList->doExactMatch(filename, F)) {
      /*
       * If the filename, the function, or the combination of both
       * is on the blacklist, don't do anything here
       */
      return false;
   }
   int lastBBLine = -1;
   unsigned int relblock = 0;

   /* Iterate over all basic blocks in this function */
   for ( Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB ) {
      /*
       * If the whitelist is empty, we start with the assumption that we
       * need to instrument the block (can be changed by blacklist).
       * If the whitelist is not empty, then the default depends on the whole
       * file or function being whitelisted entirely.
       */
      bool instrumentBlock = whiteListEmptyOrExactMatch;

      TerminatorInst *TI = BB->getTerminator();

      IRBuilder<> Builder( TI );

      bool haveLine = false;
      unsigned int line = 0;

      bool haveFile = false;
      StringRef blockFilename;

      /* Iterate over the instructions in the BasicBlock to find line number */
      for ( BasicBlock::iterator I = BB->begin(), IE = BB->end(); I != IE; ++I ) {
         DebugLoc Loc = I->getDebugLoc();

         if ( Loc.isUnknown() )
            continue;

         DILocation cDILoc(Loc.getAsMDNode(M->getContext()));
         DILocation oDILoc = cDILoc.getOrigLocation();
         
	 unsigned int instLine = oDILoc.getLineNumber();
         StringRef instFilename = oDILoc.getFilename();

         if (instFilename.str().empty()) {
	    /* If the original location is empty, use the actual location */
            instFilename = cDILoc.getFilename();
            instLine = cDILoc.getLineNumber();
	    /* If that fails as well (no location at all), skip this block */
            if (instFilename.str().empty()) continue;
         }

         /* Save the line if we don't have it yet */
         if (!haveLine) {
            line = instLine;
            haveLine = true;
            /* If we're still in the same line as the last basic block was,
             * increase the relative basic block count to distinguish the
             * the blocks in the callback later */
            if (line == lastBBLine) {
                relblock++;
            } else {
                /* New line, reset relative basic block count to 0 */
                relblock = 0;
            }
            
            /* Store away line of last basic block */
            lastBBLine = line;

            // Also resolve the file now that this block originally belonged to
            blockFilename = instFilename;

            if (blockFilename != filename) {
                if (myBlackList->doCoarseMatch(blockFilename, F)) {
                    // The file we are including from is blacklisted
                    instrumentBlock = false;
                    break;
                }

                if (!myWhiteList->isEmpty() && !myWhiteList->doExactMatch(blockFilename, F)) {
                    // The file we are including isn't whitelisted
                    instrumentBlock = false;
                    break;
                }
            }

            /* No need to iterate further if we know already that we should instrument */
            if (instrumentAll) {
               break;
            }
         }
         if (myDoLogInstrumentationDebug && myDoLogInstrumentation)
             myLogInstStream << "Checking " << instFilename.str() << " line: " << instLine << " blockline " << line << " relblock " << relblock << std::endl;

         /* Check white- and blacklists. A blacklist match immediately aborts */
         if (!instrumentBlock) {
         instrumentBlock = instrumentBlock 
                            || myWhiteList->doExactMatch(instFilename, instLine)
                            || myWhiteList->doExactMatch(instFilename, instLine, relblock);
            //if (instrumentBlock) myLogInstStream << "Decision made for " << instFilename.str() << " line: " << instLine << " blockline " << line << std::endl;
         }
         if (myBlackList->doExactMatch(instFilename, instLine) || myBlackList->doExactMatch(instFilename, instLine, relblock)) {
            instrumentBlock = false;
            break;
         }
      }

      if ((instrumentAll && haveLine && blockFilename == filename) || (haveLine && instrumentBlock)) {
         /* Create arguments for our function */
         Value* funcNameVal = Builder.CreateGlobalStringPtr(F.getName());
         Value* filenameVal = Builder.CreateGlobalStringPtr(blockFilename);
         Value* lineVal = ConstantInt::get(Type::getInt32Ty(M->getContext()), line, false);
         Value* relblockVal = ConstantInt::get(Type::getInt32Ty(M->getContext()), relblock, false);

         /* Add function call: void func(const char* function, const char* filename, uint32_t line, uint32_t relblock);  */
         Builder.CreateCall4( getInstrumentationFunction(), funcNameVal, filenameVal, lineVal, relblockVal );

         if (myDoLogInstrumentation) {
            myLogInstStream << "file:" << blockFilename.str() << " " << "func:" << F.getName().str() << " " << "line:" << line << std::endl;
         }

         ret = true;
      } else {
	if (myDoLogInstrumentationDebug && myDoLogInstrumentation) {
	    myLogInstStream << "DEBUG: " << myWhiteList->doExactMatch(filename, F) << instrumentAll << haveLine << (blockFilename == filename) << instrumentBlock << " " << blockFilename.str() << " " << filename.str() << std::endl;
	}
      }
   }

   return ret;
}


/* The function returned here will reside in an .so */
Constant* LLCov::getInstrumentationFunction() {
   Type *Args[] = {
                    Type::getInt8PtrTy( M->getContext() ), // uint8_t* function
                    Type::getInt8PtrTy( M->getContext() ), // uint8_t* filename
                    Type::getInt32Ty( M->getContext() ), // uint32_t line
                    Type::getInt32Ty( M->getContext() ) // uint32_t relblock
         };
   FunctionType *FTy = FunctionType::get( Type::getVoidTy( M->getContext() ), Args, false );
   return M->getOrInsertFunction( "llvm_llcov_block_call", FTy );
}

/* Externally called global function to create our pass */
ModulePass *llvm::createLLCovPass() {
   return new LLCov();
}

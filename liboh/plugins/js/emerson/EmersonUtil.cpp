#include "EmersonUtil.h"
#include "Util.h"
#include "EmersonException.h"

#include <antlr3.h>
#include <iostream>
#include <fstream>
#include <string>
#include "EmersonLexer.h"
#include "EmersonParser.h"
#include "EmersonTree.h"
#include "EmersonInfo.h"

using namespace std;



void myRecoverFromMismatchedSet(struct ANTLR3_BASE_RECOGNIZER_struct* _recognizer, pANTLR3_BITSET_LIST _follow)
{
}


void* myRecoverFromMismatchedToken(struct ANTLR3_BASE_RECOGNIZER_struct* _recognizer, ANTLR3_UINT32 _ttype, pANTLR3_BITSET_LIST _follow)
{
    return NULL;
}


extern pANTLR3_UINT8  EmersonParserTokenNames[];
extern void* myRecoverFromMismatchedToken(struct ANTLR3_BASE_RECOGNIZER_struct*, ANTLR3_UINT32, pANTLR3_BITSET_LIST);


pEmersonTree _treeParser;


pANTLR3_STRING emerson_printAST(pANTLR3_BASE_TREE tree)
{
    return emerson_printAST(tree,EmersonParserTokenNames);
}


// This version of the function should be called from the main compiler

bool EmersonUtil::emerson_compile(
    std::string _originalFile, const char* em_script_str, std::string& toCompileTo,
    int& errorNum, EmersonErrorFuncType error_cb, EmersonLineMap* lineMap)
{
    return emerson_compile(_originalFile, em_script_str, toCompileTo,errorNum, error_cb, NULL, lineMap);
}

bool EmersonUtil::emerson_compile(
    std::string _originalFile, const char* em_script_str, std::string& toCompileTo,
    int& errorNum, EmersonErrorFuncType errorFunction, FILE* dbg, EmersonLineMap* lineMap)
{


    
  EmersonInfo* _emersonInfo = new EmersonInfo();
  if(_originalFile.size() > 0 )
  {
    _emersonInfo->push(_originalFile);
  }

  if(errorFunction)
  {
    _emersonInfo->errorFunctionIs(errorFunction);
  }

  _emersonInfo->mismatchTokenFunctionIs(&myRecoverFromMismatchedToken);

  bool returner = emerson_compile(
      em_script_str, toCompileTo,errorNum, dbg, lineMap,_emersonInfo);
  
  delete _emersonInfo;
  return returner;
}

// This is mor basic version of the function. Should be called from else where
bool EmersonUtil::emerson_compile(
    const char* em_script_str, std::string& toCompileTo, int& errorNum,
    FILE* dbg, EmersonLineMap* lineMap, EmersonInfo* _emersonInfo)
{
    static boost::mutex EmMutex;
    boost::mutex::scoped_lock locker (EmMutex);
    
    if (dbg != NULL) fprintf(dbg, "Trying to compile \n %s\n", em_script_str);


    pANTLR3_UINT8 str = (pANTLR3_UINT8)em_script_str;
    pANTLR3_INPUT_STREAM input = antlr3NewAsciiStringCopyStream(str, strlen(em_script_str), NULL);
    bool returner = false;
    
    pEmersonLexer lxr;
    pEmersonParser psr;
    pANTLR3_COMMON_TOKEN_STREAM tstream;
    EmersonParser_program_return emersonAST;
    pANTLR3_COMMON_TREE_NODE_STREAM	nodes;
    pEmersonTree treePsr;


    if (input == NULL)
    {
        fprintf(stderr, "Unable to create input stream");
        if (dbg != NULL) fprintf(dbg, "Unable to create input stream");
        exit(ANTLR3_ERR_NOMEM);
    }

    lxr= EmersonLexerNew(input);
    if ( lxr == NULL )
    {
        fprintf(stderr, "Unable to create the lexer due to malloc() failure1\n");
        if (dbg != NULL) fprintf(dbg, "Unable to create the lexer due to malloc() failure1\n");
        exit(ANTLR3_ERR_NOMEM);
    }
    tstream = antlr3CommonTokenStreamSourceNew(ANTLR3_SIZE_HINT, TOKENSOURCE(lxr));

    if (tstream == NULL)
    {
        fprintf(stderr, "Out of memory trying to allocate token stream\n");
        if (dbg != NULL) fprintf(dbg, "Out of memory trying to allocate token stream\n");
        exit(ANTLR3_ERR_NOMEM);
    }


    psr= EmersonParserNew(tstream);  // CParserNew is generated by ANTLR3
    if (psr == NULL)
    {
        fprintf(stderr, "Out of memory trying to allocate parser\n");
        if (dbg != NULL) fprintf(dbg, "Out of memory trying to allocate parser\n");
        exit(ANTLR3_ERR_NOMEM);
    }

    // set the error function here

    if(_emersonInfo && _emersonInfo->errorFunction())
    {
      psr->pParser->rec->displayRecognitionError = (void(*)(struct ANTLR3_BASE_RECOGNIZER_struct*, pANTLR3_UINT8*))_emersonInfo->errorFunction();;

      psr->pParser->rec->recoverFromMismatchedToken = (void*(*)(struct ANTLR3_BASE_RECOGNIZER_struct*, ANTLR3_UINT32, pANTLR3_BITSET_LIST))_emersonInfo->mismatchTokenFunction();
    }


    try
    {
      emersonAST = psr->program(psr);
    }
    catch(EmersonException e)
    {
      throw e;
    }
    if (psr->pParser->rec->state->errorCount > 0)
    {
        fprintf(stderr, "The parser returned %d errors, tree walking aborted.\n", psr->pParser->rec->state->errorCount);
        if (dbg != NULL) fprintf(dbg, "The parser returned %d errors, tree walking aborted.\n", psr->pParser->rec->state->errorCount);
        errorNum = ANTLR3_ERR_NOMEM;
    }
    else
    {
        if (dbg != NULL) fprintf(dbg, "Emerson Tree after parsing \n%s\n", emerson_printAST(emersonAST.tree)->chars);

        //printf("Emerson Tree after parsing \n%s\n", emerson_printAST(emersonAST.tree)->chars);
        nodes = antlr3CommonTreeNodeStreamNewTree(emersonAST.tree, ANTLR3_SIZE_HINT); // sIZE
                                                                                      // HINT
                                                                                      // WILL
                                                                                      // SOON
                                                                                      // BE
                                                                                      // DEPRECATED!!

        treePsr= EmersonTreeNew(nodes);
        _treeParser = treePsr;

        EmersonTree_program_return returnValues = treePsr->program(treePsr);
        ANTLR3_STRING_struct* mString = returnValues.return_str;
        char* intermediate = (char*)mString->chars;
        int sizeCode = mString->len;
        toCompileTo = std::string(intermediate,sizeCode);
        returner = true;

        if (lineMap != NULL) {
            for (int i = 0; i < returnValues.numLines; i++) {
                (*lineMap)[returnValues.jsLines[i]] = returnValues.emersonLines[i];
            }
        }
        free(returnValues.jsLines);
        free(returnValues.emersonLines);
        
        if (dbg != NULL) fprintf(dbg, "The generated code is \n %s \n", toCompileTo.c_str());

        nodes   ->free  (nodes);	    nodes	= NULL;
        treePsr ->free  (treePsr);	    treePsr	= NULL;
    }

    psr->free(psr);
    psr= NULL;

    tstream->free  (tstream);
    tstream= NULL;

    lxr->free(lxr);
    lxr= NULL;

    input->close (input);
    input= NULL;


    return returner;
}

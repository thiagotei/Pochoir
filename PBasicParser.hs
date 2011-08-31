{-
 ----------------------------------------------------------------------------------
 -  Copyright (C) 2010-2011  Massachusetts Institute of Technology
 -  Copyright (C) 2010-2011  Yuan Tang <yuantang@csail.mit.edu>
 - 		                     Charles E. Leiserson <cel@mit.edu>
 - 	 
 -   This program is free software: you can redistribute it and/or modify
 -   it under the terms of the GNU General Public License as published by
 -   the Free Software Foundation, either version 3 of the License, or
 -   (at your option) any later version.
 -
 -   This program is distributed in the hope that it will be useful,
 -   but WITHOUT ANY WARRANTY; without even the implied warranty of
 -   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 -   GNU General Public License for more details.
 -
 -   You should have received a copy of the GNU General Public License
 -   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 -
 -   Suggestsions:                  yuantang@csail.mit.edu
 -   Bugs:                          yuantang@csail.mit.edu
 -
 --------------------------------------------------------------------------------
 -}

module PBasicParser where

import Text.ParserCombinators.Parsec
import qualified Text.ParserCombinators.Parsec.Token as Token
import Text.ParserCombinators.Parsec.Expr
import Text.ParserCombinators.Parsec.Language
import Text.Read (read)

import Data.Char
import Data.List
import qualified Data.Map as Map

import PShow
import PUtils
import PData

{- all the token parsers -}
lexer :: Token.TokenParser st 
lexer = Token.makeTokenParser (javaStyle
             { commentStart = "/*",
               commentEnd = "*/",
               commentLine = "//",
               identStart = letter <|> oneOf "_'",
               identLetter = alphaNum <|> oneOf "_'", 
               nestedComments = True,
               reservedOpNames = ["*", "/", "+", "-", "!", "&&", "||", "=", ">", ">=", 
                                  "<", "<=", "==", "!=", "+=", "-=", "*=", "&=", "|=", 
                                  "<<=", ">>=", "^=", "++", "--", "?", ":", "&", "|", "~",
                                  ">>", "<<", "%", "^", "->"],
               reservedNames = ["Pochoir_Array", "Pochoir", "Pochoir_Domain", 
                                "Pochoir", "Pochoir_Kernel", "Pochoir_Guard", 
                                "auto", "{", "};", "const", "volatile", "register", 
                                "#define", "int", "float", "double", 
                                "bool", "true", "false",
                                "if", "else", "switch", "case", "break", "default",
                                "while", "do", "for", "return", "continue"],
               caseSensitive = True})

{- definition of all token parser -}
whiteSpace = Token.whiteSpace lexer
lexeme = Token.lexeme lexer
symbol = Token.symbol lexer
natural = Token.natural lexer
number = Token.naturalOrFloat lexer
integer = Token.integer lexer
brackets = Token.brackets lexer
braces = Token.braces lexer
parens = Token.parens lexer
angles = Token.angles lexer
semi = Token.semi lexer
colon = Token.colon lexer
dot = Token.dot lexer
identifier = Token.identifier lexer
reserved = Token.reserved lexer
reservedOp = Token.reservedOp lexer
comma = Token.comma lexer
semiSep = Token.semiSep lexer
semiSep1 = Token.semiSep1 lexer
commaSep = Token.commaSep lexer
commaSep1 = Token.commaSep1 lexer
charLiteral = Token.charLiteral lexer
stringLiteral = Token.stringLiteral lexer

-- pIdentifier doesn't strip whites, compared with 'identifier', 
-- so we can preserve the relative order of original source input
pIdentifier :: GenParser Char ParserState String
pIdentifier = do l_start <- letter <|> char '_'
                 l_body <- many (alphaNum <|> char '_' <?> "Wrong Identifier")
                 return (l_start : l_body)

pDelim :: GenParser Char ParserState String
pDelim = do try comma
            return ", "
     <|> do try semi
            return ";\n"
     <|> do try $ symbol ")"
            return ") "

pMember :: String -> GenParser Char ParserState String
pMember l_memFunc = 
    do l_start <- char '.'
       l_body <- string l_memFunc
       return (l_memFunc)

pPochoirKernelParams :: GenParser Char ParserState (PName, PName)
pPochoirKernelParams = 
    do l_shape <- identifier
       comma
       l_kernelFunc <- identifier
       return (l_shape, l_kernelFunc)

pParseTileKernel :: GenParser Char ParserState PTileKernel
pParseTileKernel =
     do try $ symbol "{"
        l_tiles <- commaSep pParseTileKernel
        symbol "}"
        return $ LK l_tiles
 <|> do l_kernelName <- try $ identifier
        l_state <- getState
        case Map.lookup l_kernelName $ pKernel l_state of
             -- Nothing -> return emptyTileKernel
             Nothing -> return $ SK emptyKernel { kName = l_kernelName }
             Just l_kernel -> return $ SK l_kernel 

ppArray :: String -> ParserState -> GenParser Char ParserState String
ppArray l_id l_state =
        do try $ pMember "Register_Boundary"
           l_boundaryFn <- parens pIdentifier
           semi
           case Map.lookup l_id $ pArray l_state of
               Nothing -> return (l_id ++ ".Register_Boundary(" ++ l_boundaryFn ++ "); /* UNKNOWN Register_Boundary with " ++ l_id ++ "*/" ++ breakline)
               Just l_array -> 
                    do updateState $ updateArrayBoundary l_id True 
                       return (l_id ++ ".Register_Boundary(" ++ l_boundaryFn ++ "); /* Register_Boundary */" ++ breakline)
    <|> do try $ pMember "Register_Shape"
           l_shape <- parens identifier
           semi
           case Map.lookup l_id $ pArray l_state of
               Nothing -> return (l_id ++ ".Register_Shape(" ++ l_shape ++ "); /* UNKNOWN Register_Shape with " ++ l_id ++ "*/" ++ breakline)
               Just l_pArray ->
                   case Map.lookup l_shape $ pShape l_state of
                       Nothing -> return (l_id ++ ".Register_Shape(" ++ l_shape ++ "); /* UNKNOWN Register_Shape with " ++ l_shape ++ "*/" ++ breakline)
                       Just l_pShape -> return ("/* Known */" ++ l_id ++ ".Register_Shape(" ++ l_shape ++ ");" ++ breakline)

ppStencil :: String -> ParserState -> GenParser Char ParserState String
ppStencil l_id l_state = 
        do try $ pMember "Register_Array"
           l_arrays <- parens $ commaSep1 identifier
           semi
           case Map.lookup l_id $ pStencil l_state of 
               Nothing -> return (l_id ++ ".Register_Array(" ++ 
                                  intercalate ", " l_arrays ++ 
                                  "); /* UNKNOWN Register_Array with" ++ 
                                  l_id ++ "*/" ++ breakline)
               Just l_stencil -> 
                    do let l_pArrayStatus = map (flip checkValidPArray l_state) l_arrays
                       let l_validPArray = foldr (&&) True $ map fst l_pArrayStatus
                       if (l_validPArray == True) 
                          then do let l_pArrays = map snd l_pArrayStatus 
                                  let l_revPArrays = map (flip fillToggleInPArray (sToggle l_stencil)) l_pArrays
                                  updateStencilArray l_id l_revPArrays
                                  return (l_id ++ ".Register_Array (" ++ 
                                          intercalate ", " l_arrays ++ 
                                          "); /* Known Register Array */" ++ breakline)
                          else return (l_id ++ ".Register_Array(" ++ 
                                       intercalate ", " l_arrays ++ 
                                       "); /* UNKNOWN Pochoir Array */" ++ breakline)
    -- Ad hoc implementation of Run_Unroll
    <|> do try $ pMember "Run"
           l_tstep <- parens exprStmtDim
           semi
           case Map.lookup l_id $ pStencil l_state of
               Nothing -> return (breakline ++ l_id ++ ".Run(" ++ show l_tstep ++ 
                                  "); /* Run with UNKNOWN Stencil " ++ l_id ++ 
                                  " */" ++ breakline)
               Just l_stencil -> 
                          do let l_arrayInUse = sArrayInUse l_stencil
                             let l_regBound = foldr (||) False $ map (getArrayRegBound l_state) l_arrayInUse 
                             updateState $ updateStencilBoundary l_id l_regBound 
                             return (breakline ++ l_id ++ ".Run(" ++ show l_tstep ++
                                     ");" ++ breakline)
    -- convert "Register_Kernel(g, k, ... ks) " to 
    -- "Register_Stagger_Obase_Kernels(g, k, bk)"
    <|> do try $ pMember "Register_Stagger_Kernels"
           (l_guard, l_kernels) <- parens pStencilRegisterKernelParams
           semi
           case Map.lookup l_id $ pStencil l_state of
               Nothing -> return (l_id ++ ".Register_Stagger_Kernels(" ++ l_guard ++ 
                                  ", " ++ intercalate ", " l_kernels ++ 
                                  "); /* UNKNOWN Stencil " ++ 
                                  l_id ++ "*/" ++ breakline)
               Just l_stencil ->
                   do let l_arrayInUse = sArrayInUse l_stencil
                      let l_unroll = length l_kernels
                      updateState $ updateStencilUnroll l_id l_unroll
                      l_newState <- getState
                      let l_revStencil = getPStencil l_id l_newState l_stencil
                      let l_pKernels = map (getValidKernel l_newState) l_kernels
                      let l_pGuard = getValidGuard l_newState l_guard
                      let l_validKernel = foldr (&&) True $ map fst l_pKernels
                      let l_validGuard = fst l_pGuard
                      if (l_validKernel == False || l_validGuard == False) 
                         then return (l_id ++ ".Register_Stagger_Kernels(" ++ 
                                      l_guard ++ ", " ++ intercalate ", " l_kernels ++ 
                                      ");" ++ "/* Not all kernels are valid */ " ++ 
                                      breakline)
                         else do let l_sRegKernel = sRegKernel l_stencil
                                 let l_rev_sRegKernel = l_sRegKernel ++ [(snd l_pGuard, map snd l_pKernels)]
                                 updateState $ updateStencilRegKernel l_id l_rev_sRegKernel
                                 let l_pShapes = map kShape (map snd l_pKernels)
                                 let l_merged_pShape = foldr mergePShapes (sShape l_revStencil) l_pShapes
                                 updateState $ updateStencilToggle l_id (shapeToggle l_merged_pShape)
                                 updateState $ updateStencilTimeShift l_id (shapeTimeShift l_merged_pShape)
                                 updateState $ updateStencilShape l_id l_merged_pShape
                                 -- We don't return anything in Register_Kernel 
                                 -- until 'Run', because we know the Register_Array
                                 -- only after Register_Kernel
                                 return (l_id ++ ".Register_Stagger_Kernels(" ++
                                         l_guard ++ ", " ++ 
                                         intercalate ", " l_kernels ++ ");" ++ 
                                         "/* All kernels are recognized! */" ++
                                         breakline)
    <|> do return (l_id)

-- get all iterators from Kernel
transKernel :: PStencil -> PMode -> PKernelFunc -> PKernelFunc
transKernel l_stencil l_mode l_kernelFunc =
       let l_exprStmts = kfStmt l_kernelFunc
           l_kernelFuncParams = kfParams l_kernelFunc
           l_iters =
                   case l_mode of 
                       PMacroShadow -> getFromStmts getIter PRead 
                                    (transArrayMap $ sArrayInUse l_stencil) 
                                    l_exprStmts
                       PCPointer -> getFromStmts getIter PRead
                                    (transArrayMap $ sArrayInUse l_stencil) 
                                    l_exprStmts
                       PPointer -> getFromStmts (getPointer $ l_kernelFuncParams) PRead 
                                    (transArrayMap $ sArrayInUse l_stencil) 
                                    l_exprStmts
                       POptPointer -> getFromStmts getIter PRead
                                    (transArrayMap $ sArrayInUse l_stencil) 
                                    l_exprStmts 
                       PCaching -> getFromStmts (getPointer $ l_kernelFuncParams) PRead
                                    (transArrayMap $ sArrayInUse l_stencil) 
                                    l_exprStmts 
                       PMUnroll -> let l_get = 
                                            if sRank l_stencil < 3 
                                                then getIter
                                                else (getPointer $ l_kernelFuncParams)
                                   in  getFromStmts l_get PRead
                                         (transArrayMap $ sArrayInUse l_stencil) 
                                         l_exprStmts 
                       PDefault -> let l_get = 
                                            if sRank l_stencil < 3 
                                                then getIter
                                                else (getPointer $ l_kernelFuncParams)
                                   in  getFromStmts l_get PRead
                                         (transArrayMap $ sArrayInUse l_stencil) 
                                         l_exprStmts 
           l_revIters = transIterN 0 l_iters
       in  l_kernelFunc { kfIter = l_revIters }

pShowRegKernel :: PMode -> PStencil -> (PGuard, [PKernel]) -> String
pShowRegKernel l_mode l_stencil (l_guard, l_kernels) =
    let l_revKernelFunc = map (transKernel l_stencil l_mode) (map kFunc l_kernels) 
        l_id = sName l_stencil
        l_guardName = gName l_guard
    in  case l_mode of
             PDefault -> 
                 let l_showKernel = 
                       if sRank l_stencil < 3
                          then pShowSingleOptPointerKernel
                          else pShowSinglePointerKernel
                 in  pSplitKernel
                      ("Default_", l_id, l_guardName, l_revKernelFunc, 
                        l_stencil) 
                      l_showKernel
             PMUnroll -> 
                 let l_showKernel = 
                       if sRank l_stencil < 3
                          then pShowSingleOptPointerKernel
                          else pShowSinglePointerKernel
                 in  pUnrollMultiKernel
                      ("MUnroll_", l_id, l_guardName, l_revKernelFunc, 
                        l_stencil) 
                      l_showKernel
             PMacroShadow -> 
                 pSplitScope 
                   ("macro_", l_id, l_guardName, l_revKernelFunc, 
                     l_stencil) 
                   pShowUnrolledMacroKernels
             PPointer -> 
                  pSplitKernel
                   ("Pointer_", l_id, l_guardName, l_revKernelFunc, 
                     l_stencil) 
                   pShowSinglePointerKernel
             POptPointer -> 
                  pSplitKernel 
                   ("Opt_Pointer_", l_id, l_guardName, l_revKernelFunc,
                     l_stencil) 
                   pShowSingleOptPointerKernel
             PCaching -> 
                  pSplitScope 
                   ("Caching_", l_id, l_guardName, l_revKernelFunc, 
                     l_stencil) 
                   (pShowUnrolledCachingKernels l_stencil)
             PCPointer -> 
                  pSplitKernel 
                   ("C_Pointer_", l_id, l_guardName, l_revKernelFunc, 
                     l_stencil) 
                   pShowSingleCPointerKernel

-- for mode -unroll-multi-kernel
pUnrollMultiKernel :: (String, String, String, [PKernelFunc], PStencil) -> (Bool -> String -> Int -> Int -> [PKernelFunc] -> String) -> String
pUnrollMultiKernel (l_tag, l_id, l_guard, l_kernels, l_stencil) l_showSingleKernel = 
    let oldKernelName = intercalate "_" $ map kfName l_kernels
        bdryKernelName = l_tag ++ "boundary_" ++ oldKernelName
        obaseKernelName = l_tag ++ "interior_" ++ oldKernelName
        regBound = sRegBound l_stencil
        unroll = length l_kernels
        bdryKernel = if regBound 
                        then pShowMUnrolledBoundaryKernels False bdryKernelName 
                                l_stencil l_kernels 
                        else ""
        obaseKernel = pShowMUnrolledKernels obaseKernelName l_stencil l_kernels l_showSingleKernel
        runKernel = if regBound then obaseKernelName ++ ", " ++ bdryKernelName  
                                else obaseKernelName 
        l_pShape = pSysShape $ foldr mergePShapes emptyShape (map kfShape l_kernels)
    in  (breakline ++ show l_pShape ++
         breakline ++ bdryKernel ++ 
         breakline ++ obaseKernel ++ 
         breakline ++ l_id ++ ".Register_Stagger_Obase_Kernels(" ++ l_guard ++ ", " ++ 
         show unroll ++ ", " ++ runKernel ++ ");" ++ breakline)

-- For modes : -split-pointer -split-opt-pointer -split-c-pointer
pSplitKernel :: (String, String, String, [PKernelFunc], PStencil) -> (Bool -> String -> Int -> Int -> [PKernelFunc] -> String) -> String
pSplitKernel (l_tag, l_id, l_guard, l_kernels, l_stencil) l_showSingleKernel = 
    let oldKernelName = intercalate "_" $ map kfName l_kernels
        bdryKernelName = l_tag ++ "boundary_" ++ oldKernelName
        obaseKernelName = l_tag ++ "interior_" ++ oldKernelName
        cond_bdryKernelName = l_tag ++ "cond_boundary_" ++ oldKernelName
        cond_obaseKernelName = l_tag ++ "cond_interior_" ++ oldKernelName
        regBound = sRegBound l_stencil
        unroll = length l_kernels
        bdryKernel = if regBound 
                        then pShowUnrolledBoundaryKernels False bdryKernelName 
                                l_stencil l_kernels 
                        else ""
        cond_bdryKernel = pShowUnrolledBoundaryKernels True 
                                    cond_bdryKernelName l_stencil l_kernels
        obaseKernel = pShowUnrolledKernels False obaseKernelName l_stencil l_kernels l_showSingleKernel
        cond_obaseKernel = pShowUnrolledKernels True cond_obaseKernelName l_stencil l_kernels l_showSingleKernel
        runKernel = if regBound then obaseKernelName ++ ", " ++ 
                                     cond_obaseKernelName ++ ", " ++ 
                                     bdryKernelName ++ ", " ++ 
                                     cond_bdryKernelName
                                else obaseKernelName ++ ", " ++ 
                                     cond_obaseKernelName
        l_pShape = pSysShape $ foldr mergePShapes emptyShape (map kfShape l_kernels)
    in  (breakline ++ show l_pShape ++
         bdryKernel ++ breakline ++ cond_bdryKernel ++ 
         obaseKernel ++ breakline ++ cond_obaseKernel ++
         l_id ++ ".Register_Stagger_Obase_Kernels(" ++ l_guard ++ ", " ++ 
         show unroll ++ ", " ++ runKernel ++ ");" ++ breakline)

-- For modes : -split-macro-shadow, -split-caching
pSplitScope :: (String, String, String, [PKernelFunc], PStencil) -> (Bool -> String -> [PKernelFunc] -> String) -> String
pSplitScope (l_tag, l_id, l_guard, l_kernels, l_stencil) l_showKernels = 
    let oldKernelName = intercalate "_" $ map kfName l_kernels
        bdryKernelName = l_tag ++ "boundary_" ++ oldKernelName
        obaseKernelName = l_tag ++ "interior_" ++ oldKernelName
        cond_bdryKernelName = l_tag ++ "cond_boundary_" ++ oldKernelName
        cond_obaseKernelName = l_tag ++ "cond_interior_" ++ oldKernelName
        regBound = sRegBound l_stencil
        unroll = length l_kernels
        bdryKernel = if regBound 
                        then pShowUnrolledBoundaryKernels False bdryKernelName 
                                l_stencil l_kernels 
                        else ""
{-
        cond_bdryKernel = if unroll > 1
                             then pShowUnrolledBoundaryKernels True 
                                    cond_bdryKernelName l_stencil l_kernels
                             else bdryKernel
                                 -}
        cond_bdryKernel = pShowUnrolledBoundaryKernels True 
                                   cond_bdryKernelName l_stencil l_kernels
        obaseKernel = l_showKernels False obaseKernelName l_kernels
        cond_obaseKernel = l_showKernels True cond_obaseKernelName l_kernels
        runKernel = if regBound then obaseKernelName ++ ", " ++ 
                                     cond_obaseKernelName ++ ", " ++ 
                                     bdryKernelName ++ ", " ++ 
                                     cond_bdryKernelName
                                else obaseKernelName ++ ", " ++ 
                                     cond_obaseKernelName
        l_pShape = pSysShape $ foldr mergePShapes emptyShape (map kfShape l_kernels)
    in (breakline ++ show l_pShape ++
        bdryKernel ++ breakline ++ cond_bdryKernel ++ 
        obaseKernel ++ breakline ++ cond_obaseKernel ++
        l_id ++ ".Register_Stagger_Obase_Kernels(" ++ l_guard ++ ", " ++ 
        show unroll ++ ", " ++ runKernel ++ ");" ++ breakline)

-------------------------------------------------------------------------------------------
--                             Following are C++ Grammar Parser                         ---
-------------------------------------------------------------------------------------------
pStencilRun :: GenParser Char ParserState (String, String)
pStencilRun = 
        do l_tstep <- try exprStmtDim
           comma
           l_func <- identifier
           return (show l_tstep, l_func)
    <?> "Stencil Run Parameters"

-- parse the input parameters of Register_Kernel:
-- it contains one guard function, and multiple computing kernels
pStencilRegisterKernelParams :: GenParser Char ParserState (String, [String])
pStencilRegisterKernelParams = 
        do l_guard <- identifier 
           comma
           l_kernels <- commaSep1 identifier
           return (l_guard, l_kernels)
    <?> "Stencil Register_Kernel Parameters"

-- pDeclStatic <type, rank>
pDeclStatic :: GenParser Char ParserState (PType, PValue)
pDeclStatic = do l_type <- pType 
                 comma
                 -- exprDeclDim is an int that has to be known at compile-time
                 l_rank <- exprDeclDim
                 return (l_type, l_rank)

pDeclStaticNum :: GenParser Char ParserState (PValue)
pDeclStaticNum = do l_rank <- exprDeclDim
                    return (l_rank)

pDeclDynamic :: GenParser Char ParserState ([PName], PName, [DimExpr])
pDeclDynamic = do (l_qualifiers, l_name) <- try pVarDecl
--                  l_dims <- parens (commaSep1 exprDeclDim)
                  -- exprStmtDim is something might be known at Run-time
                  l_dims <- option [] (parens $ commaSep1 exprStmtDim)
                  return (l_qualifiers, l_name, l_dims)

pDeclPochoir :: GenParser Char ParserState ([PName], PName, String)
pDeclPochoir = do (l_qualifiers, l_name) <- try pVarDecl
                  return (l_qualifiers, l_name, "")

pVarDecl :: GenParser Char ParserState ([PName], PName)
pVarDecl = do l_qualifiers <- many cppQualifier
              l_name <- identifier
              return (l_qualifiers, l_name)

cppQualifier :: GenParser Char ParserState PName
cppQualifier = 
       do reservedOp "*"
          return "*"
   <|> do reservedOp "&"
          return "&"
   <|> do reserved "const"
          return "const"
   <|> do reserved "volatile"
          return "volatile"
   <|> do reserved "register"
          return "register"

ppShape :: GenParser Char ParserState [Int]
ppShape = do l_shape <- braces (commaSep1 $ integer >>= return . fromInteger)
             return (l_shape)

{- parse a single statement which is ended by ';' -}
pStubStatement :: GenParser Char ParserState Stmt
pStubStatement = do stmt <- manyTill anyChar $ try semi
                    return (UNKNOWN $ stmt ++ ";")
             <|> do stmt <- manyTill anyChar $ try $ symbol "}"
                    return (UNKNOWN $ stmt ++ "}")
             <|> do stmt <- manyTill anyChar $ try eol 
                    return (UNKNOWN $ stmt)

{- parse a single statement which is ended by ';' 
 - new version : return the expr (syntax tree) instead of string
 -}
pStatement :: GenParser Char ParserState Stmt
pStatement = try pParenStmt 
         <|> try pDeclLocalStmt
         <|> try pIfStmt
         <|> try pSwitchStmt
         <|> try pDoStmt
         <|> try pWhileStmt
         <|> try pForStmt
         <|> try pExprStmt
         <|> try pNOPStmt
       --  <|> try pNOOPStmt
         <|> try pRetStmt
         <|> try pReturnStmt
         <|> try pContStmt
          -- pStubStatement scan in everything else except the "Pochoir_kernel_end" or "};"
         <|> try pStubStatement
         <?> "Statement"

-- pNOOPStmt :: GenParser Char ParserState Stmt
-- pNOOPStmt =
--     do return NOP

pNOPStmt :: GenParser Char ParserState Stmt
pNOPStmt =
    do semi
       return NOP

pContStmt :: GenParser Char ParserState Stmt
pContStmt =
    do reserved "continue"
       semi
       return (CONT)

pReturnStmt :: GenParser Char ParserState Stmt
pReturnStmt =
    do reserved "return"
       semi
       return (RETURN)

pRetStmt :: GenParser Char ParserState Stmt
pRetStmt =
    do reserved "return"
       l_expr <- try exprStmt
       semi
       return (RET l_expr)

pExprStmt :: GenParser Char ParserState Stmt
pExprStmt =
    do {- C++ comments are filtered by exprStmt -}
       l_expr <- try exprStmt
       semi
       return (EXPR l_expr)

pForStmt :: GenParser Char ParserState Stmt
pForStmt = 
    do reserved "for"
       l_exprs <- parens $ semiSep1 (commaSep pForExpr)
       l_stmt <- pStatement
       return (FOR l_exprs l_stmt)

pDoStmt :: GenParser Char ParserState Stmt
pDoStmt =
    do reserved "do"
       symbol "{"
       l_stmts <- manyTill pStatement (try $ symbol "}")
       -- l_stmt <- pStatement
       reserved "while"
       l_expr <- exprStmt
       semi
       return (DO l_expr l_stmts)

pWhileStmt :: GenParser Char ParserState Stmt
pWhileStmt =
    do reserved "while"
       l_boolExpr <- exprStmt
       symbol "{"
       l_stmts <- manyTill pStatement (try $ symbol "}")
       return (WHILE l_boolExpr l_stmts)

pParenStmt :: GenParser Char ParserState Stmt
pParenStmt =
    do symbol "{"
       l_stmts <- manyTill pStatement (try $ symbol "}")
       return (BRACES l_stmts)

pTypeDecl :: GenParser Char ParserState ([PName], PType)
pTypeDecl = do l_qualifiers <- many cppQualifier
               l_type <- pType
               return (l_qualifiers ++ [typeName l_type], l_type)

pTypeDecl_r :: GenParser Char ParserState ([PName], PType)
pTypeDecl_r = do l_type <- pType
                 l_qualifiers <- many cppQualifier
                 return ([typeName l_type] ++ l_qualifiers, l_type)

pDeclLocalStmt :: GenParser Char ParserState Stmt
pDeclLocalStmt =
    do (l_qualifiers, l_type) <- try pTypeDecl <|> try pTypeDecl_r
       l_exprs <- commaSep1 exprStmt 
       semi
       return (DEXPR l_qualifiers l_type l_exprs)

pIfStmt :: GenParser Char ParserState Stmt
pIfStmt =
    do reserved "if"
       l_boolExpr <- exprStmt
       l_trueBranch <- pStatement
       l_falseBranch <- option NOP pElseBranch
       return (IF l_boolExpr l_trueBranch l_falseBranch)

pSwitchStmt :: GenParser Char ParserState Stmt
pSwitchStmt =
    do reserved "switch"
       l_boolExpr <- exprStmt
       l_cases <- braces (many pCase)
       return (SWITCH l_boolExpr l_cases)
 
pParams :: GenParser Char ParserState (RegionT, Bool)
pParams = do l_regionT <- pRegionParam
             option "" comma
             l_obase <- option False (pObaseParam)
             return (l_regionT, l_obase)

pRegionParam :: GenParser Char ParserState RegionT
pRegionParam = do reserved "Periodic"
                  return Periodic
           <|> do reserved "Non-periodic"
                  return Nonperiodic
           <?> "Periodic/Non-periodic"

pObaseParam :: GenParser Char ParserState Bool
pObaseParam = do reserved "Obase"
                 return True
            <|>  return False
                  
pForExpr :: GenParser Char ParserState Stmt
pForExpr =  do (l_qualifiers, l_type) <- try pTypeDecl <|> try pTypeDecl_r
               l_exprs <- commaSep1 exprStmt 
               return (DEXPR l_qualifiers l_type l_exprs)
        <|> do l_expr <- exprStmt
               return (EXPR l_expr)
        <|> do whiteSpace
               return NOP
        <?> "For Expression"

pCase :: GenParser Char ParserState Stmt
pCase = do reserved "case"
           l_value <- natural >>= return . fromInteger
           colon
           l_stmts <- manyTill pStatement $ reserved "break"
           semi
           return (CASE l_value (l_stmts ++ [BREAK]))
    <|> do reserved "default"
           colon
           l_stmts <- manyTill pStatement $ reserved "break"
           semi
           return (DEFAULT (l_stmts ++ [BREAK]))
    <?> "Cases"

pElseBranch :: GenParser Char ParserState Stmt
pElseBranch = do reserved "else"
                 l_stmt <- pStatement
                 return l_stmt

{-
pSimpleType :: GenParser Char ParserState PType
pSimpleType = 
        do reserved "double" 
           return (PDouble)
    <|> do reserved "int"
           return (PInt)
    <|> do reserved "float"
           return (PFloat)
    <|> do reserved "bool"
           return (PBool)
-}

pType :: GenParser Char ParserState PType
pType = do reserved "double" 
           return PType{typeName = "double", basicType = PDouble}
    <|> do reserved "int"
           return PType{typeName = "int", basicType = PInt}
    <|> do reserved "float"
           return PType{typeName = "float", basicType = PFloat}
    <|> do reserved "bool"
           return PType{typeName = "bool", basicType = PBool}
    <|> do reserved "char"
           return PType{typeName = "char", basicType = PChar}
    <|> do reserved "short"
           return PType{typeName = "short", basicType = PShort}
    <|> do reserved "long"
           return PType{typeName = "long", basicType = PLong}
    <|> do reserved "unsigned"
           return PType{typeName = "unsigned", basicType = PUnsigned}
    <|> do reserved "signed"
           return PType{typeName = "signed", basicType = PSigned}
    <|> do reserved "void"
           return PType{typeName = "void", basicType = PVoid}
    <|> do l_type <- identifier
           l_qualifiers <- many cppQualifier
           return PType{typeName = l_type ++ (intercalate " " l_qualifiers), basicType = PUserType}
    <|> do l_qualifiers <- many cppQualifier
           l_type <- identifier 
           return PType{typeName = (intercalate " " l_qualifiers) ++ l_type, basicType = PUserType}

eol :: GenParser Char ParserState String
eol = do string "\n" 
         whiteSpace
         return "\n"
  <|> do string "\r\n" 
         whiteSpace
         return "\r\n"
  <|> do string "\n\r" 
         whiteSpace
         return "\n\r"
  <?> "eol"

-- Expression Parser for Dim in Declaration --
exprDeclDim :: GenParser Char ParserState Int
exprDeclDim = buildExpressionParser tableDeclDim termDeclDim
   <?> "exprDeclDim"

tableDeclDim = [[Prefix (reservedOp "-" >> return negate)],
         [op "*" (*) AssocLeft, op "/" div AssocLeft],
         [op "+" (+) AssocLeft, op "-" (-) AssocLeft]]
         where op s fop assoc = Infix (do {reservedOp s; return fop} <?> "operator") assoc

termDeclDim :: GenParser Char ParserState Int
termDeclDim = 
       try (parens exprDeclDim)
{-
   <|> do literal_dim <- try (identifier)
          l_state <- getState
          case Map.lookup literal_dim $ pMacro l_state of
              -- FIXME: If it's nothing, then something must be wrong
              Nothing -> return (0)
              Just num_dim -> return (num_dim)
-}
   <|> do num_dim <- try (natural)
          return (fromInteger num_dim)
   <?> "termDeclDim"

-- Expression Parser for Dim in Statements --
exprStmtDim :: GenParser Char ParserState DimExpr
exprStmtDim = buildExpressionParser tableStmtDim termStmtDim <?> "ExprStmtDim"

tableStmtDim = [
         [bop "*" "*" AssocLeft, bop "/" "/" AssocLeft],
         [bop "+" "+" AssocLeft, bop "-" "-" AssocLeft],
         [bop "==" "==" AssocLeft, bop "!=" "!=" AssocLeft]]
         where bop str fop assoc = Infix ((reservedOp str >> return (DimDuo fop)) <?> "operator") assoc

termStmtDim :: GenParser Char ParserState DimExpr
termStmtDim = do e <- try (parens exprStmtDim)
                 return (DimParen e)
          <|> do literal_dim <- try (identifier)
                 return (DimVAR literal_dim)
{-
                 l_state <- getState
                 -- check whether it's an effective Range name
                 case Map.lookup literal_dim $ pMacro l_state of
                     Nothing -> return (DimVAR literal_dim)
                     Just l_dim -> return (DimINT l_dim)
-}
          <|> do num_dim <- try (natural)
                 return (DimINT $ fromInteger num_dim)
          <?> "TermStmtDim"

-- Expression Parser for Statements --
exprStmt :: GenParser Char ParserState Expr
exprStmt = buildExpressionParser tableStmt termStmt
   <?> "Expression Statement"

tableStmt = [[Postfix (reservedOp "++" >> return (PostUno "++")),
              Postfix (reservedOp "--" >> return (PostUno "--"))],
            [Prefix (reservedOp "!" >> return (Uno "!")),
              Prefix (reservedOp "~" >> return (Uno "~")),
              Prefix (reservedOp "++" >> return (Uno "++")),
              Prefix (reservedOp "--" >> return (Uno "--")),
              Prefix (reservedOp "-" >> return (Uno "-")),
              Prefix (reservedOp "+" >> return (Uno "+")), --Unary Plus
              Prefix (reservedOp "*" >> return (Uno "*")), --Dereference
              Prefix (reservedOp "&" >> return (Uno "&"))  --Address of
             ],
         [op "*" "*" AssocLeft, op "/" "/" AssocLeft,
          op "%" "%" AssocLeft],
         [op "+" "+" AssocLeft, op "-" "-" AssocLeft],
         [op ">>" ">>" AssocLeft, op "<<" "<<" AssocLeft],
         [op ">" ">" AssocLeft, op "<" "<" AssocLeft,
          op ">=" ">=" AssocLeft, op "<=" "<=" AssocLeft],
         [op "==" "==" AssocLeft, op "!=" "!=" AssocLeft],
         [op "&" "&" AssocLeft], --bitwise and
         [op "^" "^" AssocLeft], --bitwise xor
         [op "|" "|" AssocLeft], --bitwise inclusive or
         [op "&&" "&&" AssocLeft], --logical and
         [op "||" "||" AssocLeft], --logical or
         [op ":" ":" AssocLeft],
         [op "?" "?" AssocLeft],
         [Infix (reservedOp "=" >> return (Duo "=")) AssocLeft,
          Infix (reservedOp "/=" >> return (Duo "/=")) AssocLeft,
          Infix (reservedOp "*=" >> return (Duo "*=")) AssocLeft,
          Infix (reservedOp "+=" >> return (Duo "+=")) AssocLeft,
          Infix (reservedOp "-=" >> return (Duo "-=")) AssocLeft,
          Infix (reservedOp "%=" >> return (Duo "%=")) AssocLeft,
          Infix (reservedOp "&=" >> return (Duo "&=")) AssocLeft,
          Infix (reservedOp "|=" >> return (Duo "|=")) AssocLeft,
          Infix (reservedOp "^=" >> return (Duo "^=")) AssocLeft,
          Infix (reservedOp ">>=" >> return (Duo ">>=")) AssocLeft,
          Infix (reservedOp "<<=" >> return (Duo "<<=")) AssocLeft
          ]]
         where op s fop assoc = Infix (do {reservedOp s; return (Duo fop)} <?> "operator") assoc

termStmt :: GenParser Char ParserState Expr
termStmt =  do try pArrayOfStructTermStmt
        <|> do try ppArrayOfStructTermStmt
--        <|> do try pPArrayOfStructTermStmt
        <|> do l_expr <- try (parens exprStmt) 
               return (PARENS l_expr)
        <|> do l_num <- try (number)
               case l_num of
                  Left n -> return (INT $ fromInteger n)
                  Right n -> return (FLOAT n)
        <|> do reserved "true" 
               return (BOOL "true") 
        <|> do reserved "false"
               return (BOOL "false")
        <|> do try pParenTermStmt
        <|> do try pBracketTermStmt
        <|> do try pBExprTermStmt
        -- pArrayOfStructTermStmt has to be before pPlainVarTermStmt
        -- because pPlainVarTermStmt just scan a plain identifier
        -- so, it's a conflict with pArrayOfStructTermStmt
        <|> do try pPlainVarTermStmt
        <?> "term statement"

pParenTermStmt :: GenParser Char ParserState Expr
pParenTermStmt =
    do l_qualifiers <- try (many cppQualifier)
       l_var <- try identifier
       l_dims <- parens (commaSep1 exprStmtDim)
       return (PVAR (concat l_qualifiers) l_var l_dims)

pBracketTermStmt :: GenParser Char ParserState Expr
pBracketTermStmt =
    do l_var <- try identifier
       l_dim <- brackets exprStmtDim
       return (BVAR l_var l_dim)

pBExprTermStmt :: GenParser Char ParserState Expr
pBExprTermStmt =
    do l_var <- try identifier
       l_expr <- brackets exprStmt
       return (BExprVAR l_var l_expr)

pPlainVarTermStmt :: GenParser Char ParserState Expr
pPlainVarTermStmt =
    do l_qualifiers <- try (many cppQualifier)
       l_var <- try identifier
       return (VAR (concat l_qualifiers) l_var)

pArrayOfStructTermStmt :: GenParser Char ParserState Expr
pArrayOfStructTermStmt =
    do l_type <- try pType
       l_pTerm <- parens pParenTermStmt
       l_connector <- symbol "." <|> symbol "->"
       l_field <- identifier
       return (SVAR l_type l_pTerm l_connector l_field)

ppArrayOfStructTermStmt :: GenParser Char ParserState Expr
ppArrayOfStructTermStmt =
    do let opt_type = PType{basicType = PUserType, typeName = ""}
       l_pTerm <- pParenTermStmt 
       l_connector <- symbol "." <|> symbol "->"
       l_field <- identifier
       return (SVAR opt_type l_pTerm l_connector l_field)

pPArrayOfStructTermStmt :: GenParser Char ParserState Expr
pPArrayOfStructTermStmt =
    do l_type <- parens pType
       l_pTerm <- parens pParenTermStmt
       l_connector <- symbol "." <|> symbol "->"
       l_field <- identifier
       return (PSVAR l_type l_pTerm l_connector l_field)

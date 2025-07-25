%{
#include "ast.h"
#include "parser.yy.h"
#include <iostream>
#include <memory>

int yylex(YYSTYPE *yylval, YYLTYPE *yylloc, void *yyscanner);

void yyerror(YYLTYPE *locp, void *yyscanner, const char* s) {
    std::cerr << "Parser Error at line " << locp->first_line << " column " << locp->first_column << ": " << s << std::endl;
}

using namespace ast;
%}

// request a pure (reentrant) parser
%define api.pure full
// enable location in error handler
%locations
// enable verbose syntax error message
%define parse.error verbose
// define the extra parameter for passing the lexer state
%param {void *yyscanner}

// keywords
%token SHOW TABLES CREATE TABLE DROP DESC INSERT INTO VALUES DELETE FROM ASC ORDER BY
WHERE UPDATE SET SELECT INT CHAR FLOAT DATETIME INDEX AND JOIN EXIT HELP TXN_BEGIN TXN_COMMIT TXN_ABORT TXN_ROLLBACK ORDER_BY ENABLE_NESTLOOP ENABLE_SORTMERGE
COUNT MAX MIN SUM AS GROUP HAVING IN STATIC_CHECKPOINT LOAD OUTPUT_FILE ON OFF

// non-keywords
%token LEQ NEQ GEQ T_EOF

// type-specific tokens
%token <sv_str> FILE_PATH IDENTIFIER VALUE_STRING
%token <sv_int> VALUE_INT
%token <sv_float> VALUE_FLOAT
%token <sv_bool> VALUE_BOOL

// specify types for non-terminal symbol
%type <sv_node> stmt dbStmt ddl dml txnStmt setStmt
%type <sv_field> field
%type <sv_fields> fieldList
%type <sv_type_len> type
%type <sv_comp_op> op
%type <sv_expr> expr
%type <sv_val> value
%type <sv_vals> valueList
%type <sv_str> tbName colName alias asClause
%type <sv_strs> tableList colNameList
%type <sv_col> col
%type <sv_cols> colList group_by_clause
%type <sv_bound> select_item
%type <sv_bounds> select_list
%type <sv_havings> having_clause having_clauses
%type <sv_set_clause> setClause
%type <sv_set_clauses> setClauses
%type <sv_cond> condition
%type <sv_conds> whereClause optWhereClause
%type <sv_orderby>  order_clause opt_order_clause
%type <sv_orderby_dir> opt_asc_desc
%type <sv_setKnobType> set_knob_type

%%
start:
        stmt ';'
    {
        parse_tree = std::move($1);
        YYACCEPT;
    }
    |   SET set_knob_type OFF
    {
        parse_tree = std::make_shared<SetStmt>($2, false);
        YYACCEPT;
    }
    |   SET set_knob_type ON
    {
        parse_tree = std::make_shared<SetStmt>($2, true);
        YYACCEPT;
    }
    |   HELP
    {
        parse_tree = std::make_shared<Help>();
        YYACCEPT;
    }
    |   EXIT
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
    |   T_EOF
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
    ;

stmt:
        dbStmt
    |   ddl
    |   dml
    |   txnStmt
    |   setStmt
    ;

txnStmt:
        TXN_BEGIN
    {
        $$ = std::make_shared<TxnBegin>();
    }
    |   TXN_COMMIT
    {
        $$ = std::make_shared<TxnCommit>();
    }
    |   TXN_ABORT
    {
        $$ = std::make_shared<TxnAbort>();
    }
    | TXN_ROLLBACK
    {
        $$ = std::make_shared<TxnRollback>();
    }
    ;

dbStmt:
        SHOW TABLES
    {
        $$ = std::make_shared<ShowTables>();
    }
    |   SHOW INDEX FROM tbName
    {
        $$ = std::make_shared<ShowIndexs>($4);
    }
    ;

setStmt:
        SET set_knob_type '=' VALUE_BOOL
    {
        $$ = std::make_shared<SetStmt>($2, $4);
    }
    ;

ddl:
        CREATE TABLE tbName '(' fieldList ')'
    {
        $$ = std::make_shared<CreateTable>($3, $5);
    }
    |   CREATE STATIC_CHECKPOINT
    {
        $$ = std::make_shared<CreateStaticCheckpoint>();
    }
    |   DROP TABLE tbName
    {
        $$ = std::make_shared<DropTable>($3);
    }
    |   DESC tbName
    {
        $$ = std::make_shared<DescTable>($2);
    }
    |   CREATE INDEX tbName '(' colNameList ')'
    {
        $$ = std::make_shared<CreateIndex>($3, $5);
    }
    |   DROP INDEX tbName '(' colNameList ')'
    {
        $$ = std::make_shared<DropIndex>($3, $5);
    }
    ;

dml:
        LOAD FILE_PATH INTO tbName
    {
        $$ = std::make_shared<LoadStmt>($2, $4);
    }
    |   INSERT INTO tbName VALUES '(' valueList ')'
    {
        $$ = std::make_shared<InsertStmt>($3, $6);
    }
    |   DELETE FROM tbName optWhereClause
    {
        $$ = std::make_shared<DeleteStmt>($3, $4);
    }
    |   UPDATE tbName SET setClauses optWhereClause
    {
        $$ = std::make_shared<UpdateStmt>($2, $4, $5);
    }
    |   SELECT select_list FROM tableList optWhereClause group_by_clause having_clauses opt_order_clause
    {
        $$ = std::static_pointer_cast<Expr>(std::make_shared<SelectStmt>($2, $4, $5, $6, $7, $8));
    }
    ;

fieldList:
        field
    {
        $$.emplace_back(std::move($1));
    }
    |   fieldList ',' field
    {
        $$.emplace_back(std::move($3));
    }
    ;

colNameList:
        colName
    {
        $$.emplace_back(std::move($1));
    }
    |   colNameList ',' colName
    {
        $$.emplace_back(std::move($3));
    }
    ;

field:
        colName type
    {
        $$ = std::make_shared<ColDef>($1, $2);
    }
    ;

type:
        INT
    {
        $$ = std::make_shared<TypeLen>(SV_TYPE_INT, sizeof(int));
    }
    |   CHAR '(' VALUE_INT ')'
    {
        $$ = std::make_shared<TypeLen>(SV_TYPE_STRING, $3);
    }
    |   FLOAT
    {
        $$ = std::make_shared<TypeLen>(SV_TYPE_FLOAT, sizeof(float));
    }
    |   DATETIME
    {
        $$ = std::make_shared<TypeLen>(SV_TYPE_STRING, 19);
    }
    ;

valueList:
        value
    {
        $$.emplace_back(std::move($1));
    }
    |   valueList ',' value
    {
        $$.emplace_back(std::move($3));
    }
    ;

value:
        VALUE_INT
    {
        $$ = std::make_shared<IntLit>($1);
    }
    |   VALUE_FLOAT
    {
        $$ = std::make_shared<FloatLit>($1);
    }
    |   VALUE_STRING
    {
        $$ = std::make_shared<StringLit>($1);
    }
    |   VALUE_BOOL
    {
        $$ = std::make_shared<BoolLit>($1);
    }
    ;

condition:
        col op expr
    {
        $$ = std::make_shared<BinaryExpr>($1, $2, $3);
    }
    |   col op '(' valueList ')'
    {
        $$ = std::make_shared<BinaryExpr>($1, $2, $4);
    }
    ;

optWhereClause:
        /* epsilon */
    {
        /* ignore */
    }
    |   WHERE whereClause
    {
        $$ = std::move($2);
    }
    ;

whereClause:
        condition 
    {
        $$.emplace_back(std::move($1));
    }
    |   whereClause AND condition
    {
        $$.emplace_back(std::move($3));
    }
    ;

col:
        tbName '.' colName
    {
        $$ = std::make_shared<Col>(std::move($1), std::move($3));
    }
    |   colName
    {
        $$ = std::make_shared<Col>("", std::move($1));
    }
    ;

colList:
        col
    {
        $$.emplace_back(std::move($1));
    }
    |   colList ',' col
    {
        $$.emplace_back(std::move($3));
    }
    ;

op:
        '='
    {
        $$ = SV_OP_EQ;
    }
    |   '<'
    {
        $$ = SV_OP_LT;
    }
    |   '>'
    {
        $$ = SV_OP_GT;
    }
    |   NEQ
    {
        $$ = SV_OP_NE;
    }
    |   LEQ
    {
        $$ = SV_OP_LE;
    }
    |   GEQ
    {
        $$ = SV_OP_GE;
    }
    |   IN
    {
        $$ = SV_OP_IN;
    }
    ;

expr:
        value
    {
        $$ = std::static_pointer_cast<Expr>($1);
    }
    |   col
    {
        $$ = std::static_pointer_cast<Expr>($1);
    }
    |   '(' SELECT select_list FROM tableList optWhereClause group_by_clause having_clauses opt_order_clause ')'
    {
        $$ = std::make_shared<SelectStmt>($3, $5, $6, $7, $8, $9);
    }
    ;

setClauses:
        setClause
    {
        $$.emplace_back(std::move($1));
    }
    |   setClauses ',' setClause
    {
        $$.emplace_back(std::move($3));
    }
    ;

setClause:
        colName '=' value
    {
        $$ = std::make_shared<SetClause>($1, $3);
    }
    |   colName '=' colName value
    {
        $$ = std::make_shared<SetClause>($1, $4, true);
    }
    ;

asClause:
        AS alias
    {
        $$ = std::move($2);
    }
    |
    {
        $$ = "";
    }
    ;

select_item:
        col asClause
    {
        $$ = std::make_shared<BoundExpr>(std::move($1), AGG_COL, std::move($2));
    }
    |   COUNT '(' '*' ')' asClause
    {
        $$ = std::make_shared<BoundExpr>(std::make_shared<Col>("", ""), AGG_COUNT, std::move($5));
    }
    |   COUNT '(' col ')' asClause
    {
        $$ = std::make_shared<BoundExpr>(std::move($3), AGG_COUNT, std::move($5));
    }
    |   MAX '(' col ')' asClause
    {
        $$ = std::make_shared<BoundExpr>(std::move($3), AGG_MAX, std::move($5));
    }
    |   MIN '(' col ')' asClause
    {
        $$ = std::make_shared<BoundExpr>(std::move($3), AGG_MIN, std::move($5));
    }
    |   SUM '(' col ')' asClause
    {
        $$ = std::make_shared<BoundExpr>(std::move($3), AGG_SUM, std::move($5));
    }
    ;

select_list:
        '*'
    {
        $$ = {};
    }
    |   select_item
    {
        $$.emplace_back(std::move($1));
    }
    |   select_list ',' select_item
    {
        $$.emplace_back(std::move($3));
    }
    ;

tableList:
        tbName
    {
        $$.emplace_back(std::move($1));
    }
    |   tableList ',' tbName
    {
        $$.emplace_back(std::move($3));
    }
    |   tableList JOIN tbName
    {
        $$.emplace_back(std::move($3));
    }
    ;

opt_order_clause:
        ORDER BY order_clause
    { 
        $$ = std::move($3);
    }
    |   /* epsilon */
    {
        /* ignore */
    }
    ;

order_clause:
        col opt_asc_desc
    { 
        $$ = std::make_shared<OrderBy>($1, $2);
    }
    ;

opt_asc_desc:
        ASC
    {
        $$ = OrderBy_ASC;
    }
    |   DESC
    {
        $$ = OrderBy_DESC;
    }
    |
    {
        $$ = OrderBy_DEFAULT;
    }
    ;

group_by_clause:
        GROUP BY colList
    {
        $$ = std::move($3);
    }
    |   /* epsilon */
    {
        /* ignore */
    }
    ;

having_clause:
        select_item op expr
    {
        $$.emplace_back(std::make_shared<HavingExpr>($1, $2, $3));
    }
    |   having_clause AND select_item op expr
    {
        $$.emplace_back(std::make_shared<HavingExpr>($3, $4, $5));
    }
    |   /* epsilon */
    {
        /* ignore */
    }
    ;

having_clauses:
        HAVING having_clause
    {
        $$ = std::move($2);
    }
    |   /* epsilon */
    {
        /* ignore */
    }
    ;

set_knob_type:
        ENABLE_NESTLOOP
    {
        $$ = EnableNestLoop;
    }
    |   ENABLE_SORTMERGE
    {
        $$ = EnableSortMerge;
    }
    |   OUTPUT_FILE
    {
        $$ = EnableOutputFile;
    }
    ;

tbName: IDENTIFIER;

colName: IDENTIFIER;

alias: IDENTIFIER;
%%

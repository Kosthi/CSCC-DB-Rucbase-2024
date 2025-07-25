/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL
v2. You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include <cassert>
#include <iostream>
#include <map>

#include "ast.h"

namespace ast {
class TreePrinter {
 public:
  static void print(const std::shared_ptr<TreeNode>& node) {
    print_node(node, 0);
  }

 private:
  static std::string offset2string(int offset) {
    return std::string(offset, ' ');
  }

  template <typename T>
  static void print_val(const T& val, int offset) {
    std::cout << offset2string(offset) << val << '\n';
  }

  template <typename T>
  static void print_val_list(const std::vector<T>& vals, int offset) {
    std::cout << offset2string(offset) << "LIST\n";
    offset += 2;
    for (auto& val : vals) {
      print_val(val, offset);
    }
  }

  static std::string type2str(SvType type) {
    static std::map<SvType, std::string> m{
        {SV_TYPE_INT, "INT"},
        {SV_TYPE_FLOAT, "FLOAT"},
        {SV_TYPE_STRING, "STRING"},
    };
    return m.at(type);
  }

  static std::string aggType2str(AggType type) {
    static std::map<AggType, std::string> m{{AGG_COL, "AGG_COL"},
                                            {AGG_COUNT, "AGG_COUNT"},
                                            {AGG_MAX, "AGG_MAX"},
                                            {AGG_MIN, "AGG_MIN"},
                                            {AGG_SUM, "AGG_SUM"}};
    return m.at(type);
  }

  static std::string op2str(SvCompOp op) {
    static std::map<SvCompOp, std::string> m{
        {SV_OP_EQ, "=="}, {SV_OP_NE, "!="}, {SV_OP_LT, "<"}, {SV_OP_GT, ">"},
        {SV_OP_LE, "<="}, {SV_OP_GE, ">="}, {SV_OP_IN, "IN"}};
    return m.at(op);
  }

  static std::string knobType2str(SetKnobType type) {
    static std::map<SetKnobType, std::string> m{
        {EnableNestLoop, "EnableNestLoop"},
        {EnableSortMerge, "EnableSortMerge"},
        {EnableOutputFile, "EnableOutputFile"}};
    return m.at(type);
  }

  static std::string boolType2str(bool type) { return type ? "true" : "false"; }

  template <typename T>
  static void print_node_list(std::vector<T> nodes, int offset) {
    std::cout << offset2string(offset);
    offset += 2;
    std::cout << "LIST\n";
    for (auto& node : nodes) {
      print_node(node, offset);
    }
  }

  static void print_node(const std::shared_ptr<TreeNode>& node, int offset) {
    std::cout << offset2string(offset);
    offset += 2;
    if (node == nullptr) {
      std::cout << "NULLPTR\n";
    } else if (auto x = std::dynamic_pointer_cast<Help>(node)) {
      std::cout << "HELP\n";
    } else if (auto x = std::dynamic_pointer_cast<ShowTables>(node)) {
      std::cout << "SHOW_TABLES\n";
    } else if (auto x = std::dynamic_pointer_cast<ShowIndexs>(node)) {
      std::cout << "SHOW_INDEXS\n";
      print_val(x->tab_name, offset);
    } else if (auto x = std::dynamic_pointer_cast<CreateTable>(node)) {
      std::cout << "CREATE_TABLE\n";
      print_val(x->tab_name, offset);
      print_node_list(x->fields, offset);
    } else if (auto x = std::dynamic_pointer_cast<DropTable>(node)) {
      std::cout << "DROP_TABLE\n";
      print_val(x->tab_name, offset);
    } else if (auto x = std::dynamic_pointer_cast<DescTable>(node)) {
      std::cout << "DESC_TABLE\n";
      print_val(x->tab_name, offset);
    } else if (auto x = std::dynamic_pointer_cast<CreateIndex>(node)) {
      std::cout << "CREATE_INDEX\n";
      print_val(x->tab_name, offset);
      // print_val(x->col_name, offset);
      for (auto& col_name : x->col_names) print_val(col_name, offset);
    } else if (auto x = std::dynamic_pointer_cast<DropIndex>(node)) {
      std::cout << "DROP_INDEX\n";
      print_val(x->tab_name, offset);
      // print_val(x->col_name, offset);
      for (auto& col_name : x->col_names) print_val(col_name, offset);
    } else if (auto x = std::dynamic_pointer_cast<ColDef>(node)) {
      std::cout << "COL_DEF\n";
      print_val(x->col_name, offset);
      print_node(x->type_len, offset);
    } else if (auto x = std::dynamic_pointer_cast<Col>(node)) {
      std::cout << "COL\n";
      print_val(x->tab_name, offset);
      print_val(x->col_name, offset);
    } else if (auto x = std::dynamic_pointer_cast<TypeLen>(node)) {
      std::cout << "TYPE_LEN\n";
      print_val(type2str(x->type), offset);
      print_val(x->len, offset);
    } else if (auto x = std::dynamic_pointer_cast<IntLit>(node)) {
      std::cout << "INT_LIT\n";
      print_val(x->val, offset);
    } else if (auto x = std::dynamic_pointer_cast<FloatLit>(node)) {
      std::cout << "FLOAT_LIT\n";
      print_val(x->val, offset);
    } else if (auto x = std::dynamic_pointer_cast<StringLit>(node)) {
      std::cout << "STRING_LIT\n";
      print_val(x->val, offset);
    } else if (auto x = std::dynamic_pointer_cast<SetClause>(node)) {
      std::cout << "SET_CLAUSE\n";
      print_val(x->col_name, offset);
      print_node(x->val, offset);
      print_val(boolType2str(x->is_incr), offset);
    } else if (auto x = std::dynamic_pointer_cast<BinaryExpr>(node)) {
      std::cout << "BINARY_EXPR\n";
      print_node(x->lhs, offset);
      print_val(op2str(x->op), offset);
      if (x->rhs) {
        print_node(x->rhs, offset);
      } else {
        print_node_list(x->rhs_list, offset);
      }
    } else if (auto x = std::dynamic_pointer_cast<BoundExpr>(node)) {
      std::cout << "BOUND_EXPR\n";
      print_val(aggType2str(x->type), offset);
      print_node(x->col, offset);
      print_val(x->alias, offset);
    } else if (auto x = std::dynamic_pointer_cast<HavingExpr>(node)) {
      std::cout << "HAVING_EXPR\n";
      print_node(x->lhs, offset);
      print_val(op2str(x->op), offset);
      print_node(x->rhs, offset);
    } else if (auto x = std::dynamic_pointer_cast<LoadStmt>(node)) {
      std::cout << "LOAD\n";
      print_val(x->file_name, offset);
      print_val(x->table_name, offset);
    } else if (auto x = std::dynamic_pointer_cast<SetStmt>(node)) {
      std::cout << "SET_KNOB\n";
      print_val(knobType2str(x->set_knob_type_), offset);
      print_val(boolType2str(x->bool_val_), offset);
    } else if (auto x = std::dynamic_pointer_cast<InsertStmt>(node)) {
      std::cout << "INSERT\n";
      print_val(x->tab_name, offset);
      print_node_list(x->vals, offset);
    } else if (auto x = std::dynamic_pointer_cast<DeleteStmt>(node)) {
      std::cout << "DELETE\n";
      print_val(x->tab_name, offset);
      print_node_list(x->conds, offset);
    } else if (auto x = std::dynamic_pointer_cast<UpdateStmt>(node)) {
      std::cout << "UPDATE\n";
      print_val(x->tab_name, offset);
      print_node_list(x->set_clauses, offset);
      print_node_list(x->conds, offset);
    } else if (auto x = std::dynamic_pointer_cast<SelectStmt>(node)) {
      std::cout << "SELECT\n";
      print_node_list(x->select_list, offset);
      print_val_list(x->tabs, offset);
      print_node_list(x->conds, offset);      // where
      print_node_list(x->group_bys, offset);  // group by
      print_node_list(x->havings, offset);    // having
    } else if (auto x = std::dynamic_pointer_cast<TxnBegin>(node)) {
      std::cout << "BEGIN\n";
    } else if (auto x = std::dynamic_pointer_cast<TxnCommit>(node)) {
      std::cout << "COMMIT\n";
    } else if (auto x = std::dynamic_pointer_cast<TxnAbort>(node)) {
      std::cout << "ABORT\n";
    } else if (auto x = std::dynamic_pointer_cast<TxnRollback>(node)) {
      std::cout << "ROLLBACK\n";
    } else {
      assert(0);
    }
  }
};
}  // namespace ast

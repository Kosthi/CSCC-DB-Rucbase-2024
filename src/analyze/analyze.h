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
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "common/common.h"
#include "parser/parser.h"
#include "system/sm.h"

class Query {
 public:
  std::shared_ptr<ast::TreeNode> parse;
  // TODO jointree

  // where 条件
  std::vector<Condition> conds;

  // group by
  std::vector<TabCol> group_bys;
  // having 条件
  std::vector<Condition> havings;

  // TODO sortby 条件，支持多列
  TabCol sort_bys;

  // 投影列
  std::vector<TabCol> cols;
  // 聚合类型 没有聚合类型为AGG_COL
  std::vector<AggType> agg_types;
  // as 别名
  std::vector<std::string> alias;

  // 表名
  std::vector<std::string> tables;
  // update 的set 值
  std::vector<SetClause> set_clauses;
  // insert 的values值
  std::vector<Value> values;

  // min asc true
  // max asc false
  bool asc = true;
  // limit > 0 available
  int limit = -1;

  Query() = default;
};

class Analyze {
 private:
  SmManager* sm_manager_;

 public:
  explicit Analyze(SmManager* sm_manager) : sm_manager_(sm_manager) {}

  ~Analyze() = default;

  std::shared_ptr<Query> do_analyze(std::shared_ptr<ast::TreeNode> root);

 private:
  // void check_column(const std::vector<ColMeta> &all_cols, TabCol &target);

  // void get_all_cols(const std::vector<std::string> &tab_names,
  // std::vector<ColMeta> &all_cols);

  void check_column(const std::vector<std::string>& tables, TabCol& target);

  void get_clause(std::vector<std::shared_ptr<ast::BinaryExpr> >& sv_conds,
                  std::vector<Condition>& conds);

  static void get_having_clause(
      const std::vector<std::shared_ptr<ast::HavingExpr> >& sv_conds,
      std::vector<Condition>& conds);

  void check_clause(std::vector<Condition>& conds,
                    const std::vector<std::string>& tables);

  static Value convert_sv_value(const std::shared_ptr<ast::Value>& sv_val);

  static CompOp convert_sv_comp_op(ast::SvCompOp& op);
};

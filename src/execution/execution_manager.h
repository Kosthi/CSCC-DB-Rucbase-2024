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
#include "common/context.h"
#include "execution_defs.h"
#include "executor_abstract.h"
#include "optimizer/plan.h"
#include "optimizer/planner.h"
#include "record/rm.h"
#include "system/sm.h"
#include "transaction/transaction_manager.h"

class Planner;

class QlManager {
 private:
  SmManager* sm_manager_;
  TransactionManager* txn_mgr_;
  Planner* planner_;

 public:
  QlManager(SmManager* sm_manager, TransactionManager* txn_mgr,
            Planner* planner)
      : sm_manager_(sm_manager), txn_mgr_(txn_mgr), planner_(planner) {}

  void run_mutli_query(std::shared_ptr<Plan>& plan, Context* context);

  void run_cmd_utility(std::shared_ptr<Plan>& plan, const txn_id_t* txn_id,
                       Context* context) const;

  void select_from(std::unique_ptr<AbstractExecutor>& executorTreeRoot,
                   std::vector<TabCol>& sel_cols, Context* context);

  void select_fast_count_star(int count, std::string& sel_col,
                              Context* context);

  static void run_dml(std::unique_ptr<AbstractExecutor>& exec);
};

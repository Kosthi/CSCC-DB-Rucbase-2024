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

#include <cerrno>
#include <string>

#include "common/common.h"
#include "execution/executor_abstract.h"
#include "execution/executor_aggregate.h"
#include "execution/executor_delete.h"
#include "execution/executor_index_scan.h"
#include "execution/executor_insert.h"
#include "execution/executor_nestedloop_join.h"
#include "execution/executor_projection.h"
#include "execution/executor_seq_scan.h"
#include "execution/executor_sort.h"
#include "execution/executor_sortmerge_join.h"
#include "execution/executor_update.h"
#include "optimizer/plan.h"

typedef enum portalTag {
  PORTAL_Invalid_Query = 0,
  PORTAL_ONE_SELECT,
  PORTAL_DML_WITHOUT_SELECT,
  PORTAL_MULTI_QUERY,
  PORTAL_CMD_UTILITY
} portalTag;

struct PortalStmt {
  portalTag tag;

  std::vector<TabCol> sel_cols;
  std::unique_ptr<AbstractExecutor> root;
  std::shared_ptr<Plan> plan;

  PortalStmt(portalTag tag_, std::vector<TabCol> sel_cols_,
             std::unique_ptr<AbstractExecutor> root_,
             std::shared_ptr<Plan> plan_)
      : tag(tag_),
        sel_cols(std::move(sel_cols_)),
        root(std::move(root_)),
        plan(std::move(plan_)) {}
};

class Portal {
 private:
  SmManager* sm_manager_;

 public:
  explicit Portal(SmManager* sm_manager) : sm_manager_(sm_manager) {}

  ~Portal() = default;

  // 将查询执行计划转换成对应的算子树
  std::shared_ptr<PortalStmt> start(std::shared_ptr<Plan> plan,
                                    Context* context) {
    if (auto x = std::dynamic_pointer_cast<StaticCheckpointPlan>(plan)) {
      return std::make_shared<PortalStmt>(
          PORTAL_CMD_UTILITY, std::vector<TabCol>(),
          std::unique_ptr<AbstractExecutor>(), plan);
    }
    // 这里可以将select进行拆分，例如：一个select，带有return的select等
    if (auto x = std::dynamic_pointer_cast<OtherPlan>(plan)) {
      return std::make_shared<PortalStmt>(
          PORTAL_CMD_UTILITY, std::vector<TabCol>(),
          std::unique_ptr<AbstractExecutor>(), plan);
    }
    if (auto x = std::dynamic_pointer_cast<SetKnobPlan>(plan)) {
      return std::make_shared<PortalStmt>(
          PORTAL_CMD_UTILITY, std::vector<TabCol>(),
          std::unique_ptr<AbstractExecutor>(), plan);
    }
    if (auto x = std::dynamic_pointer_cast<DDLPlan>(plan)) {
      return std::make_shared<PortalStmt>(
          PORTAL_MULTI_QUERY, std::vector<TabCol>(),
          std::unique_ptr<AbstractExecutor>(), plan);
    }
    if (auto x = std::dynamic_pointer_cast<DMLPlan>(plan)) {
      switch (x->tag) {
        case T_select: {
          std::shared_ptr<ProjectionPlan> p =
              std::dynamic_pointer_cast<ProjectionPlan>(x->subplan_);
          std::unique_ptr<AbstractExecutor> root =
              convert_plan_executor(p, context);
          std::vector<TabCol> show_cols = p->sel_cols_;
          for (std::size_t i = 0; i < p->alias_.size(); ++i) {
            if (!p->alias_[i].empty()) {
              show_cols[i].col_name = std::move(p->alias_[i]);
            }
          }
          return std::make_shared<PortalStmt>(PORTAL_ONE_SELECT,
                                              std::move(show_cols),
                                              std::move(root), std::move(plan));
        }
        case T_Update: {
          std::unique_ptr<AbstractExecutor> scan =
              convert_plan_executor(x->subplan_, context, true);
          std::vector<Rid> rids;
          for (scan->beginTuple(); !scan->is_end(); scan->nextTuple()) {
            rids.emplace_back(scan->rid());
          }
          // TODO update 算子优化，如果更新值不涉及索引键，则不需要维护索引
          bool is_set_index_key = true;
          if (scan->getType() == "IndexScanExecutor") {
            auto* index_scan = dynamic_cast<IndexScanExecutor*>(scan.get());
            auto& map = index_scan->get_index_meta().cols_map;
            // 查找 set 值是否是 key
            int i = 0;
            for (; i < x->set_clauses_.size(); ++i) {
              if (map.count(x->set_clauses_[i].lhs.col_name) > 0) {
                break;
              }
            }
            if (i == x->set_clauses_.size()) {
              is_set_index_key = false;
            }
          }
          std::unique_ptr<AbstractExecutor> root =
              std::make_unique<UpdateExecutor>(
                  sm_manager_, std::move(x->tab_name_),
                  std::move(x->set_clauses_), std::move(rids), is_set_index_key,
                  scan->getType() == "IndexScanExecutor", context);
          return std::make_shared<PortalStmt>(PORTAL_DML_WITHOUT_SELECT,
                                              std::vector<TabCol>(),
                                              std::move(root), plan);
        }
        case T_Delete: {
          std::unique_ptr<AbstractExecutor> scan =
              convert_plan_executor(x->subplan_, context, true);
          std::vector<Rid> rids;
          for (scan->beginTuple(); !scan->is_end(); scan->nextTuple()) {
            rids.emplace_back(scan->rid());
          }
          std::unique_ptr<AbstractExecutor> root =
              std::make_unique<DeleteExecutor>(
                  sm_manager_, std::move(x->tab_name_), std::move(rids),
                  context, scan->getType() == "IndexScanExecutor");
          return std::make_shared<PortalStmt>(PORTAL_DML_WITHOUT_SELECT,
                                              std::vector<TabCol>(),
                                              std::move(root), plan);
        }
        case T_Insert: {
          std::unique_ptr<AbstractExecutor> root =
              std::make_unique<InsertExecutor>(sm_manager_,
                                               std::move(x->tab_name_),
                                               std::move(x->values_), context);
          return std::make_shared<PortalStmt>(PORTAL_DML_WITHOUT_SELECT,
                                              std::vector<TabCol>(),
                                              std::move(root), std::move(plan));
        }
        default:
          throw InternalError("Unexpected field type");
      }
    }
    throw InternalError("Unexpected field type");
  }

  // 遍历算子树并执行算子生成执行结果
  static void run(std::shared_ptr<PortalStmt>& portal, QlManager* ql,
                  txn_id_t* txn_id, Context* context) {
    switch (portal->tag) {
      case PORTAL_ONE_SELECT: {
        ql->select_from(portal->root, portal->sel_cols, context);
        break;
      }
      case PORTAL_DML_WITHOUT_SELECT: {
        QlManager::run_dml(portal->root);
        break;
      }
      case PORTAL_MULTI_QUERY: {
        ql->run_mutli_query(portal->plan, context);
        break;
      }
      case PORTAL_CMD_UTILITY: {
        ql->run_cmd_utility(portal->plan, txn_id, context);
        break;
      }
      default: {
        throw InternalError("Unexpected field type");
      }
    }
  }

  // 清空资源
  static void drop() {}

  std::unique_ptr<AbstractExecutor> convert_plan_executor(
      const std::shared_ptr<Plan>& plan, Context* context,
      bool gap_mode = false) {
    if (auto x = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
      return std::make_unique<ProjectionExecutor>(
          convert_plan_executor(x->subplan_, context), std::move(x->sel_cols_),
          x->limit_);
    }
    if (auto x = std::dynamic_pointer_cast<ScanPlan>(plan)) {
      // TODO 为每个子查询生成算子
      // for (auto &cond: x->conds_) {
      //     if (cond.is_sub_query && cond.sub_query_plan != nullptr) {
      //         assert(0);
      //         cond.prev = convert_plan_executor(cond.sub_query_plan,
      //         context);
      //         // 在算子执行之前就处理异常情况，否则会错误输出表头
      //         // 子查询为空抛错处理
      //         cond.prev->beginTuple();
      //         if (cond.prev->is_end()) {
      //             throw InternalError("Empty sub query!");
      //         }
      //         // 比较运算符的子查询，只能有一个元素
      //         if (cond.op != OP_IN) {
      //             cond.prev->nextTuple();
      //             if (!cond.prev->is_end()) {
      //                 throw InternalError("Subquery returns more than 1
      //                 row!");
      //             }
      //         }
      //     }
      // }
      if (x->tag == T_SeqScan) {
        return std::make_unique<SeqScanExecutor>(
            sm_manager_, std::move(x->tab_name_), std::move(x->conds_), context,
            gap_mode);
      }
      return std::make_unique<IndexScanExecutor>(
          sm_manager_, std::move(x->tab_name_), std::move(x->conds_),
          std::move(x->index_col_names_), context, gap_mode, x->asc_);
    }
    if (auto x = std::dynamic_pointer_cast<AggregatePlan>(plan)) {
      return std::make_unique<AggregateExecutor>(
          convert_plan_executor(x->subplan_, context), std::move(x->sel_cols_),
          std::move(x->agg_types_), std::move(x->group_bys_),
          std::move(x->havings_), context);
    }
    if (auto x = std::dynamic_pointer_cast<JoinPlan>(plan)) {
      std::unique_ptr<AbstractExecutor> left =
          convert_plan_executor(x->left_, context);
      std::unique_ptr<AbstractExecutor> right =
          convert_plan_executor(x->right_, context);

      // 使用 lambda 表达式并行化 left 和 right 执行器的创建
      // auto left_future = std::async(std::launch::async, [&] {
      //     return convert_plan_executor(x->left_, context);
      // });
      // auto right_future = std::async(std::launch::async, [&] {
      //     return convert_plan_executor(x->right_, context);
      // });

      // 等待并获取结果
      // std::unique_ptr<AbstractExecutor> left = left_future.get();
      // std::unique_ptr<AbstractExecutor> right = right_future.get();

      if (x->tag == T_NestLoop) {
        return std::make_unique<NestedLoopJoinExecutor>(
            std::move(left), std::move(right), std::move(x->conds_));
      }
      return std::make_unique<SortMergeJoinExecutor>(
          std::move(left), std::move(right), std::move(x->conds_));
    }
    if (auto x = std::dynamic_pointer_cast<SortPlan>(plan)) {
      return std::make_unique<SortExecutor>(
          convert_plan_executor(x->subplan_, context), std::move(x->sel_col_),
          x->is_desc_);
    }
    return nullptr;
  }
};

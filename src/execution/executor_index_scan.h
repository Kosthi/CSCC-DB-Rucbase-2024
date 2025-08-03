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

#define MAX_RECORD_SIZE 81920

#include <float.h>
#include <limits.h>

#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "predicate_manager.h"
#include "system/sm.h"

static std::map<CompOp, CompOp> swap_op = {
    {OP_EQ, OP_EQ}, {OP_NE, OP_NE}, {OP_LT, OP_GT},
    {OP_GT, OP_LT}, {OP_LE, OP_GE}, {OP_GE, OP_LE},
};

class IndexScanExecutor : public AbstractExecutor {
 private:
  SmManager* sm_manager_;
  std::string tab_name_;          // 表名称
  std::vector<Condition> conds_;  // 扫描条件
  std::vector<std::string>
      index_col_names_;  // index scan涉及到的索引包含的字段
  TabMeta& tab_;         // 表的元数据
  RmFileHandle* fh_;     // 表的数据文件句柄
  // std::vector<ColMeta> cols_; // 没必要，通过 tab 获取需要读取的字段
  std::vector<std::vector<ColMeta>::iterator> cond_cols_;  // 谓词需要读取的字段
  size_t len_;             // 选取出来的一条记录的长度
  IndexMeta& index_meta_;  // index scan涉及到的索引元数据
  Rid rid_;
  std::unique_ptr<IxScan> scan_;
  std::unique_ptr<RmRecord> rm_record_;
  std::deque<std::unique_ptr<RmRecord> > records_;
  // 实际二进制文件名
  std::string filename_;
  // 排序结果实际读取文件，二进制
  std::fstream outfile_;
  bool is_end_{false};
  size_t id_;
  bool mergesort_;
  constexpr static int int_min_ = INT32_MIN;
  constexpr static int int_max_ = INT32_MAX;
  constexpr static float float_min_ = FLT_MIN;
  constexpr static float float_max_ = FLT_MAX;

  // 满足索引算子多次调用，非归并
  bool already_begin_{false};
  Iid lower_;
  Iid upper_;
  IxIndexHandle* ih_;

  // false 为共享间隙锁，true 为互斥间隙锁
  bool gap_mode_;
  PredicateManager predicate_manager_;
  bool index_clean_{false};  // 记录是否需要过一遍索引谓词

  bool is_empty_btree_{false};

  bool scan_index_{false};

  // 顺序扫描还是逆序
  bool asc_{true};

  static std::size_t generateID() {
    static size_t current_id = 0;
    return ++current_id;
  }

  // for index scan
  void write_sorted_results() {
    // 以期望格式写入 sorted_results.txt
    auto out_expected_file =
        std::fstream("sorted_results.txt", std::ios::out | std::ios::app);
    outfile_.open(filename_, std::ios::out);

    // 打印右表头
    out_expected_file << "|";
    for (auto& col_meta : tab_.cols) {
      out_expected_file << " " << col_meta.name << " |";
    }
    out_expected_file << "\n";

    // 右表先开始
    for (; !scan_->is_end(); scan_->next()) {
      // 打印记录
      rm_record_ = fh_->get_record(scan_->rid(), context_);
      // 写入文件中
      outfile_.write(rm_record_->data, rm_record_->size);

      std::vector<std::string> columns;
      columns.reserve(tab_.cols.size());

      for (auto& col : tab_.cols) {
        std::string col_str;
        char* rec_buf = rm_record_->data + col.offset;
        if (col.type == TYPE_INT) {
          col_str = std::to_string(*(int*)rec_buf);
        } else if (col.type == TYPE_FLOAT) {
          col_str = std::to_string(*(float*)rec_buf);
        } else if (col.type == TYPE_STRING) {
          col_str = std::string((char*)rec_buf, col.len);
          col_str.resize(strlen(col_str.c_str()));
        }
        columns.emplace_back(col_str);
      }

      // 打印记录
      out_expected_file << "|";
      for (auto& col : columns) {
        out_expected_file << " " << col << " |";
      }
      out_expected_file << "\n";
    }

    outfile_.close();
    out_expected_file.close();
  }

 public:
  IndexScanExecutor(SmManager* sm_manager, std::string tab_name,
                    std::vector<Condition> conds,
                    std::vector<std::string> index_col_names, Context* context,
                    bool gap_mode = false, bool asc = true)
      : sm_manager_(sm_manager),
        tab_name_(std::move(tab_name)),
        conds_(std::move(conds)),
        index_col_names_(std::move(index_col_names)),
        gap_mode_(gap_mode),
        asc_(asc),
        tab_(sm_manager_->db_.get_table(tab_name_)),
        index_meta_(tab_.get_index_meta(index_col_names_)) {
    context_ = context;
    // index_no_ = index_no;
    // index_meta_ = tab_.get_index_meta(index_col_names_);
    fh_ = sm_manager_->fhs_[tab_name_].get();
    // cols_ = tab_.cols;
    len_ = tab_.cols.back().offset + tab_.cols.back().len;

    // 分析阶段处理
    // for (auto &cond: conds_) {
    //     if (cond.lhs_col.tab_name != tab_name_) {
    //         // lhs is on other table, now rhs must be on this table
    //         assert(!cond.is_rhs_val && cond.rhs_col.tab_name == tab_name_);
    //         // swap lhs and rhs
    //         std::swap(cond.lhs_col, cond.rhs_col);
    //         cond.op = swap_op.at(cond.op);
    //     }
    // }
    id_ = generateID();
    filename_ = "sorted_results_index_" + std::to_string(id_) + ".txt";
    mergesort_ = !conds_[0].is_rhs_val && conds_.size() == 1;

    // where w_id=:w_id and c_w_id=w_id and c_d_id=:d_id and c_id=:c_id;
    // 全表走索引 加 merge
    if (!conds_[0].is_rhs_val && conds_.size() > 1) {
      scan_index_ = true;
      // 擦掉第一个
      conds_.erase(conds_.begin());
    }

    // 有点耗时
    const auto index_name = tab_.get_index_name(index_col_names_);
    ih_ = sm_manager_->ihs_[index_name].get();

    predicate_manager_ = PredicateManager(index_meta_);

    // TODO 支持更多谓词的解析 > >
    for (auto it = conds_.begin(); it != conds_.end();) {
      if (it->lhs_col.tab_name == tab_name_ && it->op != OP_NE &&
          it->is_rhs_val) {
        if (predicate_manager_.addPredicate(it->lhs_col.col_name, *it)) {
          it = conds_.erase(it);
          continue;
        }
      }
      ++it;
    }

    cond_cols_.reserve(conds_.size());

    for (auto& cond : conds_) {
      // 存对应的 col_meta 迭代器
      cond_cols_.emplace_back(tab_.get_col(cond.lhs_col.col_name));
    }

    // S 锁
    // if (context_ != nullptr) {
    //     context_->lock_mgr_->lock_shared_on_table(context_->txn_,
    //     fh_->GetFd());
    // }
    auto gap = Gap(predicate_manager_.getIndexConds());
    if (gap_mode_) {
      context_->lock_mgr_->lock_exclusive_on_gap(context_->txn_, index_meta_,
                                                 gap, fh_->GetFd());
    } else {
      context_->lock_mgr_->lock_shared_on_gap(context_->txn_, index_meta_, gap,
                                              fh_->GetFd());
    }
  }

  void beginTuple() override {
    // 已经解决空树问题
    // if (is_empty_btree_) {
    //     is_end_ = true;
    //     return;
    // }
    if (already_begin_ && (!mergesort_ || scan_index_)) {
      is_end_ = false;
      scan_ =
          std::make_unique<IxScan>(ih_, lower_, upper_, sm_manager_->get_bpm());
      while (!scan_->is_end()) {
        // 不回表
        // 全是等号或最后一个谓词是比较，不需要再扫索引
        if (index_clean_ ||
            predicate_manager_.cmpIndexConds(scan_->get_key())) {
          // 回表，查不在索引里的谓词
          rid_ = scan_->rid();
          rm_record_ = fh_->get_record(rid_, context_);
          if (conds_.empty() || cmp_conds(rm_record_.get(), conds_)) {
            return;
          }
        }
        scan_->next();
      }
      is_end_ = true;
      return;
    }

    // 全表扫索引
    if (scan_index_) {
      scan_ =
          std::make_unique<IxScan>(ih_, lower_, upper_, sm_manager_->get_bpm());
      already_begin_ = true;

      // where a > 1, c < 1
      while (!scan_->is_end()) {
        // 不回表
        // 全是等号或最后一个谓词是比较，不需要再扫索引
        if (index_clean_ ||
            predicate_manager_.cmpIndexConds(scan_->get_key())) {
          // 回表，查不在索引里的谓词
          rid_ = scan_->rid();
          rm_record_ = fh_->get_record(rid_, context_);
          if (conds_.empty() || cmp_conds(rm_record_.get(), conds_)) {
            return;
          }
        }
        scan_->next();
      }
      is_end_ = true;
      return;
    }

    // 如果 '列 op 列'，走 sortmerge
    // TODO 行多外部排序慢，但是为了通过归并连接测试
    if (mergesort_) {
      // 适用嵌套连接走索引的情况
      // struct stat st;
      // 当前算子排序过了，已经存在排序结果文件
      if (already_begin_) {
        // 可能是算子结束进来的，也有可能是到一部分重新开始了
        if (!is_end_) {
          outfile_.close();
        }

        // 记得一定要重置，否则读到文件尾后算子会一直保持结束状态
        is_end_ = false;

        outfile_.open(filename_, std::ios::in);
        if (!outfile_.is_open()) {
          std::stringstream s;
          s << "Failed to open file: " << std::strerror(errno);
          throw InternalError(s.str());
        }

        // 缓存记录
        do {
          rm_record_ = std::make_unique<RmRecord>(len_);
          outfile_.read(rm_record_->data, rm_record_->size);
          if (outfile_.gcount() == 0) {
            break;
          }
          records_.emplace_back(std::move(rm_record_));
        } while (records_.size() < MAX_RECORD_SIZE);

        if (!records_.empty()) {
          rm_record_ = std::move(records_.front());
          records_.pop_front();
        } else {
          is_end_ = true;
          rm_record_ = nullptr;
          outfile_.close();
        }
        return;
      }

      is_end_ = false;
      scan_ =
          std::make_unique<IxScan>(ih_, lower_, upper_, sm_manager_->get_bpm());
      write_sorted_results();

      already_begin_ = true;

      outfile_.open(filename_, std::ios::in);

      // 缓存记录
      do {
        rm_record_ = std::make_unique<RmRecord>(len_);
        outfile_.read(rm_record_->data, rm_record_->size);
        if (outfile_.gcount() == 0) {
          break;
        }
        records_.emplace_back(std::move(rm_record_));
      } while (records_.size() < MAX_RECORD_SIZE);

      if (!records_.empty()) {
        rm_record_ = std::move(records_.front());
        records_.pop_front();
      } else {
        is_end_ = true;
        rm_record_ = nullptr;
        outfile_.close();
      }
      return;
    }

    char* left_key = new char[index_meta_.col_tot_len];
    char* right_key = new char[index_meta_.col_tot_len];

    // 找出下界 [
    auto last_left_tuple = predicate_manager_.getLeftLastTuple(left_key);
    auto last_right_tuple = predicate_manager_.getRightLastTuple(right_key);

    auto last_left_op = std::get<0>(last_left_tuple);
    auto last_right_op = std::get<0>(last_right_tuple);

    auto last_idx = std::get<1>(last_left_tuple);  // 第一个范围查询位置

    index_clean_ = last_idx == index_meta_.col_num;

    // assert(last_idx == std::get<1>(last_right_tuple));

    // index(a, b, c) where a = 1, b = 1 等值查询
    if (last_left_op == OP_INVALID && last_right_op == OP_INVALID) {
      set_remaining_all_min(last_idx, left_key);
      lower_ = ih_->lower_bound(left_key);
      // 设置成最大值，需要根据类型设置，不能直接0xff，int 为 -1
      set_remaining_all_max(last_idx, right_key);
      upper_ = ih_->upper_bound(right_key);
    } else {
      switch (last_left_op) {
        // 交给查上界的处理
        case OP_INVALID: {
          lower_ = ih_->leaf_begin();
          break;
        }
        // 全部都是等值查询
        case OP_EQ: {
          // where p_id = 0, name = 'bztyhnmj';
          // 设置成最小值，需要根据类型设置，不能直接0，int 会有负值
          // assert(last_idx == index_meta_.cols.size());
          // set_remaining_all_min(offset, last_idx, key);
          lower_ = ih_->lower_bound(left_key);
          // 设置成最大值，需要根据类型设置，不能直接0xff，int 为 -1
          // set_remaining_all_max(offset, last_idx, key);
          upper_ = ih_->upper_bound(right_key);

          // 1.最简单的情况，唯一索引等值锁定存在的数据：加行锁index(a, b, c) a
          // = 1, b = 1, c = 1 加间隙锁 1.1 a = 1 1.2 a = 1, b = 1

          break;
        }
        case OP_GT: {
          // where name > 'bztyhnmj';                      last_idx = 0, + 1
          // where name > 'bztyhnmj' and id = 1;           last_idx = 0, + 1
          // where p_id = 3, name > 'bztyhnmj';            last_idx = 1, + 1
          // where p_id = 3, name > 'bztyhnmj' and id = 1; last_idx = 1, + 1
          // 设置成最大值，需要根据类型设置，不能直接0xff，int 为 -1
          // TODO may error?
          set_remaining_all_max(last_idx + 1, left_key);
          lower_ = ih_->upper_bound(left_key);
          // 如果前面有等号需要重新更新上下界
          // where w_id = 0 and name > 'bztyhnmj';
          if (last_right_op == OP_INVALID) {
            if (last_idx > 0) {
              // 把后面的范围查询清 0 找上限
              // 设置成最大值，需要根据类型设置，不能直接0xff，int 为 -1
              set_remaining_all_max(last_idx, left_key);
              upper_ = ih_->upper_bound(left_key);
            }
          }
          break;
        }
        case OP_GE: {
          // where name >= 'bztyhnmj';                      last_idx = 0, + 1
          // where name >= 'bztyhnmj' and id = 1;           last_idx = 0, + 1
          // where p_id = 3, name >= 'bztyhnmj';            last_idx = 1, + 1
          // where p_id = 3, name >= 'bztyhnmj' and id = 1; last_idx = 1, + 1
          // 如果前面有等号需要重新更新上下界
          // 设置成最小值，需要根据类型设置，不能直接0，int 会有负值
          set_remaining_all_min(last_idx + 1, left_key);
          lower_ = ih_->lower_bound(left_key);
          // where w_id = 0 and name >= 'bztyhnmj';
          if (last_right_op == OP_INVALID) {
            if (last_idx > 0) {
              // 把后面的范围查询置最大 找上限
              // 设置成最大值，需要根据类型设置，不能直接0xff，int 为 -1
              set_remaining_all_max(last_idx, left_key);
              upper_ = ih_->upper_bound(left_key);
            }
          }
          break;
        }
        default:
          throw InternalError("Unexpected op type！");
      }

      // 找出上界 )
      switch (last_right_op) {
        // 在前面查下界时已经确定
        case OP_INVALID: {
          upper_ = ih_->leaf_end();
          break;
        }
        // 全部都是等值查询
        case OP_EQ: {
          // 优化：如果这里是 EQ，前面找下界已经确定过了，不需要再走一遍
          // where p_id = 0, name = 'bztyhnmj';
          // 设置成最小值，需要根据类型设置，不能直接0，int 会有负值
          // set_remaining_all_min(offset, last_idx, right_key);
          // assert(last_idx == index_meta_.cols.size());

          // lower_ = ih_->lower_bound(right_key);
          // 设置成最大值，需要根据类型设置，不能直接0xff，int 为 -1
          // set_remaining_all_max(offset, last_idx, right_key);
          // upper_ = ih_->upper_bound(right_key);

          // 1.最简单的情况，唯一索引等值锁定存在的数据：加行锁index(a, b, c) a
          // = 1, b = 1, c = 1 加间隙锁 1.1 a = 1 1.2 a = 1, b = 1

          break;
        }
        case OP_LT: {
          // where name < 'bztyhnmj';                      last_idx = 0, + 1
          // where name < 'bztyhnmj' and id = 1;           last_idx = 0, + 1
          // where p_id = 3, name < 'bztyhnmj';            last_idx = 1, + 1
          // where p_id = 3, name < 'bztyhnmj' and id = 1; last_idx = 1, + 1
          // 设置成最小值，需要根据类型设置，不能直接0，int 会有负值
          set_remaining_all_min(last_idx + 1, right_key);
          upper_ = ih_->lower_bound(right_key);
          // 如果前面有等号需要重新更新上下界
          // where w_id = 0 and name < 'bztyhnmj';
          if (last_left_op == OP_INVALID) {
            if (last_idx > 0) {
              // 把后面的范围查询清 0 找下限
              // 设置成最小值，需要根据类型设置，不能直接0，int 会有负值
              set_remaining_all_min(last_idx, right_key);
              lower_ = ih_->lower_bound(right_key);
            }
          }
          break;
        }
        case OP_LE: {
          // where name <= 'bztyhnmj';                      last_idx = 0, + 1
          // where name <= 'bztyhnmj' and id = 1;           last_idx = 0, + 1
          // where p_id = 3, name <= 'bztyhnmj';            last_idx = 1, + 1
          // where p_id = 3, name <= 'bztyhnmj' and id = 1; last_idx = 1, + 1
          // 设置成最大值，需要根据类型设置，不能直接0xff，int 为 -1
          // TODO error?
          set_remaining_all_max(last_idx + 1, right_key);
          upper_ = ih_->upper_bound(right_key);
          // 如果前面有等号需要重新更新上下界
          // where w_id = 0 and name <= 'bztyhnmj';
          if (last_left_op == OP_INVALID) {
            if (last_idx > 0) {
              // 把后面的范围查询清 0 找下限
              // 设置成最小值，需要根据类型设置，不能直接0，int 会有负值
              set_remaining_all_min(last_idx, right_key);
              lower_ = ih_->lower_bound(right_key);
            }
          }
          break;
        }
        default:
          throw InternalError("Unexpected op type！");
      }
    }

    // 释放内存
    delete[] left_key;
    delete[] right_key;

    scan_ =
        std::make_unique<IxScan>(ih_, lower_, upper_, sm_manager_->get_bpm());
    already_begin_ = true;

    // max 找最后一个记录的情况
    if (!scan_->is_end() && !asc_) {
      rid_ = scan_->prev_rid(upper_);
      rm_record_ = fh_->get_record(rid_, context_);
      // std::cout << "fast max" << std::endl;
      return;
    }

    // where a > 1, c < 1
    while (!scan_->is_end()) {
      // 不回表
      // 全是等号或最后一个谓词是比较，不需要再扫索引
      // TODO 这里尽量把比较次数减少，如果选出来的记录都落在索引上了，就可以跳过
      if (index_clean_ || predicate_manager_.cmpIndexConds(scan_->get_key())) {
        // 回表，查不在索引里的谓词
        rid_ = scan_->rid();
        rm_record_ = fh_->get_record(rid_, context_);
        if (conds_.empty() || cmp_conds(rm_record_.get(), conds_)) {
          return;
        }
      }
      scan_->next();
    }
    is_end_ = true;
  }

  void nextTuple() override {
    // 这里只对逆向扫描的 max 作了 return，如果是正向扫描 min 还会调用一次
    // nextTuple，并没有完全做到 limit 1 所以还是在投影算子的 nextTuple
    // 写个判断比较好
    if (!asc_) {
      return;
    }
    if (!records_.empty()) {
      rm_record_ = std::move(records_.front());
      records_.pop_front();
      return;
    }
    // sortmerge
    if (mergesort_) {
      // 缓存记录
      do {
        rm_record_ = std::make_unique<RmRecord>(len_);
        outfile_.read(rm_record_->data, rm_record_->size);
        if (outfile_.gcount() == 0) {
          break;
        }
        records_.emplace_back(std::move(rm_record_));
      } while (records_.size() < MAX_RECORD_SIZE);

      if (!records_.empty()) {
        rm_record_ = std::move(records_.front());
        records_.pop_front();
      } else {
        is_end_ = true;
        rm_record_ = nullptr;
        outfile_.close();
      }
      return;
    }
    if (scan_->is_end()) {
      is_end_ = true;
      return;
    }
    for (scan_->next(); !scan_->is_end(); scan_->next()) {
      // 不回表
      // 全是等号或最后一个谓词是比较，不需要再扫索引
      // TODO 待优化
      if (index_clean_ || predicate_manager_.cmpIndexConds(scan_->get_key())) {
        // 回表，查不在索引里的谓词
        rid_ = scan_->rid();
        rm_record_ = fh_->get_record(rid_, context_);
        if (conds_.empty() || cmp_conds(rm_record_.get(), conds_)) {
          return;
        }
      }
    }
    is_end_ = true;
  }

  std::unique_ptr<RmRecord> Next() override { return std::move(rm_record_); }

  Rid& rid() override { return rid_; }

  bool is_end() const { return is_end_; }

  const std::vector<ColMeta>& cols() const override { return tab_.cols; }

  size_t tupleLen() const override { return len_; }

  // 根据不同的列值类型设置不同的最大值
  // int   类型范围 int_min_ ~ int_max_
  // float 类型范围 float_min_ ~ float_max_
  // char  类型范围 0 ~ 255
  void set_remaining_all_max(int last_idx, char*& key) {
    // 设置成最大值
    for (size_t i = last_idx; i < index_meta_.cols.size(); ++i) {
      auto& [index_offset, col] = index_meta_.cols[i];
      if (col.type == TYPE_INT) {
        memcpy(key + index_offset, &int_max_, sizeof(int));
      } else if (col.type == TYPE_FLOAT) {
        memcpy(key + index_offset, &float_max_, sizeof(float));
      } else if (col.type == TYPE_STRING) {
        memset(key + index_offset, 0xff, col.len);
      } else {
        throw InternalError("Unexpected data type！");
      }
    }
  }

  // 根据不同的列值类型设置不同的最小值
  // int   类型范围 int_min_ ~ int_max_
  // float 类型范围 float_min_ ~ float_max_
  // char  类型范围 0 ~ 255
  void set_remaining_all_min(int last_idx, char*& key) {
    for (size_t i = last_idx; i < index_meta_.cols.size(); ++i) {
      auto& [index_offset, col] = index_meta_.cols[i];
      if (col.type == TYPE_INT) {
        memcpy(key + index_offset, &int_min_, sizeof(int));
      } else if (col.type == TYPE_FLOAT) {
        memcpy(key + index_offset, &float_min_, sizeof(float));
      } else if (col.type == TYPE_STRING) {
        memset(key + index_offset, 0, col.len);
      } else {
        throw InternalError("Unexpected data type！");
      }
    }
  }

  // 判断是否满足单个谓词条件
  bool cmp_cond(int i, const RmRecord* rec, const Condition& cond) {
    const auto& lhs_col_meta = cond_cols_[i];
    const char* lhs_data = rec->data + lhs_col_meta->offset;
    char* rhs_data;
    ColType rhs_type;
    // 全局record 防止作为临时变量离开作用域自动析构，char* 指针指向错误的地址
    std::unique_ptr<RmRecord> record;
    // 提取左值与右值的数据和类型
    // 常值
    if (cond.is_rhs_val) {
      rhs_type = cond.rhs_val.type;
      rhs_data = cond.rhs_val.raw->data;
    } else if (cond.is_sub_query) {
      // 查的是值列表
      if (!cond.rhs_value_list.empty()) {
        // in 谓词
        if (cond.op == OP_IN) {
          // 前面已经强制转换和检查类型匹配过了，这里不需要
          for (auto& value : cond.rhs_value_list) {
            rhs_data = value.raw->data;
            if (compare(lhs_data, rhs_data, lhs_col_meta->len, value.type) ==
                0) {
              return true;
            }
          }
          return false;
        }
        // 比较谓词
        assert(cond.rhs_value_list.size() == 1);
        auto& value = cond.rhs_value_list[0];
        int cmp =
            compare(lhs_data, value.raw->data, lhs_col_meta->len, value.type);
        switch (cond.op) {
          case OP_EQ:
            return cmp == 0;
          case OP_NE:
            return cmp != 0;
          case OP_LT:
            return cmp < 0;
          case OP_GT:
            return cmp > 0;
          case OP_LE:
            return cmp <= 0;
          case OP_GE:
            return cmp >= 0;
          default:
            throw InternalError("Unexpected op type！");
        }
      }
      // 查的是子算子，在算子生成阶段已经检查了合法性
      cond.prev->beginTuple();

      // where id <= (select count(*) from grade);
      // where id <= (select id from grade where id = 1); // 返回单列单行
      ColMeta rhs_col_meta;
      if (cond.sub_query->agg_types[0] == AGG_COUNT &&
          cond.sub_query->cols[0].tab_name.empty() &&
          cond.sub_query->cols[0].col_name.empty()) {
        rhs_type = TYPE_INT;
      } else {
        // ！子查询右边的列值类型应该由子算子决定
        rhs_col_meta = cond.prev->cols()[0];
        // where id > (select count(id) from grade);
        if (cond.sub_query->agg_types[0] == AGG_COUNT) {
          rhs_type = TYPE_INT;
        } else {
          rhs_type = rhs_col_meta.type;
        }
      }

      // 处理 in 谓词，扫描算子直到找到一个完全相等的
      if (cond.op == OP_IN) {
        for (; !cond.prev->is_end(); cond.prev->nextTuple()) {
          record = cond.prev->Next();
          rhs_data = record->data;
          if (lhs_col_meta->type == TYPE_FLOAT && rhs_type == TYPE_INT) {
            rhs_type = TYPE_FLOAT;
            const float a = *reinterpret_cast<const int*>(rhs_data);
            memcpy(rhs_data, &a, sizeof(float));
          }
          if (compare(lhs_data, rhs_data, lhs_col_meta->len, rhs_type) == 0) {
            return true;
          }
        }
        return false;
      }

      // 聚合或列值只能有一行
      record = cond.prev->Next();
      rhs_data = record->data;
      if (lhs_col_meta->type == TYPE_FLOAT && rhs_type == TYPE_INT) {
        rhs_type = TYPE_FLOAT;
        const float a = *reinterpret_cast<const int*>(rhs_data);
        memcpy(rhs_data, &a, sizeof(float));
      }
    } else {
      // 列值
      assert(0);
      // 没有 id1 = id2 的情况
      // const auto &rhs_col_meta = get_col(rec_cols, cond.rhs_col);
      // rhs_type = rhs_col_meta->type;
      // rhs_data = rec->data + rhs_col_meta->offset;
    }

    if (lhs_col_meta->type != rhs_type) {
      throw IncompatibleTypeError(coltype2str(lhs_col_meta->type),
                                  coltype2str(rhs_type));
    }

    int cmp = compare(lhs_data, rhs_data, lhs_col_meta->len, rhs_type);
    switch (cond.op) {
      case OP_EQ:
        return cmp == 0;
      case OP_NE:
        return cmp != 0;
      case OP_LT:
        return cmp < 0;
      case OP_GT:
        return cmp > 0;
      case OP_LE:
        return cmp <= 0;
      case OP_GE:
        return cmp >= 0;
      default:
        throw InternalError("Unexpected op type！");
    }
  }

  bool cmp_conds(const RmRecord* rec, const std::vector<Condition>& conds) {
    for (int i = 0; i < conds.size(); ++i) {
      if (!cmp_cond(i, rec, conds[i])) {
        return false;
      }
    }
    return true;
  }

  IndexMeta& get_index_meta() { return index_meta_; }

  std::string getType() { return "IndexScanExecutor"; }
};

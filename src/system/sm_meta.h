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

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "errors.h"

/* 字段元数据 */
struct ColMeta {
  std::string tab_name;  // 字段所属表名称
  std::string name;      // 字段名称
  ColType type;          // 字段类型
  int len;               // 字段长度
  int offset;            // 字段位于记录中的偏移量

  friend std::ostream& operator<<(std::ostream& os, const ColMeta& col) {
    // ColMeta中有各个基本类型的变量，然后调用重载的这些变量的操作符<<（具体实现逻辑在defs.h）
    return os << col.tab_name << ' ' << col.name << ' ' << col.type << ' '
              << col.len << ' ' << col.offset;
  }

  friend std::istream& operator>>(std::istream& is, ColMeta& col) {
    return is >> col.tab_name >> col.name >> col.type >> col.len >> col.offset;
  }

  // 重载相等运算符
  bool operator==(const ColMeta& other) const {
    return tab_name == other.tab_name && name == other.name;
  }
};

/* 索引元数据 */
struct IndexMeta {
  std::string tab_name;  // 索引所属表名称
  int col_tot_len = 0;   // 索引字段长度总和
  int col_num = 0;       // 索引字段数量
  std::vector<std::pair<int, ColMeta> >
      cols;  // 索引包含的字段 在索引中的偏移量 -> 列元信息
  std::unordered_map<std::string, std::pair<int, ColMeta> > cols_map;

  IndexMeta() = default;

  IndexMeta(std::string&& tab_name_, int col_tot_len_, int col_num_,
            std::vector<ColMeta>&& cols_)
      : tab_name(std::move(tab_name_)),
        col_tot_len(col_tot_len_),
        col_num(col_num_) {
    int offset = 0;
    for (auto& col : cols_) {
      cols.emplace_back(offset, std::move(col));
      offset += cols.back().second.len;
    }
    for (auto it = cols.begin(); it != cols.end(); ++it) {
      cols_map.emplace(it->second.name, *it);
    }
  }

  friend std::ostream& operator<<(std::ostream& os, const IndexMeta& index) {
    os << index.tab_name << " " << index.col_tot_len << " " << index.col_num;
    for (auto& [_, col] : index.cols) {
      std::ignore = _;
      os << "\n" << col;
    }
    return os;
  }

  friend std::istream& operator>>(std::istream& is, IndexMeta& index) {
    is >> index.tab_name >> index.col_tot_len >> index.col_num;
    int offset = 0;
    int len = 0;
    for (int i = 0; i < index.col_num; ++i) {
      ColMeta col;
      is >> col;
      len = col.len;
      index.cols.emplace_back(offset, std::move(col));
      offset += len;
    }
    for (auto it = index.cols.begin(); it != index.cols.end(); ++it) {
      index.cols_map.emplace(it->second.name, *it);
    }
    return is;
  }

  // 重载相等运算符
  bool operator==(const IndexMeta& other) const {
    return tab_name == other.tab_name && col_tot_len == other.col_tot_len &&
           col_num == other.col_num && cols == other.cols;
  }
};

// 定义自定义哈希函数
namespace std {
template <>
struct hash<IndexMeta> {
  std::size_t operator()(const IndexMeta& index) const {
    std::size_t hash = std::hash<std::string>{}(index.tab_name);
    hash ^= std::hash<int>{}(index.col_tot_len) << 1;
    hash ^= std::hash<int>{}(index.col_num) << 2;
    for (const auto& [_, col] : index.cols) {
      std::ignore = _;
      hash ^= std::hash<std::string>{}(col.name) << 3;
    }
    return hash;
  }
};

template <>
struct hash<std::vector<std::string> > {
  std::size_t operator()(const std::vector<std::string>& vec) const {
    std::size_t hash = 0;
    std::hash<std::string> hasher;
    for (const auto& str : vec) {
      hash ^= hasher(str) + 0x9e3779b9 + (hash << 6) +
              (hash >> 2);  // Improved hash
    }
    return hash;
  }
};
}  // namespace std

/* 表元数据 */
struct TabMeta {
  std::string name;           // 表名称
  std::vector<ColMeta> cols;  // 表包含的字段
  std::unordered_map<std::string, IndexMeta>
      indexes;  // 表上建立的索引 索引名 -> 索引元信息
  std::unordered_map<std::string, size_t>
      cols_map;  // 空间换时间，在 Analyze 阶段快速确定列是否存在
  std::unordered_map<std::vector<std::string>, std::string>
      index_names_map;  // cols name -> index file name
  // eg: A(a,b,c) -> A_a_b_c.idx

  /* 判断当前表中是否存在名为col_name的字段 */
  bool is_col(const std::string& col_name) const {
    return cols_map.count(col_name);
  }

  // 统一用 tab.get_index_name
  std::string get_index_name(const std::vector<std::string>& index_cols) {
    // 做个 cache 处理，避免多次连接字符串操作
    auto it = index_names_map.find(index_cols);
    if (it == index_names_map.end()) {
      // 只要为命中才连接
      std::string ix_name(name);
      for (const auto& col : index_cols) {
        ix_name.append("_").append(col);
      }
      ix_name.append(".idx");
      it = index_names_map.emplace(index_cols, std::move(ix_name)).first;
    }
    return it->second;
  }

  /* 判断当前表上是否建有指定索引，索引包含的字段为col_names */
  bool is_index(const std::vector<std::string>& col_names) {
    // TODO 用哈希表优化
    auto ix_name = get_index_name(col_names);
    return indexes.count(ix_name);
  }

  /* 根据字段名称集合获取索引元数据 */
  IndexMeta& get_index_meta(const std::vector<std::string>& col_names) {
    // TODO 优化
    auto it = indexes.find(get_index_name(col_names));
    if (it != indexes.end()) {
      return it->second;
    }
    throw IndexNotFoundError(name, col_names);
  }

  /* 根据字段名称获取字段元数据 */
  std::vector<ColMeta>::iterator get_col(const std::string& col_name) {
    auto pos = cols_map.find(col_name);
    if (pos == cols_map.end()) {
      throw ColumnNotFoundError(col_name);
    }
    return cols.begin() + pos->second;
  }

  friend std::ostream& operator<<(std::ostream& os, const TabMeta& tab) {
    os << tab.name << '\n' << tab.cols.size() << '\n';
    for (auto& col : tab.cols) {
      os << col << '\n';  // col是ColMeta类型，然后调用重载的ColMeta的操作符<<
    }
    os << tab.indexes.size() << "\n";
    for (auto& [index_name, index] : tab.indexes) {
      os << index_name << '\n';
      os << index << "\n";
    }
    return os;
  }

  friend std::istream& operator>>(std::istream& is, TabMeta& tab) {
    size_t n;
    is >> tab.name >> n;
    for (size_t i = 0; i < n; i++) {
      ColMeta col;
      is >> col;
      tab.cols.emplace_back(col);
    }
    for (size_t i = 0; i < tab.cols.size(); ++i) {
      tab.cols_map.emplace(tab.cols[i].name, i);
    }
    is >> n;
    std::string index_name;
    IndexMeta index;
    for (size_t i = 0; i < n; ++i) {
      is >> index_name;
      is >> index;
      tab.indexes.emplace(std::move(index_name), std::move(index));
      // TODO 注意这里要清空
      index.cols.clear();
      index.cols_map.clear();
    }
    return is;
  }
};

// 注意重载了操作符 << 和 >>，这需要更底层同样重载TabMeta、ColMeta的操作符 << 和
// >>
/* 数据库元数据 */
class DbMeta {
  friend class SmManager;
  friend class Analyze;

 private:
  std::string name_;                               // 数据库名称
  std::unordered_map<std::string, TabMeta> tabs_;  // 数据库中包含的表

 public:
  // DbMeta(std::string name) : name_(name) {}

  /* 判断数据库中是否存在指定名称的表 */
  bool is_table(const std::string& tab_name) const {
    return tabs_.count(tab_name);
  }

  void SetTabMeta(const std::string& tab_name, const TabMeta& meta) {
    tabs_[tab_name] = meta;
  }

  /* 获取指定名称表的元数据 */
  TabMeta& get_table(const std::string& tab_name) {
    auto pos = tabs_.find(tab_name);
    if (pos == tabs_.end()) {
      throw TableNotFoundError(tab_name);
    }

    return pos->second;
  }

  // 重载操作符 <<
  friend std::ostream& operator<<(std::ostream& os, const DbMeta& db_meta) {
    os << db_meta.name_ << '\n' << db_meta.tabs_.size() << '\n';
    for (auto& entry : db_meta.tabs_) {
      os << entry.second << '\n';
    }
    return os;
  }

  friend std::istream& operator>>(std::istream& is, DbMeta& db_meta) {
    size_t n;
    is >> db_meta.name_ >> n;
    for (size_t i = 0; i < n; i++) {
      TabMeta tab;
      is >> tab;
      db_meta.tabs_[tab.name] = tab;
    }
    return is;
  }
};

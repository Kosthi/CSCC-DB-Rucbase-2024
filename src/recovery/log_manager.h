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

#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "common/config.h"
#include "log_defs.h"
#include "record/rm_defs.h"

/* 日志记录对应操作的类型 */
enum LogType : int {
  UPDATE = 0,
  INSERT,
  DELETE,
  BEGIN,
  COMMIT,
  ABORT,
  STATIC_CHECKPOINT
};

static std::string LogTypeStr[] = {
    "UPDATE", "INSERT", "DELETE",           "BEGIN",
    "COMMIT", "ABORT",  "STATIC_CHECKPOINT"};

class LogRecord {
 public:
  LogType log_type_;     /* 日志对应操作的类型 */
  lsn_t lsn_;            /* 当前日志的lsn */
  uint32_t log_tot_len_; /* 整个日志记录的长度 */
  txn_id_t log_tid_;     /* 创建当前日志的事务ID */
  lsn_t prev_lsn_;       /* 事务创建的前一条日志记录的lsn，用于undo */

  virtual ~LogRecord() = default;

  // 把日志记录序列化到dest中
  virtual void serialize(char* dest) const {
    memcpy(dest + OFFSET_LOG_TYPE, &log_type_, sizeof(LogType));
    memcpy(dest + OFFSET_LSN, &lsn_, sizeof(lsn_t));
    memcpy(dest + OFFSET_LOG_TOT_LEN, &log_tot_len_, sizeof(uint32_t));
    memcpy(dest + OFFSET_LOG_TID, &log_tid_, sizeof(txn_id_t));
    memcpy(dest + OFFSET_PREV_LSN, &prev_lsn_, sizeof(lsn_t));
  }

  // 从src中反序列化出一条日志记录
  virtual void deserialize(const char* src) {
    log_type_ = *reinterpret_cast<const LogType*>(src);
    lsn_ = *reinterpret_cast<const lsn_t*>(src + OFFSET_LSN);
    log_tot_len_ = *reinterpret_cast<const uint32_t*>(src + OFFSET_LOG_TOT_LEN);
    log_tid_ = *reinterpret_cast<const txn_id_t*>(src + OFFSET_LOG_TID);
    prev_lsn_ = *reinterpret_cast<const lsn_t*>(src + OFFSET_PREV_LSN);
  }

  // used for debug
  virtual void format_print() {
    std::cout << "log type in father_function: " << LogTypeStr[log_type_]
              << "\n";
    printf("Print Log Record:\n");
    printf("log_type_: %s\n", LogTypeStr[log_type_].c_str());
    printf("lsn: %d\n", lsn_);
    printf("log_tot_len: %d\n", log_tot_len_);
    printf("log_tid: %d\n", log_tid_);
    printf("prev_lsn: %d\n", prev_lsn_);
  }
};

class StaticCheckpointLogRecord : public LogRecord {
 public:
  StaticCheckpointLogRecord() {
    log_type_ = STATIC_CHECKPOINT;
    lsn_ = INVALID_LSN;
    log_tot_len_ = LOG_HEADER_SIZE;
    log_tid_ = INVALID_TXN_ID;
    prev_lsn_ = INVALID_LSN;
  }

  StaticCheckpointLogRecord(txn_id_t txn_id) : StaticCheckpointLogRecord() {
    log_tid_ = txn_id;
  }

  // 序列化Begin日志记录到dest中
  void serialize(char* dest) const override { LogRecord::serialize(dest); }

  // 从src中反序列化出一条Begin日志记录
  void deserialize(const char* src) override { LogRecord::deserialize(src); }

  virtual void format_print() override {
    std::cout << "log type in son_function: " << LogTypeStr[log_type_] << "\n";
    LogRecord::format_print();
  }
};

class BeginLogRecord : public LogRecord {
 public:
  BeginLogRecord() {
    log_type_ = BEGIN;
    lsn_ = INVALID_LSN;
    log_tot_len_ = LOG_HEADER_SIZE;
    log_tid_ = INVALID_TXN_ID;
    prev_lsn_ = INVALID_LSN;
  }

  BeginLogRecord(txn_id_t txn_id) : BeginLogRecord() { log_tid_ = txn_id; }

  // 序列化Begin日志记录到dest中
  void serialize(char* dest) const override { LogRecord::serialize(dest); }

  // 从src中反序列化出一条Begin日志记录
  void deserialize(const char* src) override { LogRecord::deserialize(src); }

  virtual void format_print() override {
    std::cout << "log type in son_function: " << LogTypeStr[log_type_] << "\n";
    LogRecord::format_print();
  }
};

/**
 * TODO: commit操作的日志记录
 */
class CommitLogRecord : public LogRecord {
 public:
  CommitLogRecord() {
    log_type_ = COMMIT;
    lsn_ = INVALID_LSN;
    log_tot_len_ = LOG_HEADER_SIZE;
    log_tid_ = INVALID_TXN_ID;
    prev_lsn_ = INVALID_LSN;
  }

  CommitLogRecord(txn_id_t txn_id) : CommitLogRecord() { log_tid_ = txn_id; }

  // 序列化Begin日志记录到dest中
  void serialize(char* dest) const override { LogRecord::serialize(dest); }

  // 从src中反序列化出一条Begin日志记录
  void deserialize(const char* src) override { LogRecord::deserialize(src); }

  void format_print() override {
    std::cout << "log type in son_function: " << LogTypeStr[log_type_] << "\n";
    LogRecord::format_print();
  }
};

/**
 * TODO: abort操作的日志记录
 */
class AbortLogRecord : public LogRecord {
 public:
  AbortLogRecord() {
    log_type_ = ABORT;
    lsn_ = INVALID_LSN;
    log_tot_len_ = LOG_HEADER_SIZE;
    log_tid_ = INVALID_TXN_ID;
    prev_lsn_ = INVALID_LSN;
  }

  AbortLogRecord(txn_id_t txn_id) : AbortLogRecord() { log_tid_ = txn_id; }

  // 序列化Begin日志记录到dest中
  void serialize(char* dest) const override { LogRecord::serialize(dest); }

  // 从src中反序列化出一条Begin日志记录
  void deserialize(const char* src) override { LogRecord::deserialize(src); }

  void format_print() override {
    std::cout << "log type in son_function: " << LogTypeStr[log_type_] << "\n";
    LogRecord::format_print();
  }
};

class InsertLogRecord : public LogRecord {
 public:
  InsertLogRecord() {
    log_type_ = INSERT;
    lsn_ = INVALID_LSN;
    log_tot_len_ = LOG_HEADER_SIZE;
    log_tid_ = INVALID_TXN_ID;
    prev_lsn_ = INVALID_LSN;
    table_name_ = nullptr;
  }

  ~InsertLogRecord() override { delete[] table_name_; }

  InsertLogRecord(txn_id_t txn_id, RmRecord& insert_value, Rid& rid,
                  const std::string& table_name)
      : InsertLogRecord() {
    log_tid_ = txn_id;
    insert_value_ = insert_value;
    rid_ = rid;
    log_tot_len_ += sizeof(int);
    log_tot_len_ += insert_value_.size;
    log_tot_len_ += sizeof(Rid);
    table_name_size_ = table_name.length();
    table_name_ = new char[table_name_size_ + 1];
    memcpy(table_name_, table_name.c_str(), table_name_size_);
    table_name_[table_name_size_] = '\0';
    log_tot_len_ += sizeof(size_t) + table_name_size_;
  }

  // 把 insert 日志记录序列化到 dest 中
  void serialize(char* dest) const override {
    LogRecord::serialize(dest);
    int offset = OFFSET_LOG_DATA;
    memcpy(dest + offset, &insert_value_.size, sizeof(int));
    offset += sizeof(int);
    memcpy(dest + offset, insert_value_.data, insert_value_.size);
    offset += insert_value_.size;
    memcpy(dest + offset, &rid_, sizeof(Rid));
    offset += sizeof(Rid);
    memcpy(dest + offset, &table_name_size_, sizeof(size_t));
    offset += sizeof(size_t);
    memcpy(dest + offset, table_name_, table_name_size_);
  }

  // 从 src 中反序列化出一条 Insert 日志记录
  void deserialize(const char* src) override {
    LogRecord::deserialize(src);
    insert_value_.Deserialize(src + OFFSET_LOG_DATA);
    int offset = OFFSET_LOG_DATA + sizeof(int) + insert_value_.size;
    rid_ = *reinterpret_cast<const Rid*>(src + offset);
    offset += sizeof(Rid);
    table_name_size_ = *reinterpret_cast<const size_t*>(src + offset);
    offset += sizeof(size_t);
    table_name_ = new char[table_name_size_ + 1];
    memcpy(table_name_, src + offset, table_name_size_);
    table_name_[table_name_size_] = '\0';
  }

  void format_print() override {
    printf("insert record\n");
    LogRecord::format_print();
    printf("insert value: %s\n", insert_value_.data);
    printf("insert rid: %d, %d\n", rid_.page_no, rid_.slot_no);
    printf("table name: %s\n", table_name_);
  }

  inline std::string get_table_name() const { return table_name_; }

  RmRecord insert_value_;   // 插入的记录
  Rid rid_;                 // 记录插入的位置
  char* table_name_;        // 插入记录的表名称
  size_t table_name_size_;  // 表名称的大小
};

/**
 * TODO: delete操作的日志记录
 */
class DeleteLogRecord : public LogRecord {
 public:
  DeleteLogRecord() {
    log_type_ = DELETE;
    lsn_ = INVALID_LSN;
    log_tot_len_ = LOG_HEADER_SIZE;
    log_tid_ = INVALID_TXN_ID;
    prev_lsn_ = INVALID_LSN;
    table_name_ = nullptr;
  }

  ~DeleteLogRecord() override { delete[] table_name_; }

  DeleteLogRecord(txn_id_t txn_id, RmRecord& delete_value, Rid& rid,
                  const std::string& table_name)
      : DeleteLogRecord() {
    log_tid_ = txn_id;
    delete_value_ = delete_value;
    rid_ = rid;
    log_tot_len_ += sizeof(int);
    log_tot_len_ += delete_value_.size;
    log_tot_len_ += sizeof(Rid);
    table_name_size_ = table_name.length();
    table_name_ = new char[table_name_size_ + 1];
    memcpy(table_name_, table_name.c_str(), table_name_size_);
    table_name_[table_name_size_] = '\0';
    log_tot_len_ += sizeof(size_t) + table_name_size_;
  }

  // 把 delete 日志记录序列化到 dest 中
  void serialize(char* dest) const override {
    LogRecord::serialize(dest);
    int offset = OFFSET_LOG_DATA;
    memcpy(dest + offset, &delete_value_.size, sizeof(int));
    offset += sizeof(int);
    memcpy(dest + offset, delete_value_.data, delete_value_.size);
    offset += delete_value_.size;
    memcpy(dest + offset, &rid_, sizeof(Rid));
    offset += sizeof(Rid);
    memcpy(dest + offset, &table_name_size_, sizeof(size_t));
    offset += sizeof(size_t);
    memcpy(dest + offset, table_name_, table_name_size_);
  }

  // 从 src 中反序列化出一条 delete 日志记录
  void deserialize(const char* src) override {
    LogRecord::deserialize(src);
    delete_value_.Deserialize(src + OFFSET_LOG_DATA);
    int offset = OFFSET_LOG_DATA + sizeof(int) + delete_value_.size;
    rid_ = *reinterpret_cast<const Rid*>(src + offset);
    offset += sizeof(Rid);
    table_name_size_ = *reinterpret_cast<const size_t*>(src + offset);
    offset += sizeof(size_t);
    table_name_ = new char[table_name_size_ + 1];
    memcpy(table_name_, src + offset, table_name_size_);
    table_name_[table_name_size_] = '\0';
  }

  void format_print() override {
    printf("delete record\n");
    LogRecord::format_print();
    printf("delete value: %s\n", delete_value_.data);
    printf("delete rid: %d, %d\n", rid_.page_no, rid_.slot_no);
    printf("table name: %s\n", table_name_);
  }

  inline std::string get_table_name() { return {table_name_}; }

  RmRecord delete_value_;   // 删除的记录
  Rid rid_;                 // 记录删除的位置
  char* table_name_;        // 删除记录的表名称
  size_t table_name_size_;  // 表名称的大小
};

/**
 * TODO: update操作的日志记录
 */
class UpdateLogRecord : public LogRecord {
 public:
  UpdateLogRecord() {
    log_type_ = UPDATE;
    lsn_ = INVALID_LSN;
    log_tot_len_ = LOG_HEADER_SIZE;
    log_tid_ = INVALID_TXN_ID;
    prev_lsn_ = INVALID_LSN;
    table_name_ = nullptr;
  }

  ~UpdateLogRecord() override { delete[] table_name_; }

  UpdateLogRecord(txn_id_t txn_id, RmRecord& old_value, RmRecord& update_value,
                  Rid& rid, const std::string& table_name)
      : UpdateLogRecord() {
    log_tid_ = txn_id;
    old_value_ = old_value;
    update_value_ = update_value;
    rid_ = rid;
    // 旧值和新值共用一个 size
    log_tot_len_ += sizeof(int) + old_value_.size + update_value_.size;
    log_tot_len_ += sizeof(Rid);
    table_name_size_ = table_name.length();
    table_name_ = new char[table_name_size_ + 1];
    memcpy(table_name_, table_name.c_str(), table_name_size_);
    table_name_[table_name_size_] = '\0';
    log_tot_len_ += sizeof(size_t) + table_name_size_;
  }

  // 把 update 日志记录序列化到 dest 中
  void serialize(char* dest) const override {
    LogRecord::serialize(dest);
    int offset = OFFSET_LOG_DATA;
    memcpy(dest + offset, &old_value_.size, sizeof(int));
    offset += sizeof(int);
    memcpy(dest + offset, old_value_.data, old_value_.size);
    offset += old_value_.size;
    memcpy(dest + offset, update_value_.data, update_value_.size);
    offset += update_value_.size;
    memcpy(dest + offset, &rid_, sizeof(Rid));
    offset += sizeof(Rid);
    memcpy(dest + offset, &table_name_size_, sizeof(size_t));
    offset += sizeof(size_t);
    memcpy(dest + offset, table_name_, table_name_size_);
  }

  // 从 src 中反序列化出一条 update 日志记录
  void deserialize(const char* src) override {
    LogRecord::deserialize(src);
    old_value_.Deserialize(src + OFFSET_LOG_DATA);
    update_value_.Deserialize(
        src + OFFSET_LOG_DATA + sizeof(int) + old_value_.size, old_value_.size);
    int offset =
        OFFSET_LOG_DATA + sizeof(int) + old_value_.size + update_value_.size;
    rid_ = *reinterpret_cast<const Rid*>(src + offset);
    offset += sizeof(Rid);
    table_name_size_ = *reinterpret_cast<const size_t*>(src + offset);
    offset += sizeof(size_t);
    table_name_ = new char[table_name_size_ + 1];
    memcpy(table_name_, src + offset, table_name_size_);
    table_name_[table_name_size_] = '\0';
  }

  void format_print() override {
    printf("update record\n");
    LogRecord::format_print();
    printf("old value: %s\n", old_value_.data);
    printf("update value: %s\n", update_value_.data);
    printf("update rid: %d, %d\n", rid_.page_no, rid_.slot_no);
    printf("table name: %s\n", table_name_);
  }

  inline std::string get_table_name() const { return {table_name_}; }

  RmRecord old_value_;      // 旧的记录
  RmRecord update_value_;   // 新的记录
  Rid rid_;                 // 记录删除的位置
  char* table_name_;        // 删除记录的表名称
  size_t table_name_size_;  // 表名称的大小
};

/* 日志缓冲区，只有一个buffer，因此需要阻塞地去把日志写入缓冲区中 */

class LogBuffer {
 public:
  LogBuffer() {
    offset_ = 0;
    memset(buffer_, 0, sizeof(buffer_));
  }

  bool is_full(uint32_t append_size) const {
    if (offset_ + append_size > LOG_BUFFER_SIZE) return true;
    return false;
  }

  char buffer_[LOG_BUFFER_SIZE + 1];
  uint32_t offset_;  // 写入 log 的 offset
};

/* 日志管理器，负责把日志写入日志缓冲区，以及把日志缓冲区中的内容写入磁盘中 */
class LogManager {
 public:
  explicit LogManager(DiskManager* disk_manager)
      : disk_manager_(disk_manager),
        run_background_thread_(true),
        log_flush_interval_(std::chrono::seconds(1)) {
    background_thread_ = std::thread([this] { background_flush(); });
  }

  ~LogManager() {
    {
      std::lock_guard lock(latch_);
      run_background_thread_ = false;
    }
    cv_.notify_one();
    if (background_thread_.joinable()) {
      background_thread_.join();
    }
  }

  lsn_t add_log_to_buffer(LogRecord* log_record);

  void flush_log_to_disk();

  inline LogBuffer* get_log_buffer() { return &log_buffer_; }
  inline lsn_t get_persist_lsn() const { return persist_lsn_; }
  inline void set_global_lsn(lsn_t global_lsn) {
    global_lsn_.store(global_lsn);
  }
  inline void set_persist_lsn(lsn_t persist_lsn) { persist_lsn_ = persist_lsn; }

 private:
  void background_flush() {
    std::unique_lock lk(latch_);
    while (run_background_thread_) {
      if (cv_.wait_for(lk, log_flush_interval_,
                       [this] { return !run_background_thread_; })) {
        break;
      }
      flush_log_to_disk();
    }
  }

  std::condition_variable cv_;
  std::thread background_thread_;
  bool run_background_thread_{};
  std::chrono::seconds log_flush_interval_{};

  std::atomic<lsn_t> global_lsn_{0};  // 全局lsn，递增，用于为每条记录分发lsn
  std::mutex latch_;                  // 用于对log_buffer_的互斥访问
  LogBuffer log_buffer_;              // 日志缓冲区
  lsn_t persist_lsn_{};  // 记录已经持久化到磁盘中的最后一条日志的日志号
  DiskManager* disk_manager_;
};

/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL
v2. You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

// #define NDEBUG

#include <netinet/in.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <future>

#include "analyze/analyze.h"
#include "errors.h"
#include "fmt/core.h"
#include "fmt/printf.h"
#include "optimizer/optimizer.h"
#include "optimizer/plan.h"
#include "optimizer/planner.h"
#include "portal.h"
#include "recovery/log_recovery.h"
#include "spdlog/spdlog.h"

#define SOCK_PORT 8765
#define MAX_CONN_LIMIT 8

// 是否开启 std::cout
#define ENABLE_COUT

static bool should_exit = false;

// 构建全局所需的管理器对象
auto disk_manager = std::make_unique<DiskManager>();
auto log_manager = std::make_unique<LogManager>(disk_manager.get());
auto buffer_pool_manager = std::make_unique<BufferPoolManager>(
    BUFFER_POOL_SIZE, disk_manager.get(), log_manager.get());
auto rm_manager =
    std::make_unique<RmManager>(disk_manager.get(), buffer_pool_manager.get());
auto ix_manager =
    std::make_unique<IxManager>(disk_manager.get(), buffer_pool_manager.get());
auto sm_manager =
    std::make_unique<SmManager>(disk_manager.get(), buffer_pool_manager.get(),
                                rm_manager.get(), ix_manager.get());
auto lock_manager = std::make_unique<LockManager>();
auto txn_manager =
    std::make_unique<TransactionManager>(lock_manager.get(), sm_manager.get());
auto planner = std::make_unique<Planner>(sm_manager.get());
auto optimizer = std::make_unique<Optimizer>(sm_manager.get(), planner.get());
auto ql_manager = std::make_unique<QlManager>(sm_manager.get(),
                                              txn_manager.get(), planner.get());
auto recovery = std::make_unique<RecoveryManager>(
    disk_manager.get(), buffer_pool_manager.get(), sm_manager.get(),
    log_manager.get(), txn_manager.get());
auto portal = std::make_unique<Portal>(sm_manager.get());
auto analyze = std::make_unique<Analyze>(sm_manager.get());
// pthread_mutex_t *buffer_mutex;
pthread_mutex_t* sockfd_mutex;

// 定义线程池大小
// constexpr int MAX_THREAD_POOL_SIZE = 2;
std::list<std::thread> load_thread_pool;
std::deque<std::future<void> > futures;
std::mutex pool_mutex;

// file_name -> command
std::unordered_map<std::string, std::string> sort_map = {
    {"warehouse", "none"},
    {"item", "none"},
    {"stock", "sort -n -t, -k2,2 -k1,1 "},
    {"district", "sort -n -t, -k2,2 -k1,1 "},
    {"customer", "sort -n -t, -k3,3 -k2,2 -k1,1 "},
    {"history", "none"},
    {"orders", "sort -n -t, -k3,3 -k2,2 -k1,1 "},
    {"new_orders", "sort -n -t, -k3,3 -k2,2 -k1,1 "},
    {"order_line", "sort -n -t, -k3,3 -k2,2 -k1,1 -k4,4 "}};

void load_data(std::string filename, std::string tabname);

int fast_count_star(std::string& tabname, Context* context);

static jmp_buf jmpbuf;

void sigint_handler(int signo) {
  should_exit = true;
  log_manager->flush_log_to_disk();
#ifdef ENABLE_COUT
  spdlog::info("The Server receive Crtl+C, will been closed");
#endif
  longjmp(jmpbuf, 1);
}

// 判断当前正在执行的是显式事务还是单条SQL语句的事务，并更新事务ID
void SetTransaction(txn_id_t* txn_id, Context* context) {
  context->txn_ = txn_manager->get_transaction(*txn_id);
  if (context->txn_ == nullptr ||
      context->txn_->get_state() == TransactionState::COMMITTED ||
      context->txn_->get_state() == TransactionState::ABORTED) {
    context->txn_ = txn_manager->begin(nullptr, context->log_mgr_);
    *txn_id = context->txn_->get_transaction_id();
    context->txn_->set_txn_mode(false);
  }
}

void* client_handler(void* sock_fd) {
  int fd = *((int*)sock_fd);
  free(sock_fd);
  pthread_mutex_unlock(sockfd_mutex);

  int i_recvBytes;
  // 接收客户端发送的请求
  char data_recv[BUFFER_LENGTH];
  // 需要返回给客户端的结果
  char* data_send = new char[BUFFER_LENGTH];
  // 需要返回给客户端的结果的长度
  int offset = 0;
  // 记录客户端当前正在执行的事务ID
  txn_id_t txn_id = INVALID_TXN_ID;
  // 每个 client 分配一个词法分析器解析 SQL 语句
  yyscan_t scanner;
  yylex_init(&scanner);

#ifdef ENABLE_COUT
  spdlog::info("establish client connection, sockfd: %d", fd);
#endif

  while (true) {
#ifdef ENABLE_COUT
    spdlog::info("Waiting for request...", fd);
#endif
    memset(data_recv, 0, BUFFER_LENGTH);

    i_recvBytes = recv(fd, data_recv, BUFFER_LENGTH, 0);

    if (i_recvBytes == 0) {
#ifdef ENABLE_COUT
      spdlog::info("Maybe the client has closed");
#endif
      break;
    }

    if (i_recvBytes == -1) {
#ifdef ENABLE_COUT
      spdlog::info("Client read error!");
#endif
      break;
    }

#ifdef ENABLE_COUT
    // printf("i_recvBytes: %d \n ", i_recvBytes);
#endif

    if (strcmp(data_recv, "exit") == 0) {
      spdlog::info("Client exit.");
      break;
    }

    if (strcmp(data_recv, "crash") == 0) {
      spdlog::info("Server crash");
      delete[] data_send;
      for (auto& [_, txn] : txn_manager->txn_map) {
        std::ignore = _;
        delete txn;
      }
      exit(1);
    }

    // 处理数据
    if (strncmp(data_recv, "load", 4) == 0 ||
        strncmp(data_recv, "LOAD", 4) == 0) {
      std::string str(data_recv);
      std::stringstream ss(str);
      std::string load_keyword, path, into_keyword, table_name;
      ss >> load_keyword >> path >> into_keyword >> table_name;

      table_name.pop_back();

      // if (futures.size() >= 2) {
      //     futures.front().get();
      //     futures.pop_front();
      // }

      // 将任务加入线程池
      futures.emplace_back(std::async(std::launch::async, load_data,
                                      std::move(path), std::move(table_name)));

      std::string s = "l\n";
      if (write(fd, s.c_str(), s.length()) == -1) {
        throw UnixError();
      }
      continue;
    }

    pool_mutex.lock();
    for (auto& future : futures) {
      future.get();
    }
    if (!futures.empty()) {
      for (auto& [_, fh] : sm_manager->fhs_) {
        std::ignore = _;
        buffer_pool_manager->flush_all_pages(fh->GetFd());
      }
      for (auto& [_, ih] : sm_manager->ihs_) {
        std::ignore = _;
        buffer_pool_manager->flush_all_pages(ih->fd_);
      }
    }
    futures.clear();
    pool_mutex.unlock();
#ifdef ENABLE_COUT
    spdlog::info("Read from client %d: %d", fd, data_recv);
#endif
    memset(data_send, '\0', BUFFER_LENGTH);
    offset = 0;

    // 开启事务，初始化系统所需的上下文信息（包括事务对象指针、锁管理器指针、日志管理器指针、存放结果的buffer、记录结果长度的变量）
    Context* context = new Context(lock_manager.get(), log_manager.get(),
                                   nullptr, data_send, &offset);
    SetTransaction(&txn_id, context);

    // 用于判断是否已经调用了 yy_delete_buffer 来删除 buf
    bool finish_analyze = false;
    // pthread_mutex_lock(buffer_mutex);
    YY_BUFFER_STATE buf = yy_scan_string(data_recv, scanner);
    if (yyparse(scanner) == 0) {
      if (ast::parse_tree != nullptr) {
        try {
          // analyze and rewrite
          // 查询计划生成
          std::shared_ptr<Query> query =
              analyze->do_analyze(std::move(ast::parse_tree));
          yy_delete_buffer(buf, scanner);
          finish_analyze = true;
          // pthread_mutex_unlock(buffer_mutex);
          // 全表 count 走 fast_count
          if (query->agg_types.size() == 1 &&
              query->agg_types[0] == AGG_COUNT && query->conds.empty()) {
            // 后续支持笛卡尔积 count，这里先简化只有单个表
            auto& col_name = query->alias.empty() ? query->cols[0].col_name
                                                  : query->alias[0];
            ql_manager->select_fast_count_star(
                fast_count_star(query->tables[0], context), col_name, context);
          } else {
            // 优化器
            std::shared_ptr<Plan> plan = optimizer->plan_query(query, context);
            // portal
            std::shared_ptr<PortalStmt> portalStmt =
                portal->start(plan, context);
            portal->run(portalStmt, ql_manager.get(), &txn_id, context);
            portal->drop();
          }
        } catch (TransactionAbortException& e) {
          // 事务需要回滚，需要把abort信息返回给客户端并写入output.txt文件中
          std::string str = "abort\n";
          memcpy(data_send, str.c_str(), str.length());
          data_send[str.length()] = '\0';
          offset = str.length();

          // 回滚事务
          txn_manager->abort(context->txn_, log_manager.get());
#ifdef ENABLE_COUT
          std::cout << e.GetInfo() << std::endl;
#endif

          if (planner->enable_output_file) {
            std::fstream outfile;
            outfile.open("output.txt", std::ios::out | std::ios::app);
            outfile << str;
            outfile.close();
          }
        } catch (RMDBError& e) {
          // 遇到异常，需要打印failure到output.txt文件中，并发异常信息返回给客户端
#ifdef ENABLE_COUT
          std::cerr << e.what() << std::endl;
#endif

          memcpy(data_send, e.what(), e.get_msg_len());
          data_send[e.get_msg_len()] = '\n';
          data_send[e.get_msg_len() + 1] = '\0';
          offset = e.get_msg_len() + 1;

          // 将报错信息写入output.txt
          if (planner->enable_output_file) {
            std::fstream outfile;
            outfile.open("output.txt", std::ios::out | std::ios::app);
            outfile << "failure\n";
            outfile.close();
          }
        }
      }
    } else {
      // 遇到异常，需要打印failure到output.txt文件中，并发异常信息返回给客户端
      // std::string str = "语法层解析错误";
      // std::cerr << str << std::endl;

      // memcpy(data_send, str.c_str(), str.size());
      // data_send[str.size()] = '\n';
      // data_send[str.size() + 1] = '\0';
      // offset = str.size() + 1;

      // 将报错信息写入output.txt
      if (planner->enable_output_file) {
        std::fstream outfile;
        outfile.open("output.txt", std::ios::out | std::ios::app);
        outfile << "failure\n";
        outfile.close();
      }
    }
    if (finish_analyze == false) {
      yy_delete_buffer(buf, scanner);
      // pthread_mutex_unlock(buffer_mutex);
    }
    // future TODO: 格式化 sql_handler.result, 传给客户端
    // send result with fixed format, use protobuf in the future
    if (send(fd, data_send, offset + 1, 0) == -1) {
      perror("Send failed");
      break;
    }
    // 如果是单挑语句，需要按照一个完整的事务来执行，所以执行完当前语句后，自动提交事务
    if (context->txn_->get_txn_mode() == false) {
      txn_manager->commit(context->txn_, context->log_mgr_);
    }
    delete context;
  }

  // release memory
  delete[] data_send;

  // Clear
#ifdef ENABLE_COUT
  spdlog::info("Terminating current client_connection...");
#endif
  close(fd);           // close a file descriptor.
  pthread_exit(NULL);  // terminate calling thread!
}

void start_server() {
  // init mutex
  // buffer_mutex = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
  sockfd_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
  // pthread_mutex_init(buffer_mutex, nullptr);
  pthread_mutex_init(sockfd_mutex, nullptr);

  int sockfd_server;
  int fd_temp;
  struct sockaddr_in s_addr_in{};

  // 初始化连接
  sockfd_server = socket(AF_INET, SOCK_STREAM, 0);  // ipv4,TCP
  assert(sockfd_server != -1);
  int val = 1;
  setsockopt(sockfd_server, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  // before bind(), set the attr of structure sockaddr.
  memset(&s_addr_in, 0, sizeof(s_addr_in));
  s_addr_in.sin_family = AF_INET;
  s_addr_in.sin_addr.s_addr = htonl(INADDR_ANY);
  s_addr_in.sin_port = htons(SOCK_PORT);
  fd_temp =
      bind(sockfd_server, (struct sockaddr*)(&s_addr_in), sizeof(s_addr_in));
  if (fd_temp == -1) {
    std::cout << "Bind error!" << std::endl;
    exit(1);
  }

  fd_temp = listen(sockfd_server, MAX_CONN_LIMIT);
  if (fd_temp == -1) {
    std::cout << "Listen error!" << std::endl;
    exit(1);
  }

  while (!should_exit) {
#ifdef ENABLE_COUT
    spdlog::info("Waiting for new connection...");
#endif
    // 解决局部变量析构问题
    int* sockfd = (int*)malloc(sizeof(int));

    pthread_t thread_id;
    struct sockaddr_in s_addr_client{};
    int client_length = sizeof(s_addr_client);

    if (setjmp(jmpbuf)) {
      free(sockfd);
      std::cout << "Break from Server Listen Loop\n";
      break;
    }

    // Block here. Until server accepts a new connection.
    pthread_mutex_lock(sockfd_mutex);
    *sockfd = accept(sockfd_server, (struct sockaddr*)(&s_addr_client),
                     (socklen_t*)(&client_length));
    if (*sockfd == -1) {
      free(sockfd);
      std::cout << "Accept error!" << std::endl;
      continue;  // ignore current socket ,continue while loop.
    }

    // 和客户端建立连接，并开启一个线程负责处理客户端请求
    if (pthread_create(&thread_id, nullptr, &client_handler, (void*)(sockfd)) !=
        0) {
      free(sockfd);
      std::cout << "Create thread fail!" << std::endl;
      break;  // break while loop
    }

    // 将线程设置为分离状态，自动回收资源
    pthread_detach(thread_id);
  }

  buffer_pool_manager->ouput_info();

  // Clear
  std::cout << " Try to close all client-connection.\n";
  int ret = shutdown(
      sockfd_server,
      SHUT_WR);  // shut down the all or part of a full-duplex connection.
  if (ret == -1) {
    printf("%s\n", strerror(errno));
  }
  //    assert(ret != -1);
  std::cout << "before close db: " << std::endl;
  sm_manager->close_db();
  std::cout << "before delete txn: " << std::endl;
  for (auto& [_, txn] : txn_manager->txn_map) {
    std::ignore = _;
    delete txn;
  }
  txn_manager->txn_map.clear();

#ifdef ENABLE_COUT
  std::cout << " DB has been closed.\n";
  std::cout << "Server shuts down." << std::endl;
#endif
}

int main(int argc, char** argv) {
  if (argc != 2) {
    // 需要指定数据库名称
    std::cerr << "Usage: " << argv[0] << " <database>" << std::endl;
    exit(1);
  }

  signal(SIGINT, sigint_handler);
  signal(SIGTERM, sigint_handler);
  try {
#ifdef ENABLE_COUT
    spdlog::info(
        "\n"
        "  _____  __  __ _____  ____  \n"
        " |  __ \\|  \\/  |  __ \\|  _ \\ \n"
        " | |__) | \\  / | |  | | |_) |\n"
        " |  _  /| |\\/| | |  | |  _ < \n"
        " | | \\ \\| |  | | |__| | |_) |\n"
        " |_|  \\_\\_|  |_|_____/|____/ \n"
        "\n"
        "Welcome to RMDB!\n"
        "Type 'help;' for help.\n"
        "\n");
#endif
    // Database name is passed by args
    std::string db_name = argv[1];
    if (!sm_manager->is_dir(db_name)) {
      // Database not found, create a new one
      sm_manager->create_db(db_name);
    }
    // Open database
    sm_manager->open_db(db_name);

    // recovery database
#ifdef ENABLE_LOGGING
    recovery->analyze();
    recovery->redo();
    recovery->undo();
#endif

    // 静态 map 预留空间
    TransactionManager::Initialize(20);
    // 开启服务端，开始接受客户端连接
    start_server();
  } catch (RMDBError& e) {
    std::cerr << e.what() << std::endl;
    exit(1);
  }
  return 0;
}

std::string doSort(const std::string& filename, const std::string& tabname) {
  if (sort_map[tabname] == "none") {
    return filename;
  }
  std::string actual_filename = "sorted_" + tabname + ".csv";
  std::string command = sort_map[tabname] + filename + " > " + actual_filename;
  // Open the pipe
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"),
                                                pclose);
  if (!pipe) {
    throw std::runtime_error("popen() failed!");
  }
  return actual_filename;
}

// 使用命令wc -l < file.csv 得到行数
int getFileLineCount(const std::string& filename) {
  std::string command = "wc -l < " + filename;
  std::array<char, 128> buffer{};
  std::string result;

  // Open the pipe
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"),
                                                pclose);
  if (!pipe) {
    throw std::runtime_error("popen() failed!");
  }

  // Read the output
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }

  // Extract the number of lines from the result string
  // 记得减去表头
  return std::stoi(result) - 1;
}

void load_data(std::string filename, std::string tabname) {
  // 如果使用 bulkloading 算法索引载入，必须先 sort 表文件
  // filename = doSort(filename, tabname);

  // 获取 table
  auto& tab_ = sm_manager->db_.get_table(tabname);
  auto&& fh = sm_manager->fhs_[tabname].get();

  int& record_len_ = fh->get_file_hdr().record_size;
  int& max_nums_ = fh->get_file_hdr().num_records_per_page;

  int fd = open(filename.c_str(), O_RDONLY);
  if (fd == -1) {
    perror("Error opening file");
    return;
  }

  // 得到文件大小
  struct stat sb;
  if (fstat(fd, &sb) == -1) {
    perror("Error getting file size");
    close(fd);
    return;
  }

  size_t file_size = sb.st_size;

  // Memory map the file
  char* file_content = static_cast<char*>(
      mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0));
  if (file_content == MAP_FAILED) {
    perror("Error mapping file");
    close(fd);
    return;
  }

  // 跳过表头
  size_t i = 0;
  // 使用 std::find 查找换行符的位置
  const char* newlinePos =
      std::find(file_content, file_content + file_size, '\n');
  // 一定存在表头
  i = newlinePos - file_content + 1;  // 跳过换行符

  // 按页刷入
  auto page_size = max_nums_ * record_len_;
  // 先试试不乘 2
  char* data = new char[page_size];
  char* cur = data;
  // 从第一页开始放数据
  int page_no = 1;

  int row = 0;
  int nums_record = 0;

  // 读取到 data 中
  int col_idx = 0;
  auto& cols_meta = tab_.cols;

  // int index_pages = 0;
  // int ih_fd = -1;

  // 支持正常索引载入与 bulkloading 批量载入
  // bulkloading
  // 算法参考了开源实现，并修复了两处实现缺陷，完美适应了本项目实现的 B+ 树引擎
  if (!tab_.indexes.empty()) {
    // 只有一个索引
    //        int num_lines = getFileLineCount(filename);
    //
    //        auto &ix_name = tab_.indexes.begin()->first;
    //        auto &ih = sm_manager->ihs_[ix_name];
    //        // ih_fd = ih->fd_;
    //
    //        auto &index_meta = tab_.indexes.begin()->second;
    //        int &tot_len = index_meta.col_tot_len;
    //
    //        // 直接设置开始分配的页面号
    //        // disk_manager->set_fd2pageno(ih->fd_, 2);
    //
    //        // 创建第一个叶子结点
    //        int pos = 0;
    //        auto node = ih->fetch_node(ih->file_hdr_->root_page_);
    //        // ih->file_hdr_->first_leaf_ = node->get_page_no();
    //        // node->set_is_leaf_page(true);
    //
    //        char *node_key = node->get_key(0);
    //        Rid *node_rid = node->get_rid(0);
    //
    //        int block = 0;
    //        // 计算叶子结点的个数和每块应装入的元组条数
    //        // max_size * 0.7 大小 尽可能避免分裂重组
    //        int expected_leaf_num =
    //        static_cast<int>((ih->file_hdr_->btree_order_ + 1) * 0.7); int
    //        blocks = num_lines / expected_leaf_num + (num_lines %
    //        expected_leaf_num ? 1 : 0);
    //        // 调整块数量，确保每个叶子节点半满
    //        if (blocks > 1 && (ih->file_hdr_->btree_order_ + 1) / 2 >
    //        num_lines / blocks) {
    //            // 可能执行到这里
    //            --blocks;
    //        }
    //
    //        // 实际每个叶子上的元组数量
    //        int actual_leaf_num = num_lines / blocks;
    //        // 多出来的元组
    //        int remaining_leaf_num = num_lines % blocks;
    //
    //        // max_size * 0.9 大小 让父节点尽可能放入更多的元组
    //        int upper_expected_parent_num =
    //        static_cast<int>((ih->file_hdr_->btree_order_ + 1) * 0.9); int
    //        upper_blocks = blocks / upper_expected_parent_num + (num_lines %
    //        upper_expected_parent_num ? 1 : 0);
    //        // 调整块数量，确保每个叶子节点半满
    //        if (upper_blocks > 1 && (ih->file_hdr_->btree_order_ + 1) / 2 >
    //        blocks / upper_blocks) {
    //            --upper_blocks;
    //        }
    //
    //        // 实际每个父亲节点上的元组数量
    //        int actual_upper_parent_num = blocks / upper_blocks;
    //        // 多出来的元组
    //        int remaining_upper_parent_num = blocks % upper_blocks;
    //
    //        // 第一个父亲节点页号
    //        int parent_page_no = blocks + 2;
    //
    //        // 只能构成一个节点，即为根节点
    //        if (blocks == 1) {
    //            node->set_parent_page_no(INVALID_PAGE_ID);
    //            ih->file_hdr_->root_page_ = node->get_page_no();
    //        } else {
    //            // 否则叶子节点指向父亲节点
    //            node->set_parent_page_no(parent_page_no);
    //        }
    //
    //        // 设置叶子节点大小
    //        node->set_size(actual_leaf_num);
    //
    //        // 对于父亲节点，孩子节点数量
    //        int child_pos = 0;
    //
    //        // 把每个索引块的第一个元组拷贝，用于父亲节点生成
    //        char *key_temp = nullptr;
    //        char *key_temp_head = nullptr;
    //        Rid *rid_temp = nullptr;
    //        Rid *rid_temp_head = nullptr;
    //
    //        // 只有有父亲节点才需要
    //        if (blocks > 1) {
    //            key_temp = new char[blocks * tot_len];
    //            key_temp_head = key_temp;
    //            rid_temp = new Rid[blocks];
    //            rid_temp_head = rid_temp;
    //        }
    //
    // char *key = new char[tot_len];
    auto* txn = new Transaction(666);

    // i 慢指针，j 快指针
    for (std::size_t j = i; j < file_size; ++j) {
      if (file_content[j] == ',') {
        switch (cols_meta[col_idx].type) {
          case TYPE_INT: {
            *(int*)(cur + cols_meta[col_idx].offset) =
                std::atoi(file_content + i);
            break;
          }
          case TYPE_FLOAT: {
            *(float*)(cur + cols_meta[col_idx].offset) =
                std::atof(file_content + i);
            break;
          }
          case TYPE_STRING: {
            memcpy(cur + cols_meta[col_idx].offset, file_content + i,
                   cols_meta[col_idx].len);
            break;
          }
          default:
            break;
        }
        ++col_idx;
        i = j + 1;
      } else if (file_content[j] == '\n' || j == file_size - 1) {
        switch (cols_meta[col_idx].type) {
          case TYPE_INT: {
            *(int*)(cur + cols_meta[col_idx].offset) =
                std::atoi(file_content + i);
            break;
          }
          case TYPE_FLOAT: {
            *(float*)(cur + cols_meta[col_idx].offset) =
                std::atof(file_content + i);
            break;
          }
          case TYPE_STRING: {
            memcpy(cur + cols_meta[col_idx].offset, file_content + i,
                   cols_meta[col_idx].len);
            break;
          }
          default:
            break;
        }
        col_idx = 0;
        i = j + 1;

        // 得到索引键
        //                for (auto &[index_offset, col]: index_meta.cols) {
        //                    memcpy(key + index_offset, cur + col.offset,
        //                    col.len);
        //                }

        // 正常索引载入用这个
        for (auto& [index_name, index] : tab_.indexes) {
          auto& ih = sm_manager->ihs_[index_name];
          char* key = new char[index.col_tot_len];
          for (auto& [index_offset, col_meta] : index.cols) {
            memcpy(key + index_offset, cur + col_meta.offset, col_meta.len);
          }
          ih->insert_entry(key, {page_no, row % max_nums_}, txn);
          delete[] key;
        }

        // 一个叶子节点放满了
        //                if (pos == actual_leaf_num) {
        //                    pos = 0;
        //
        //                    auto new_node = ih->create_node();
        //                    // 设置叶子节点前后关系
        //                    new_node->set_prev_leaf(node->get_page_no());
        //                    new_node->set_next_leaf(node->get_next_leaf());
        //                    node->set_next_leaf(new_node->get_page_no());
        //
        //                    // unpin
        //                    buffer_pool_manager->unpin_page(node->get_page_id(),
        //                    true);
        //
        //                    node = std::move(new_node);
        //                    node->set_is_leaf_page(true);
        //                    node_key = node->get_key(0);
        //                    node_rid = node->get_rid(0);
        //
        //                    if (++block + remaining_leaf_num == blocks) {
        //                        // assert(0);
        //                        ++actual_leaf_num;
        //                    }
        //
        //                    // 设置大小
        //                    node->set_size(actual_leaf_num);
        //
        //                    // 对于父亲节点，孩子节点数量 + 1
        //                    if (++child_pos >= actual_upper_parent_num) {
        //                        // 父亲节点 + 1
        //                        // 孩子节点数量设置为 0
        //                        child_pos = 0;
        //                        //
        //                        注意这里的2和起始节点页号有关，parent_page_no
        //                        - blocks - 2 才是实际父节点块数 if
        //                        (++parent_page_no - blocks - 2 == upper_blocks
        //                        - remaining_upper_parent_num) {
        //                            ++actual_upper_parent_num;
        //                        }
        //                    }
        //                    // 指向父亲节点
        //                    node->set_parent_page_no(parent_page_no);
        //                }
        //
        //                //
        //                注意如果一个叶子节点放满了，跳到下一个叶子头也得拷贝
        //                if (pos == 0 && blocks > 1) {
        //                    // 将每个索引块的第一个元组拷贝一份到临时数组中
        //                    memcpy(key_temp_head, key, tot_len);
        //                    Rid rid{.page_no = node->get_page_no(), .slot_no =
        //                    0}; memcpy(rid_temp_head, &rid, sizeof(Rid));
        //                    key_temp_head += tot_len;
        //                    // 加一个 rid
        //                    ++rid_temp_head;
        //                }

        // 初始化键值对
        //                Rid rid{page_no, row % max_nums_};
        //                memcpy(node_key, key, tot_len);
        //                memcpy(node_rid, &rid, sizeof(Rid));
        //                node_key += tot_len;
        //                ++node_rid;
        //                ++pos;

        cur += record_len_;
        // 满足一页或者读到最后了，刷进去
        if ((row + 1) % max_nums_ == 0 || j == file_size - 1) {
          nums_record = (row + 1) % max_nums_ == 0 ? (row == 0 ? 1 : max_nums_)
                                                   : (row + 1) % max_nums_;
          fh->load_record(page_no, data, nums_record, cur - data);
          ++page_no;
          cur = data;
        }

        ++row;
      }
    }

    // delete []key;

    // 设置最后一个叶子节点
    //        ih->file_hdr_->last_leaf_ = node->get_page_no();
    //        // unpin
    //        buffer_pool_manager->unpin_page(node->get_page_id(), true);
    //
    //        // 需要建立父亲节点
    //        if (blocks > 1) {
    //            ih->create_upper_parent_nodes(key_temp, rid_temp, blocks + 2,
    //            blocks);
    //        }

    // index_pages = ih->file_hdr_->num_pages_;

    delete txn;

    // ih->Draw(buffer_pool_manager.get(), "dis_Graph.dot");
  } else {
    // i 慢指针，j 快指针
    for (std::size_t j = i; j < file_size; ++j) {
      if (file_content[j] == ',') {
        switch (cols_meta[col_idx].type) {
          case TYPE_INT: {
            *(int*)(cur + cols_meta[col_idx].offset) =
                std::atoi(file_content + i);
            break;
          }
          case TYPE_FLOAT: {
            *(float*)(cur + cols_meta[col_idx].offset) =
                std::atof(file_content + i);
            break;
          }
          case TYPE_STRING: {
            memcpy(cur + cols_meta[col_idx].offset, file_content + i,
                   cols_meta[col_idx].len);
            break;
          }
          default:
            break;
        }
        ++col_idx;
        i = j + 1;
      } else if (file_content[j] == '\n' || j == file_size - 1) {
        switch (cols_meta[col_idx].type) {
          case TYPE_INT: {
            *(int*)(cur + cols_meta[col_idx].offset) =
                std::atoi(file_content + i);
            break;
          }
          case TYPE_FLOAT: {
            *(float*)(cur + cols_meta[col_idx].offset) =
                std::atof(file_content + i);
            break;
          }
          case TYPE_STRING: {
            memcpy(cur + cols_meta[col_idx].offset, file_content + i,
                   cols_meta[col_idx].len);
            break;
          }
          default:
            break;
        }
        col_idx = 0;
        i = j + 1;

        cur += record_len_;
        // 满足一页或者读到最后了，刷进去
        if ((row + 1) % max_nums_ == 0 || j == file_size - 1) {
          nums_record = (row + 1) % max_nums_ == 0 ? (row == 0 ? 1 : max_nums_)
                                                   : (row + 1) % max_nums_;
          fh->load_record(page_no, data, nums_record, cur - data);
          ++page_no;
          cur = data;
        }

        ++row;
      }
    }
  }

  // Unmap the file and close the file descriptor
  if (munmap(file_content, file_size) == -1) {
    perror("Error unmapping file");
  }
  close(fd);

  if (nums_record < max_nums_) {
    fh->set_first_free_page_no(page_no - 1);
  }

  // printf("table: %s, fd: %d, used table pages: %d\n", tabname.c_str(),
  // fh->GetFd(), page_no - 1); printf("table: %s, fd: %d, used index pages:
  // %d\n", tabname.c_str(), ih_fd, index_pages);

  // 释放内存
  delete[] data;
}

int fast_count_star(std::string& tabname, Context* context) {
  auto& fh = sm_manager->fhs_[tabname];
  context->lock_mgr_->lock_shared_on_table(context->txn_, fh->GetFd());

  int count = 0;
  auto first_page = RM_FIRST_RECORD_PAGE;
  auto& total_pages = fh->get_file_hdr().num_pages;
  while (first_page < total_pages) {
    auto&& page_handle = fh->fetch_page_handle(first_page++);
    count += page_handle.page_hdr->num_records;
    // TODO 记得 unpin
    buffer_pool_manager->unpin_page(page_handle.page->get_page_id(), false);
  }

  return count;
}

<div align="center">
<img src="RMDB.jpg"  width=25%  /> 
</div>

全国大学生计算机系统能力大赛数据库管理系统赛道，以培养学生“数据库管理系统内核实现”能力为目标。本次比赛为参赛队伍提供数据库管理系统代码框架RMDB，参赛队伍在RMDB的基础上，设计和实现一个完整的关系型数据库管理系统，该系统要求具备运行TPC-C基准测试（TPC-C是一个面向联机事务处理的测试基准）常用负载的能力。

RMDB由中国人民大学数据库教学团队开发，同时得到教育部-华为”智能基座”项目的支持，平台、赛题和测试用例等得到了全国大学生计算机系统能力大赛数据库管理系统赛道技术委员会的支持和审核。系统能力大赛专家组和[101计划数据库系统课程](http://101.pku.edu.cn/courseDetails?id=DC767C683D697417E0555943CA7634DE)工作组给予了指导。

## 作品简介
队伍学校：沈阳工业大学

队伍名称：DataDance

队伍成员：**吴奕民**、张梦圆

钻石赞助：[**RushDB 实验室**](https://github.com/RushDB-Lab)(**声明：本仓库代码与RushDB无任何关系，所有基于本仓库的二次修改请删除此行**)

初赛完成度（100%），决赛完成度（99%，还有更多优化空间）。

决赛所使用的 [**TPC-C测试脚本**](https://github.com/Kosthi/TPCC-Tester) 一并开源。

[Final-Competition](https://github.com/Kosthi/CSCC-DB-Rucbase-2024/commits/Final-Competition) 版本，最后在决赛赛后通道跑到上限 8w tpmc，期待 25 年🈶队伍能打出新的战绩。

[![24决赛模拟赛.png](https://s21.ax1x.com/2025/02/24/pE1eO0J.png)](https://imgse.com/i/pE1eO0J)

## 分支信息

参赛过程各阶段代码均以分支形式归档。

目前主分支（main）不定期维护开发。

| 阶段  | 题目名称           | 分支链接                                                                                   |
|-------|--------------------|------------------------------------------------------------------------------------------|
| 初赛  | 缓冲池管理         | [Task1-StorageManagement](https://github.com/Kosthi/CSCC-DB-Rucbase-2024/tree/Task1-StorageManagement) |
| 初赛  | 查询执行           | [Task2-QueryExecution](https://github.com/Kosthi/CSCC-DB-Rucbase-2024/tree/Task2-QueryExecution)       |
| 初赛  | 唯一索引           | [Task3-UniqueIndex](https://github.com/Kosthi/CSCC-DB-Rucbase-2024/tree/Task3-UniqueIndex)             |
| 初赛  | 聚合算子           | [Task4-AggGroup](https://github.com/Kosthi/CSCC-DB-Rucbase-2024/tree/Task4-AggGroup)                   |
| 初赛  | 不相关子查询       | [Task5-IrrSubquery](https://github.com/Kosthi/CSCC-DB-Rucbase-2024/tree/Task5-IrrSubquery)             |
| 初赛  | 归并查询           | [Task6-SortMerge](https://github.com/Kosthi/CSCC-DB-Rucbase-2024/tree/Task6-SortMerge)                 |
| 初赛  | 事务控制           | [Task7-TxnControl](https://github.com/Kosthi/CSCC-DB-Rucbase-2024/tree/Task7-TxnControl)               |
| 初赛  | 冲突可串行化       | [Task8-ConflictSerial](https://github.com/Kosthi/CSCC-DB-Rucbase-2024/tree/Task8-ConflictSerial)       |
| 初赛  | 间隙锁             | [Task9-GapLock](https://github.com/Kosthi/CSCC-DB-Rucbase-2024/tree/Task9-GapLock)                  |
| 初赛  | 静态检查点         | [Task10-StaticCheckpointRecovery](https://github.com/Kosthi/CSCC-DB-Rucbase-2024/tree/Task10-StaticCheckpointRecovery) |
| 初赛  | 性能测试           | [Task11-Performance](https://github.com/Kosthi/CSCC-DB-Rucbase-2024/tree/Task11-Performance)        |
| 决赛  | TPC-C Benchmark    | [Final-Competition](https://github.com/Kosthi/CSCC-DB-Rucbase-2024/tree/Final-Competition)          |

## 实验环境：
- 操作系统：Ubuntu 18.04 及以上(64位)
- 编译器：GCC
- 编程语言：C++17
- 管理工具：cmake
- 推荐编辑器：VScode

### 依赖环境库配置：
- gcc 7.1及以上版本（要求完全支持C++17）
- cmake 3.16及以上版本
- flex
- bison
- readline

欲查看有关依赖运行库和编译工具的更多信息，以及如何运行的说明，请查阅[RMDB使用文档](docs/RMDB使用文档.pdf)

欲了解如何在非Linux系统PC上部署实验环境的指导，请查阅[RMDB环境配置文档](docs/RMDB环境配置文档.pdf)

### 项目说明文档

- [RMDB环境配置文档](docs/RMDB环境配置文档.pdf)
- [RMDB使用文档](docs/RMDB使用文档.pdf)
- [RMDB项目结构](docs/RMDB项目结构.pdf)
- [测试说明文档](测试说明文档.pdf)

## 推荐参考资料

- [**Database System Concepts** (***Seventh Edition***)](https://db-book.com/)
- [PostgreSQL 数据库内核分析](https://book.douban.com/subject/6971366//)
- [数据库系统实现](https://book.douban.com/subject/4838430/)
- [数据库系统概论(第5版)](http://chinadb.ruc.edu.cn/second/url/2)

## License
RMDB采用[木兰宽松许可证，第2版](https://license.coscl.org.cn/MulanPSL2)，可以自由拷贝和使用源码, 当做修改或分发时, 请遵守[木兰宽松许可证，第2版](https://license.coscl.org.cn/MulanPSL2).

[![Visitors](https://api.visitorbadge.io/api/visitors?path=https://github.com/Kosthi/CSCC-DB-Rucbase-2024&label=visitors&countColor=%23263759)](https://visitorbadge.io/status?path=https://github.com/Kosthi/CSCC-DB-Rucbase-2024)

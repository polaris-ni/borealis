# tests/framework — 复刻自 Aurora 的注册式测试框架

本目录是 **Aurora 主仓 `tests/framework/` 的源码复刻**（Aurora 为 MIT 许可，同一作者），
依据 `AGENTS.md` §4.4 第 18 条「测试设施复刻 Aurora 的注册式框架」：用例经宏静态注册、
`main` 由本目录唯一提供、测试文件禁止自定义 `main()`。

## 复刻口径

- **不改内容**：文件按上游原样复制，宏家族、套件名约定（`--run=<stem>`，套件名恒等于文件
  stem）、reporter 形态与上游一致，便于随上游同步而非维护一份分叉。
- **唯一外部依赖**：`value_print.h` 包含 Aurora 的 `known_enums.h`（枚举 SSOT，位于
  Aurora 的 `tools/include/`）。故 `cmake/BorealisTests.cmake` 为 runner 追加了该目录作为
  include 目录——**直接引用 SSOT，不复制副本**（复制副本会测到副本而非真值）。
- **命名空间**：框架内部沿用上游的 `aurora::testing` 一类标识，属复刻代码的既有形态，
  不受本仓「标识统一 `borealis`」约束（该约束管的是本仓自主命名的符号）。

## 同步与演进

上游框架变更时按「整目录重复制 + 跑 `framework_selftest`」同步；本仓若自行修补，须在
本文件记录偏离点，避免下次同步把修补静默冲掉。分发产物须随包携带 Aurora 的 MIT 许可声明
（`SPEC.NF.PKG.01`）。

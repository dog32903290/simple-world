// app/command — 編輯命令層（undo/redo 的地基）。
// Zone: app. 命令持有 Graph& 參照，改的是 runtime 的圖（app -> runtime）。
#pragma once
#include <memory>
#include <string>
#include <vector>

namespace sw {

struct Command {
  virtual ~Command() = default;
  virtual void doIt() = 0;            // 執行 / 重做
  virtual void undo() = 0;            // 退回
  virtual const char* name() const = 0;  // for status line / command log
};

// 把多個命令綁成一個原子動作：doIt 依序、undo 反序。給插入節點 / 複製貼上用。
class MacroCommand : public Command {
 public:
  explicit MacroCommand(std::string name) : name_(std::move(name)) {}
  void add(std::unique_ptr<Command> c) { children_.push_back(std::move(c)); }
  bool empty() const { return children_.empty(); }
  size_t size() const { return children_.size(); }
  void doIt() override;
  void undo() override;
  const char* name() const override { return name_.c_str(); }

 private:
  std::string name_;
  std::vector<std::unique_ptr<Command>> children_;
};

class CommandStack {
 public:
  void push(std::unique_ptr<Command> cmd);  // 執行 cmd，推入 undo 堆，清空 redo 堆
  void undo();                              // 退一格
  void redo();                              // 前進一格
  void clear();                             // New / Open 時呼叫
  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }
  const char* lastUndoName() const;         // 最近一個可 undo 的命令名（空堆回 ""）

 private:
  std::vector<std::unique_ptr<Command>> undo_;
  std::vector<std::unique_ptr<Command>> redo_;
};

// 全域命令堆（editor_ui 與 document 共用）。
extern CommandStack g_commands;

// 隔離自測：用 local graph 跑 add/delete/move 的 do/undo/redo，斷言圖等同預期。
// injectBug=true 時刻意在 undo 後留下偏差，斷言必須 FAIL。
int runCommandSelfTest(bool injectBug);

}  // namespace sw

// app/graph_commands — 改 sw::Graph 的具體命令。每個命令持有 Graph& 參照。
// Zone: app. 依賴 runtime/graph。
#pragma once
#include <vector>

#include "app/command.h"
#include "runtime/graph.h"

namespace sw {

// 加一個已建好（含 id）的節點。undo 用 id 移除。
class AddNodeCommand : public Command {
 public:
  AddNodeCommand(Graph& g, Node node) : g_(g), node_(std::move(node)) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Add Node"; }

 private:
  Graph& g_;
  Node node_;
};

// 加一條已建好（含 id/from/to）的連線。undo 用 id 移除。
class AddConnectionCommand : public Command {
 public:
  AddConnectionCommand(Graph& g, Connection c) : g_(g), conn_(c) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Add Connection"; }

 private:
  Graph& g_;
  Connection conn_;
};

// 刪 N 條連線。doIt 快照被刪的連線，undo 還原。
class DeleteConnectionsCommand : public Command {
 public:
  DeleteConnectionsCommand(Graph& g, std::vector<int> ids) : g_(g), ids_(std::move(ids)) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Delete Connections"; }

 private:
  Graph& g_;
  std::vector<int> ids_;                 // 要刪的連線 id
  std::vector<Connection> removed_;      // doIt 時快照，供 undo 還原
};

// 刪 N 個節點 + 它們的入射連線。doIt 快照節點與連線，undo 全部還原。
class DeleteNodesCommand : public Command {
 public:
  DeleteNodesCommand(Graph& g, std::vector<int> ids) : g_(g), ids_(std::move(ids)) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Delete Nodes"; }

 private:
  Graph& g_;
  std::vector<int> ids_;                 // 要刪的節點 id
  std::vector<Node> removedNodes_;       // doIt 時快照
  std::vector<Connection> removedConns_; // doIt 時快照（入射連線）
};

// 移動 N 個節點。記錄每個節點的舊/新座標；undo 設回舊，doIt/redo 設新。
class MoveNodesCommand : public Command {
 public:
  struct Move { int id; float oldX, oldY, newX, newY; };
  MoveNodesCommand(Graph& g, std::vector<Move> moves) : g_(g), moves_(std::move(moves)) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Move Nodes"; }

 private:
  Graph& g_;
  std::vector<Move> moves_;
};

// 改某節點某 Float input 的常數（Inspector 拉 slider）。可 undo。
class SetInputValueCommand : public Command {
 public:
  SetInputValueCommand(Graph& g, int nodeId, std::string portId, float oldV, float newV)
      : g_(g), nodeId_(nodeId), portId_(std::move(portId)), old_(oldV), new_(newV) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Set Value"; }
 private:
  Graph& g_; int nodeId_; std::string portId_; float old_, new_;
};

}  // namespace sw

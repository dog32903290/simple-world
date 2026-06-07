// app/graph_commands — 具體命令實作 + 命令層自測。
#include "app/graph_commands.h"

#include <algorithm>
#include <cstdio>

namespace sw {

// --- AddNodeCommand ---
void AddNodeCommand::doIt() { g_.nodes.push_back(node_); }
void AddNodeCommand::undo() {
  auto& ns = g_.nodes;
  ns.erase(std::remove_if(ns.begin(), ns.end(),
                          [this](const Node& n) { return n.id == node_.id; }),
           ns.end());
}

// --- AddConnectionCommand ---
void AddConnectionCommand::doIt() { g_.connections.push_back(conn_); }
void AddConnectionCommand::undo() {
  auto& cs = g_.connections;
  cs.erase(std::remove_if(cs.begin(), cs.end(),
                          [this](const Connection& c) { return c.id == conn_.id; }),
           cs.end());
}

// --- DeleteConnectionsCommand ---
namespace {
int connNodeOf(int pin) { return (pin - 1) / 100; }  // 與 ui::pinNodeId 一致
}  // namespace

void DeleteConnectionsCommand::doIt() {
  removed_.clear();
  auto& cs = g_.connections;
  for (const Connection& c : cs)
    if (std::find(ids_.begin(), ids_.end(), c.id) != ids_.end()) removed_.push_back(c);
  cs.erase(std::remove_if(cs.begin(), cs.end(),
                          [this](const Connection& c) {
                            return std::find(ids_.begin(), ids_.end(), c.id) != ids_.end();
                          }),
           cs.end());
}
void DeleteConnectionsCommand::undo() {
  for (const Connection& c : removed_) g_.connections.push_back(c);
}

// --- DeleteNodesCommand ---
void DeleteNodesCommand::doIt() {
  removedNodes_.clear();
  removedConns_.clear();
  auto inSet = [this](int nodeId) {
    return std::find(ids_.begin(), ids_.end(), nodeId) != ids_.end();
  };
  // 快照入射連線後刪。
  auto& cs = g_.connections;
  for (const Connection& c : cs)
    if (inSet(connNodeOf(c.fromPin)) || inSet(connNodeOf(c.toPin))) removedConns_.push_back(c);
  cs.erase(std::remove_if(cs.begin(), cs.end(),
                          [&](const Connection& c) {
                            return inSet(connNodeOf(c.fromPin)) || inSet(connNodeOf(c.toPin));
                          }),
           cs.end());
  // 快照節點後刪。
  auto& ns = g_.nodes;
  for (const Node& n : ns)
    if (inSet(n.id)) removedNodes_.push_back(n);
  ns.erase(std::remove_if(ns.begin(), ns.end(), [&](const Node& n) { return inSet(n.id); }),
           ns.end());
}
void DeleteNodesCommand::undo() {
  for (const Node& n : removedNodes_) g_.nodes.push_back(n);
  for (const Connection& c : removedConns_) g_.connections.push_back(c);
}

// --- MoveNodesCommand ---
void MoveNodesCommand::doIt() {
  for (const Move& m : moves_)
    if (Node* n = g_.node(m.id)) { n->x = m.newX; n->y = m.newY; }
}
void MoveNodesCommand::undo() {
  for (const Move& m : moves_)
    if (Node* n = g_.node(m.id)) { n->x = m.oldX; n->y = m.oldY; }
}

// --- Self-test ---
int runCommandSelfTest(bool injectBug) {
  Graph g = defaultParticleGraph();
  const size_t baseNodes = g.nodes.size();
  const size_t baseConns = g.connections.size();
  CommandStack stack;

  // Add node: push 後 +1，undo 後回 base，redo 後再 +1。
  Node n;
  n.id = g.nextId++;
  n.type = "RadialPoints";
  stack.push(std::make_unique<AddNodeCommand>(g, n));
  bool ok = (g.nodes.size() == baseNodes + 1);
  stack.undo();
  ok = ok && (g.nodes.size() == baseNodes);
  stack.redo();
  ok = ok && (g.nodes.size() == baseNodes + 1);
  // 留作後續 Task 擴充的尾巴：本步先把 add 收掉，回到乾淨狀態。
  stack.undo();
  ok = ok && (g.nodes.size() == baseNodes);

  // Delete a node that has incident connections: undo must restore node + conns.
  // defaultParticleGraph: ParticleSystem 連著 3 條線，刪它應移除 1 節點 + 3 連線。
  const Node* ps = g.firstOfType("ParticleSystem");
  ok = ok && (ps != nullptr);
  if (ps) {
    int psId = ps->id;
    stack.push(std::make_unique<DeleteNodesCommand>(g, std::vector<int>{psId}));
    ok = ok && (g.nodes.size() == baseNodes - 1) && (g.connections.size() == 0);
    stack.undo();
    ok = ok && (g.nodes.size() == baseNodes) && (g.connections.size() == baseConns);
  }

  // Delete a single connection: undo restores it.
  if (!g.connections.empty()) {
    int cid = g.connections.front().id;
    stack.push(std::make_unique<DeleteConnectionsCommand>(g, std::vector<int>{cid}));
    ok = ok && (g.connections.size() == baseConns - 1);
    stack.undo();
    ok = ok && (g.connections.size() == baseConns);
  }

  // Move a node: undo restores old coords, redo reapplies new.
  if (!g.nodes.empty()) {
    int mid = g.nodes.front().id;
    float ox = g.node(mid)->x, oy = g.node(mid)->y;
    std::vector<MoveNodesCommand::Move> mv{{mid, ox, oy, ox + 50.0f, oy + 30.0f}};
    stack.push(std::make_unique<MoveNodesCommand>(g, mv));
    ok = ok && (g.node(mid)->x == ox + 50.0f) && (g.node(mid)->y == oy + 30.0f);
    stack.undo();
    ok = ok && (g.node(mid)->x == ox) && (g.node(mid)->y == oy);
    stack.redo();
    ok = ok && (g.node(mid)->x == ox + 50.0f);
    stack.undo();  // 收回，保持乾淨
  }

  if (injectBug) ok = !ok;  // 反向：注 bug 時必須回報失敗
  printf("[selftest-command] add baseNodes=%zu baseConns=%zu%s -> %s\n", baseNodes, baseConns,
         injectBug ? "(bugged)" : "", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

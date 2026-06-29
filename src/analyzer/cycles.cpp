#include "cycles.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace dl::analyzer {

namespace {

struct SccCtx {
    std::unordered_map<LockId, std::vector<LockId>, LockIdHash> adj;
    std::unordered_map<LockId, int, LockIdHash> idx, low;
    std::unordered_map<LockId, bool, LockIdHash> on_stack;
    std::vector<LockId> stk;
    int counter = 0;
    std::vector<std::vector<LockId>> sccs;
};

void tarjan_visit(SccCtx& c, const LockId& v) {
    c.idx[v] = c.low[v] = c.counter++;
    c.stk.push_back(v);
    c.on_stack[v] = true;
    auto it = c.adj.find(v);
    if (it != c.adj.end()) {
        for (const auto& w : it->second) {
            if (c.idx.find(w) == c.idx.end()) {
                tarjan_visit(c, w);
                c.low[v] = std::min(c.low[v], c.low[w]);
            } else if (c.on_stack[w]) {
                c.low[v] = std::min(c.low[v], c.idx[w]);
            }
        }
    }
    if (c.low[v] == c.idx[v]) {
        std::vector<LockId> scc;
        while (true) {
            LockId w = c.stk.back(); c.stk.pop_back();
            c.on_stack[w] = false;
            scc.push_back(w);
            if (w == v) break;
        }
        c.sccs.push_back(std::move(scc));
    }
}

}  // namespace

std::vector<Cycle> find_cycles(const Graph& g, int max_per_scc) {
    SccCtx c;
    for (const auto& [key, info] : g.edges) {
        c.adj[key.from].push_back(key.to);
        if (c.adj.find(key.to) == c.adj.end()) c.adj[key.to] = {};
        (void)info;
    }
    for (auto& [node, _] : c.adj) {
        if (c.idx.find(node) == c.idx.end()) tarjan_visit(c, node);
    }

    std::vector<Cycle> out;
    for (auto& scc : c.sccs) {
        if (scc.size() < 2) continue;
        std::unordered_set<LockId, LockIdHash> in_scc(scc.begin(), scc.end());
        int emitted = 0;
        for (const auto& start : scc) {
            if (emitted >= max_per_scc) break;
            std::unordered_map<LockId, bool, LockIdHash> on_path;
            std::vector<LockId> path;
            std::function<bool(const LockId&)> dfs = [&](const LockId& u) -> bool {
                on_path[u] = true;
                path.push_back(u);
                auto it = c.adj.find(u);
                if (it != c.adj.end()) {
                    for (const auto& v : it->second) {
                        if (in_scc.find(v) == in_scc.end()) continue;
                        if (v == start && path.size() >= 2) {
                            Cycle cy;
                            for (size_t i = 0; i < path.size(); ++i) {
                                LockId from = path[i];
                                LockId to   = (i + 1 < path.size()) ? path[i + 1] : start;
                                auto eit = g.edges.find(EdgeKey{from, to});
                                if (eit == g.edges.end()) { cy.edges.clear(); break; }
                                cy.edges.push_back({from, to, &eit->second});
                            }
                            if (!cy.edges.empty()) {
                                out.push_back(std::move(cy));
                                if (++emitted >= max_per_scc) return true;
                            }
                        } else if (!on_path[v]) {
                            if (dfs(v)) return true;
                        }
                    }
                }
                on_path[u] = false;
                path.pop_back();
                return false;
            };
            dfs(start);
        }
    }

    // 去重（按旋转归一化）
    auto canon = [](Cycle& cy) {
        if (cy.edges.empty()) return;
        size_t mi = 0;
        LockIdHash h;
        for (size_t i = 1; i < cy.edges.size(); ++i)
            if (h(cy.edges[i].from) < h(cy.edges[mi].from)) mi = i;
        std::rotate(cy.edges.begin(), cy.edges.begin() + mi, cy.edges.end());
    };
    for (auto& cy : out) canon(cy);
    std::sort(out.begin(), out.end(), [](const Cycle& a, const Cycle& b){
        if (a.edges.size() != b.edges.size()) return a.edges.size() < b.edges.size();
        LockIdHash h;
        for (size_t i = 0; i < a.edges.size(); ++i) {
            if (!(a.edges[i].from == b.edges[i].from))
                return h(a.edges[i].from) < h(b.edges[i].from);
        }
        return false;
    });
    out.erase(std::unique(out.begin(), out.end(), [](const Cycle& a, const Cycle& b){
        if (a.edges.size() != b.edges.size()) return false;
        for (size_t i = 0; i < a.edges.size(); ++i) {
            if (!(a.edges[i].from == b.edges[i].from) || !(a.edges[i].to == b.edges[i].to))
                return false;
        }
        return true;
    }), out.end());
    return out;
}

}  // namespace dl::analyzer

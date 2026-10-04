#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace agi::browser {
// UI-thread owned native identities. Never supplied by renderer/client input.
class Lifecycle {
 public:
  using Id = std::string;
  struct Tab { Id id, window, profile, opener; int engine = 0; bool closing = false, invalidated = false; uint64_t deadline = 0; };
  struct Window { Id id, active; std::vector<Id> tabs; };
  using Invalidate = std::function<void(const Id&, const Id&)>;
  explicit Lifecycle(Invalidate invalidate = {}) : invalidate_(std::move(invalidate)) {}
  Id NewWindow();
  Id CreateTab(const Id& window, const Id& profile, const Id& opener = {}, uint64_t deadline = 0);
  bool Bind(const Id& tab, int engine);
  bool Activate(const Id& tab);
  bool Reorder(const Id& tab, size_t index);
  bool Move(const Id& tab, const Id& window, size_t index);
  bool BeginClose(const Id& tab);
  // Canceling a human beforeunload dialog never reinstates scope/documents.
  bool CancelClose(const Id& tab);
  bool FinishClose(const Id& tab);
  std::vector<Id> PendingExpired(uint64_t now) const;
  std::vector<Id> PendingFrom(const Id& opener) const;
  const Tab* FindTab(const Id& tab) const;
  bool RemoveWindow(const Id& window);
  const Tab* Resolve(const Id& tab) const;
  const Tab* ForEngine(int engine) const;
  const Window* LookupWindow(const Id& window) const;
  size_t tab_count() const { return tabs_.size(); }
 private:
  Id Next(char kind);
  void Detach(const Tab& tab);
  uint64_t sequence_ = 0;
  std::map<Id, Tab> tabs_;
  std::map<Id, Window> windows_;
  std::map<int, Id> engines_;
  Invalidate invalidate_;
};
}  // namespace agi::browser

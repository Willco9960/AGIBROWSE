#include "lifecycle.h"
#include <algorithm>
#include <limits>
namespace agi::browser {
Lifecycle::Id Lifecycle::Next(char kind) {
  if (sequence_ == std::numeric_limits<uint64_t>::max()) return {};
  return std::string(1, kind) + std::to_string(++sequence_);
}
Lifecycle::Id Lifecycle::NewWindow() {
  if (windows_.size() >= 32) return {};
  auto id = Next('w'); if (!id.empty()) windows_.emplace(id, Window{id, {}, {}});
  return id;
}
Lifecycle::Id Lifecycle::CreateTab(const Id& window, const Id& profile, const Id& opener, uint64_t deadline) {
  auto w = windows_.find(window);
  if (w == windows_.end() || tabs_.size() >= 64 || profile.empty() || profile.size() > 128) return {};
  if (!opener.empty()) { auto p = Resolve(opener); if (!p || p->profile != profile) return {}; }
  auto id = Next('t'); if (id.empty()) return {};
  auto order=w->second.tabs; order.push_back(id); auto active=id;
  tabs_.emplace(id, Tab{id, window, profile, opener, 0, false, false, deadline});
  w->second.tabs.swap(order); w->second.active.swap(active); return id;
}
bool Lifecycle::Bind(const Id& id, int engine) {
  auto t = tabs_.find(id);
  if (t == tabs_.end() || t->second.closing || t->second.engine || engine <= 0 || engines_.contains(engine)) return false;
  engines_.emplace(engine, id); t->second.engine = engine; t->second.deadline = 0; return true;
}
const Lifecycle::Tab* Lifecycle::FindTab(const Id& id) const {
  auto t = tabs_.find(id); return t == tabs_.end() ? nullptr : &t->second;
}
const Lifecycle::Tab* Lifecycle::Resolve(const Id& id) const {
  auto t = tabs_.find(id); return t == tabs_.end() || t->second.closing ? nullptr : &t->second;
}
const Lifecycle::Tab* Lifecycle::ForEngine(int engine) const {
  auto e = engines_.find(engine); return e == engines_.end() ? nullptr : Resolve(e->second);
}
const Lifecycle::Window* Lifecycle::LookupWindow(const Id& id) const {
  auto w = windows_.find(id); return w == windows_.end() ? nullptr : &w->second;
}
bool Lifecycle::Activate(const Id& id) {
  auto t = Resolve(id); if (!t) return false; windows_.at(t->window).active = id; return true;
}
bool Lifecycle::Reorder(const Id& id, size_t index) {
  auto t = Resolve(id); if (!t) return false;
  auto& current = windows_.at(t->window).tabs; if (index >= current.size()) return false;
  auto order=current;order.erase(std::find(order.begin(), order.end(), id)); order.insert(order.begin() + index, id);
  current.swap(order);return true;
}
void Lifecycle::Detach(const Tab& t) {
  auto& w = windows_.at(t.window);
  w.tabs.erase(std::find(w.tabs.begin(), w.tabs.end(), t.id));
  if (w.active == t.id) { w.active.clear(); for (const auto& id : w.tabs) if (Resolve(id)) { w.active = id; break; } }
}
bool Lifecycle::Move(const Id& id, const Id& window, size_t index) {
  auto t = tabs_.find(id); auto w = windows_.find(window);
  if (t == tabs_.end() || t->second.closing || w == windows_.end()) return false;
  if (t->second.window == window) return Reorder(id, index);
  if (index > w->second.tabs.size()) return false;
  auto& source=windows_.at(t->second.window);
  auto source_order=source.tabs,target_order=w->second.tabs;
  source_order.erase(std::find(source_order.begin(),source_order.end(),id));target_order.insert(target_order.begin()+index,id);
  auto source_active=source.active,target_active=id,new_window=window;
  if(source_active==id){source_active.clear();for(const auto& other:source_order)if(Resolve(other)){source_active=other;break;}}
  source.tabs.swap(source_order);source.active.swap(source_active);
  w->second.tabs.swap(target_order);w->second.active.swap(target_active);t->second.window.swap(new_window);return true;
}
bool Lifecycle::BeginClose(const Id& id) {
  auto t = tabs_.find(id); if (t == tabs_.end()) return false;
  if (t->second.closing && t->second.invalidated) return true;
  t->second.closing = true;
  // A throwing barrier cannot escape an engine callback or revive this handle.
  // It can be retried; the caller must not request engine close on failure.
  try { if (invalidate_) invalidate_(t->second.profile, id); }
  catch (...) { return false; }
  t->second.invalidated = true;
  auto& w = windows_.at(t->second.window);
  if (w.active == id) { w.active.clear(); for (const auto& other : w.tabs) if (Resolve(other)) { w.active = other; break; } }
  return true;
}
bool Lifecycle::CancelClose(const Id& id) {
  auto t = tabs_.find(id);
  if (t == tabs_.end() || !t->second.closing || !t->second.invalidated || !t->second.engine) return false;
  t->second.closing = false; t->second.invalidated = false; windows_.at(t->second.window).active = id; return true;
}
bool Lifecycle::FinishClose(const Id& id) {
  auto t = tabs_.find(id); if (t == tabs_.end()) return false;
  if (!BeginClose(id)) return false;
  Detach(t->second); engines_.erase(t->second.engine); tabs_.erase(t); return true;
}
std::vector<Lifecycle::Id> Lifecycle::PendingExpired(uint64_t now) const {
  std::vector<Id> result;
  for (const auto& [id,t] : tabs_) if (!t.engine && t.deadline && now >= t.deadline) result.push_back(id);
  return result;
}
std::vector<Lifecycle::Id> Lifecycle::PendingFrom(const Id& opener) const {
  std::vector<Id> result;
  for (const auto& [id,t] : tabs_) if (!t.engine && t.opener == opener) result.push_back(id);
  return result;
}
bool Lifecycle::RemoveWindow(const Id& id) {
  auto w = windows_.find(id); if (w == windows_.end() || !w->second.tabs.empty()) return false;
  windows_.erase(w); return true;
}
}

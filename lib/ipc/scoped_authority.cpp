#include "lib/ipc/scoped_authority.h"
#include <algorithm>

namespace agi::ipc {
namespace {
template<class T> bool Contains(const std::vector<T>& values, const T& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}
template<class T> bool Unique(const std::vector<T>& values) {
  for (size_t i = 0; i < values.size(); ++i)
    if (std::find(values.begin(), values.begin() + i, values[i]) != values.begin() + i) return false;
  return true;
}
bool DestinationValid(const Destination& d) {
  return IsIdentifier(d.profile) && IsIdentifier(d.tab) && IsIdentifier(d.frame) && IsIdentifier(d.document);
}
bool Read(const std::string& op) { return op == "observe" || op == "wait"; }
bool BlankOperation(const std::string& op) {
  return Read(op) || op == "focus_tab" || op == "close_tab" || op == "navigate" || op == "open_tab";
}
}
ScopedAuthority::ScopedAuthority(OriginValidator validator, Clock clock)
    : origin_validator_(std::move(validator)), clock_(std::move(clock)) {}
void ScopedAuthority::ExpireSessions(uint64_t now) {
  for (auto i = sessions_.begin(); i != sessions_.end();) {
    if (now >= i->second.transport_deadline) { Purge(i->first); i = sessions_.erase(i); }
    else ++i;
  }
}
bool ScopedAuthority::BindAuthenticatedSession(const std::string& client, const std::string& session, uint64_t transport_deadline) {
  std::lock_guard lock(mutex_);
  if (!live_ || !clock_ || !IsIdentifier(client) || !IsIdentifier(session)) return false;
  const auto now = clock_(); ExpireSessions(now);
  const auto active = std::count_if(sessions_.begin(), sessions_.end(), [](const auto& entry) { return entry.second.live; });
  if (active >= 64 || sessions_.size() >= 4096 || sessions_.contains(session) || now > UINT64_MAX - 60000) return false;
  if(!transport_deadline)transport_deadline = now + 60000;
  if(transport_deadline <= now || transport_deadline > now + 60000)return false;
  Session value; value.client = client; value.transport_deadline = transport_deadline;
  sessions_.emplace(session, std::move(value)); return true;
}
bool ScopedAuthority::IsBoundAuthenticatedSession(const std::string& client, const std::string& session) {
  std::lock_guard lock(mutex_);
  const auto s = sessions_.find(session);
  return live_ && clock_ && s != sessions_.end() && s->second.live &&
         s->second.client == client && clock_() < s->second.transport_deadline;
}
bool ScopedAuthority::ApproveNativeGrant(const ScopedGrant& g, uint64_t now) {
  std::lock_guard lock(mutex_);
  auto found = sessions_.find(g.session);
  if (!live_ || found == sessions_.end() || !found->second.live || now >= found->second.transport_deadline || found->second.client != g.client ||
      g.version != 1 || !IsIdentifier(g.grant_id) || !IsIdentifier(g.profile) ||
      !g.generation || g.generation <= found->second.generation ||
      !g.expires_after_ms || g.expires_after_ms > 28800000 || now > UINT64_MAX - g.expires_after_ms ||
      g.tabs.empty() || g.tabs.size() > 64 || !Unique(g.tabs) ||
      g.operations.empty() || g.operations.size() > 21 || !Unique(g.operations) ||
      g.contexts.empty() || g.contexts.size() > 32 || !Unique(g.contexts)) return false;
  for (const auto& tab : g.tabs) if (!IsIdentifier(tab)) return false;
  for (const auto& op : g.operations) if (!IsOperation(op) || (!g.control && !Read(op))) return false;
  for (const auto& context : g.contexts) {
    if (context.frames != FramePolicy::top && context.frames != FramePolicy::same_origin_descendants) return false;
    if (context.kind == ContextKind::host_blank) {
      if (!context.origin.empty() || context.frames != FramePolicy::top) return false;
    } else if (context.kind != ContextKind::origin || context.origin.size() > 2048 ||
               !origin_validator_ || !origin_validator_(context.origin)) return false;
  }
  Purge(g.session);
  auto& session = found->second; session.grant = g; session.generation = g.generation;
  session.deadline = now + g.expires_after_ms; return true;
}
bool ScopedAuthority::RegisterNativeDocument(const ScopedDocument& document) {
  std::lock_guard lock(mutex_);
  if (!live_ || !DestinationValid(document.destination) || revision_ == UINT64_MAX) return false;
  if (document.kind == ContextKind::host_blank) {
    if (!document.top || !document.canonical_origin.empty()) return false;
  } else if (document.kind != ContextKind::origin || document.canonical_origin.size() > 2048 ||
             !origin_validator_ || !origin_validator_(document.canonical_origin)) return false;
  const auto& d = document.destination; Key key{d.profile, d.tab, d.frame};
  if (!documents_.contains(key) && documents_.size() >= 256) return false;
  documents_[key] = {document, ++revision_}; return true;
}
void ScopedAuthority::RemoveNativeDocument(const Destination& d) {
  std::lock_guard lock(mutex_); documents_.erase({d.profile, d.tab, d.frame});
}
bool ScopedAuthority::Current(const ScopeRequest& r, uint64_t now, Pending* stamp) const {
  if (!live_ || !DestinationValid(r.destination) || !IsOperation(r.operation)) return false;
  auto s = sessions_.find(r.session);
  if (s == sessions_.end() || !s->second.live || s->second.client != r.client || !s->second.deadline || now >= s->second.deadline || now >= s->second.transport_deadline) return false;
  const auto& session = s->second; const auto& g = session.grant; const auto& d = r.destination;
  if (g.profile != d.profile || !Contains(g.tabs, d.tab) || !Contains(g.operations, r.operation)) return false;
  auto document = documents_.find({d.profile, d.tab, d.frame});
  if (document == documents_.end() || document->second.value.destination.document != d.document) return false;
  const auto& observed = document->second.value;
  const bool context = std::any_of(g.contexts.begin(), g.contexts.end(), [&](const auto& c) {
    return c.kind == observed.kind && c.origin == observed.canonical_origin &&
           (observed.top || c.frames == FramePolicy::same_origin_descendants);
  });
  if (!context || (observed.kind == ContextKind::host_blank && !BlankOperation(r.operation))) return false;
  uint64_t lease_revision = 0;
  if (!Read(r.operation)) {
    auto lease = leases_.find({d.profile, d.tab});
    if (!g.control || lease == leases_.end() || lease->second.session != r.session) return false;
    lease_revision = lease->second.revision;
  }
  if (stamp) *stamp = {r, session.generation, document->second.revision, lease_revision};
  return true;
}
bool ScopedAuthority::AcquireNativeLease(const std::string& client, const std::string& session,
                                         const std::string& tab, uint64_t now) {
  std::lock_guard lock(mutex_); auto s = sessions_.find(session);
  if (!live_ || s == sessions_.end() || !s->second.live || s->second.client != client ||
      !s->second.grant.control || !s->second.deadline || now >= s->second.deadline || now >= s->second.transport_deadline ||
      !Contains(s->second.grant.tabs, tab) || revision_ == UINT64_MAX) return false;
  const auto key = std::make_pair(s->second.grant.profile, tab); auto l = leases_.find(key);
  if (l != leases_.end()) {
    if (l->second.session == session) return true;
    auto owner = sessions_.find(l->second.session);
    if (owner != sessions_.end() && owner->second.live && now < owner->second.deadline &&
        now < owner->second.transport_deadline) return false;
  }
  leases_[key] = {session, ++revision_}; return true;
}
void ScopedAuthority::ReleaseNativeLease(const std::string& client, const std::string& session, const std::string& tab) {
  std::lock_guard lock(mutex_); auto s = sessions_.find(session);
  if (s == sessions_.end() || s->second.client != client) return;
  auto l = leases_.find({s->second.grant.profile, tab});
  if (l != leases_.end() && l->second.session == session) leases_.erase(l);
}
uint64_t ScopedAuthority::QueueScopeCheck(const ScopeRequest& request, uint64_t now) {
  std::lock_guard lock(mutex_); Pending stamp;
  if (pending_.size() >= 128 || ticket_ == UINT64_MAX || !Current(request, now, &stamp)) return 0;
  const auto ticket = ++ticket_; pending_.emplace(ticket, std::move(stamp)); return ticket;
}
bool ScopedAuthority::WithCurrentScope(uint64_t ticket, const std::function<void()>& handoff) {
  std::lock_guard lock(mutex_); auto p = pending_.find(ticket);
  if (p == pending_.end()) return false;
  const auto original = p->second; pending_.erase(p); Pending current;
  if (!handoff || !clock_ || !Current(original.request, clock_(), &current) || current.generation != original.generation ||
      current.document_revision != original.document_revision || current.lease_revision != original.lease_revision) return false;
  handoff(); return true;
}
void ScopedAuthority::Purge(const std::string& session) {
  std::erase_if(pending_, [&](const auto& entry) { return entry.second.request.session == session; });
  std::erase_if(leases_, [&](const auto& entry) { return entry.second.session == session; });
}
void ScopedAuthority::RevokeNativeSession(const std::string& session) {
  std::lock_guard lock(mutex_); auto s = sessions_.find(session);
  if (s != sessions_.end()) { Purge(session); s->second.deadline = 0; s->second.grant = {}; }
}
bool ScopedAuthority::DisconnectAuthenticatedSession(const std::string& client, const std::string& session) {
  std::lock_guard lock(mutex_); auto s = sessions_.find(session);
  if (s == sessions_.end() || s->second.client != client) return false;
  Purge(session); s->second.live = false; s->second.deadline = 0; s->second.grant = {}; return true;
}
void ScopedAuthority::RevokeNativeClient(const std::string& client) {
  std::lock_guard lock(mutex_);
  for (auto& [id, session] : sessions_) if (session.client == client) {
    Purge(id); session.live = false; session.deadline = 0; session.grant = {};
  }
}
void ScopedAuthority::Invalidate() {
  std::lock_guard lock(mutex_); live_ = false; pending_.clear(); leases_.clear(); documents_.clear(); sessions_.clear();
}
}  // namespace agi::ipc

#pragma once
#include "lib/ipc/protocol.h"
#include <functional>
#include <mutex>
#include <tuple>

namespace agi::ipc {
enum class ContextKind { origin, host_blank };
enum class FramePolicy { top, same_origin_descendants };
struct ScopeContext {
  ContextKind kind = ContextKind::origin;
  std::string origin;
  FramePolicy frames = FramePolicy::top;
  bool operator==(const ScopeContext&) const = default;
};
struct ScopedGrant {
  uint8_t version = 1;
  std::string grant_id, client, session, profile;
  std::vector<std::string> tabs, operations;
  std::vector<ScopeContext> contexts;
  bool control = false;
  uint64_t generation = 0, expires_after_ms = 0;
};
// Native engine provenance only: never accept this record from a website/client.
struct ScopedDocument {
  Destination destination;
  ContextKind kind = ContextKind::origin;
  std::string canonical_origin;
  bool top = true;
};
struct ScopeRequest {
  std::string client, session, operation;
  Destination destination;
};
// Host-owned scope gate, not an engine dispatcher or a serialized capability.
// All state changes and callbacks serialize under one mutex. Callbacks must be
// bounded, must not reenter, and must perform the actual handoff before return.
class ScopedAuthority {
 public:
  // Mandatory trusted browser-parser predicate: true only for an already
  // canonical HTTP(S) origin. No raw URL parsing or origin repair occurs here.
  using OriginValidator = std::function<bool(const std::string&)>;
  using Clock = std::function<uint64_t()>;
  explicit ScopedAuthority(OriginValidator validator, Clock clock);
  bool BindAuthenticatedSession(const std::string& client, const std::string& session, uint64_t transport_deadline = 0);
  bool IsBoundAuthenticatedSession(const std::string& client, const std::string& session);
  bool ApproveNativeGrant(const ScopedGrant& grant, uint64_t now_ms);
  bool RegisterNativeDocument(const ScopedDocument& document);
  void RemoveNativeDocument(const Destination& destination);
  bool AcquireNativeLease(const std::string& client, const std::string& session,
                          const std::string& tab, uint64_t now_ms);
  void ReleaseNativeLease(const std::string& client, const std::string& session,
                          const std::string& tab);
  // Opaque, bounded, host-internal queue tickets; zero means denied.
  uint64_t QueueScopeCheck(const ScopeRequest& request, uint64_t now_ms);
  // Consume once even on denial. Checks scope at the synchronous handoff.
  // Caller still owes target/actionability/sensitive-resource checks.
  bool WithCurrentScope(uint64_t ticket, const std::function<void()>& handoff);
  void RevokeNativeSession(const std::string& session);
  bool DisconnectAuthenticatedSession(const std::string& client, const std::string& session);
  void RevokeNativeClient(const std::string& client);
  void Invalidate();
 private:
  using Key = std::tuple<std::string, std::string, std::string>;
  struct Session { std::string client; bool live = true; uint64_t generation = 0, deadline = 0, transport_deadline = 0; ScopedGrant grant; };
  struct Document { ScopedDocument value; uint64_t revision; };
  struct Lease { std::string session; uint64_t revision; };
  struct Pending { ScopeRequest request; uint64_t generation, document_revision, lease_revision; };
  bool Current(const ScopeRequest& request, uint64_t now_ms, Pending* stamp) const;
  void Purge(const std::string& session);
  void ExpireSessions(uint64_t now);
  std::mutex mutex_;
  OriginValidator origin_validator_;
  Clock clock_;
  bool live_ = true;
  uint64_t revision_ = 0, ticket_ = 0;
  std::map<std::string, Session> sessions_;
  std::map<Key, Document> documents_;
  std::map<std::pair<std::string, std::string>, Lease> leases_;
  std::map<uint64_t, Pending> pending_;
};
}  // namespace agi::ipc

#include "lib/ipc/scoped_authority.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>
using namespace agi::ipc;
namespace {
unsigned checks = 0;
void Check(bool ok, const char* name) {
  ++checks; if (!ok) throw std::runtime_error(name);
  std::cout << "PASS " << name << "\n";
}
// Deliberately a closed test oracle, not a production URL parser.
bool Origin(const std::string& origin) {
  return origin == "https://example.test" || origin == "https://other.test" ||
         origin == "http://example.test" || origin == "https://example.test:8443" ||
         origin == "https://sub.example.test" || origin == "https://example.test.";
}
struct Fixture {
  uint64_t now = 100;
  ScopedAuthority authority{Origin, [this] { return now; }};
  Destination doc{"agent", "tab", "top", "doc"};
  ScopedGrant grant{1, "grant", "client", "session", "agent", {"tab"}, {"observe", "wait"},
                    {{ContextKind::origin, "https://example.test", FramePolicy::top}}, false, 1, 1000};
  Fixture() {
    Check(authority.BindAuthenticatedSession("client", "session"), "authenticated native binding");
    Check(authority.RegisterNativeDocument({doc, ContextKind::origin, "https://example.test", true}), "native document");
  }
  ScopeRequest Request(std::string operation = "observe") { return {"client", "session", operation, doc}; }
  bool Deliver(const ScopeRequest& r) {
    const auto ticket = authority.QueueScopeCheck(r, now); bool delivered = false;
    return ticket && authority.WithCurrentScope(ticket, [&] { delivered = true; }) && delivered;
  }
  void Approve() { Check(authority.ApproveNativeGrant(grant, now), "native grant installed"); }
};
void Basics() {
  Fixture f;
  Check(!f.Deliver(f.Request()), "pairing alone grants zero access"); f.Approve();
  Check(f.Deliver(f.Request()), "approved observation delivered");
  Check(f.Deliver(f.Request("wait")), "approved wait delivered");
  for (auto field : {0, 1, 2, 3, 4, 5}) {
    auto r = f.Request();
    if (field == 0) r.client = "other";
    if (field == 1) r.session = "other";
    if (field == 2) r.destination.profile = "human";
    if (field == 3) r.destination.tab = "other";
    if (field == 4) r.destination.frame = "other";
    if (field == 5) r.destination.document = "old";
    Check(!f.Deliver(r), "cross identity or destination read rejected");
    r.operation = "click"; Check(!f.Deliver(r), "cross identity or destination write rejected");
  }
  for (const auto& op : {"click", "execute_js", "approve", "cdp", "shell"})
    Check(!f.Deliver(f.Request(op)), "observe mode and unknown operations rejected");
  auto invalid = [&](const auto& mutate) { auto g = f.grant; g.generation = 2; mutate(g); Check(!f.authority.ApproveNativeGrant(g, f.now), "invalid native grant rejected"); };
  invalid([](auto& g) { g.version = 2; }); invalid([](auto& g) { g.client = "other"; });
  invalid([](auto& g) { g.grant_id = "*"; }); invalid([](auto& g) { g.profile = "*"; });
  invalid([](auto& g) { g.tabs = {}; }); invalid([](auto& g) { g.tabs = {"tab", "tab"}; });
  invalid([](auto& g) { g.operations = {}; }); invalid([](auto& g) { g.operations = {"observe", "observe"}; });
  invalid([](auto& g) { g.operations = {"click"}; }); invalid([](auto& g) { g.operations = {"approve"}; });
  invalid([](auto& g) { g.contexts = {}; }); invalid([](auto& g) { g.contexts.push_back(g.contexts[0]); });
  invalid([](auto& g) { g.expires_after_ms = 0; }); invalid([](auto& g) { g.expires_after_ms = 28800001; });
  invalid([](auto& g) { g.generation = 1; }); invalid([](auto& g) { g.contexts[0].frames = static_cast<FramePolicy>(7); });
  invalid([](auto& g) { g.contexts[0].kind = static_cast<ContextKind>(7); });
  for (const auto& origin : {"https://*.example.test", "https://example.test/path", "null", "about:blank", "https://example.test@other.test", "https://example.test\\evil", "https://EXAMPLE.test"})
    invalid([&](auto& g) { g.contexts[0].origin = origin; });
  f.now = 1100; Check(!f.Deliver(f.Request()), "monotonic grant expiration exact boundary");
}
void Contexts() {
  Fixture f; f.Approve();
  for (const auto& origin : {"https://other.test", "http://example.test", "https://example.test:8443", "https://sub.example.test", "https://example.test."}) {
    Check(f.authority.RegisterNativeDocument({f.doc, ContextKind::origin, origin, true}), "other canonical context registered");
    Check(!f.Deliver(f.Request()), "exact origin equality denies alternate origin");
  }
  f.doc.frame = "child";
  Check(f.authority.RegisterNativeDocument({f.doc, ContextKind::origin, "https://example.test", false}), "same origin child registered");
  Check(!f.Deliver(f.Request()), "top only excludes descendant");
  f.grant.generation = 2; f.grant.contexts[0].frames = FramePolicy::same_origin_descendants; f.Approve();
  Check(f.Deliver(f.Request()), "explicit same origin descendants allowed");
  Check(f.authority.RegisterNativeDocument({f.doc, ContextKind::origin, "https://other.test", false}), "cross origin child registered");
  Check(!f.Deliver(f.Request()), "parent grant excludes cross origin child");
  f.grant.generation = 3; f.grant.contexts.push_back({ContextKind::origin, "https://other.test", FramePolicy::same_origin_descendants}); f.Approve();
  Check(f.Deliver(f.Request()), "explicit exact child origin allowed");
  Check(!f.authority.RegisterNativeDocument({f.doc, ContextKind::origin, "null", false}), "opaque frame denied");
  Fixture blank;
  Check(blank.authority.RegisterNativeDocument({blank.doc, ContextKind::host_blank, "", true}), "host blank native marker");
  blank.grant.contexts = {{ContextKind::host_blank, "", FramePolicy::top}}; blank.grant.control = true;
  blank.grant.operations = {"observe", "click", "focus_tab"}; blank.Approve();
  Check(blank.authority.AcquireNativeLease("client", "session", "tab", blank.now), "blank control lease");
  Check(blank.Deliver(blank.Request()), "approved blank observe");
  Check(blank.Deliver(blank.Request("focus_tab")), "approved blank focus");
  Check(!blank.Deliver(blank.Request("click")), "blank exposes no DOM actions");
  Check(!blank.authority.RegisterNativeDocument({blank.doc, ContextKind::host_blank, "https://example.test", true}), "blank forbids origin");
}
void Races() {
  Fixture f; f.grant.control = true; f.grant.operations.push_back("click"); f.Approve();
  Check(!f.Deliver(f.Request("click")), "control needs lease");
  Check(f.authority.AcquireNativeLease("client", "session", "tab", f.now), "native lease acquired");
  Check(f.Deliver(f.Request("click")), "leased control handoff");
  Check(f.authority.BindAuthenticatedSession("other", "other-session"), "second native session");
  auto other = f.grant; other.client = "other"; other.session = "other-session";
  Check(f.authority.ApproveNativeGrant(other, f.now), "second session grant");
  Check(!f.authority.AcquireNativeLease("other", "other-session", "tab", f.now), "exclusive tab lease");
  auto deniedAfter = [&](std::string op, const auto& mutate) {
    auto ticket = f.authority.QueueScopeCheck(f.Request(op), f.now); Check(ticket != 0, "ticket admitted");
    mutate(); bool delivered = false;
    Check(!f.authority.WithCurrentScope(ticket, [&] { delivered = true; }) && !delivered, "changed authority blocks actual handoff");
  };
  deniedAfter("click", [&] { f.authority.ReleaseNativeLease("client", "session", "tab"); });
  Check(f.authority.AcquireNativeLease("client", "session", "tab", f.now), "lease reacquired");
  deniedAfter("click", [&] { f.authority.ReleaseNativeLease("client", "session", "tab"); f.authority.AcquireNativeLease("client", "session", "tab", f.now); });
  deniedAfter("observe", [&] { f.authority.RegisterNativeDocument({f.doc, ContextKind::origin, "https://example.test", true}); });
  deniedAfter("observe", [&] { ++f.grant.generation; f.Approve(); });
  deniedAfter("observe", [&] { f.now += f.grant.expires_after_ms; });
  ++f.grant.generation; f.Approve();
  deniedAfter("observe", [&] { f.authority.RevokeNativeSession("session"); });
  Check(!f.Deliver(f.Request()), "revoked grant blocks subsequent observation");
  ++f.grant.generation; f.Approve();
  Check(f.authority.AcquireNativeLease("client", "session", "tab", f.now), "lease after fresh native approval");
  deniedAfter("click", [&] { f.authority.RevokeNativeClient("client"); });
  Check(!f.authority.ApproveNativeGrant(f.grant, f.now), "client revocation tombstones session");
  Check(!f.Deliver(f.Request()), "client revoke blocks observations");
}
void BoundsAndLifecycle() {
  Fixture f; f.Approve(); std::vector<uint64_t> tickets;
  for (unsigned i = 0; i < 128; ++i) tickets.push_back(f.authority.QueueScopeCheck(f.Request(), f.now));
  Check(tickets.back() != 0 && !f.authority.QueueScopeCheck(f.Request(), f.now), "queue bounded at 128");
  Check(f.authority.WithCurrentScope(tickets[0], [] {}), "ticket consumed at handoff");
  Check(!f.authority.WithCurrentScope(tickets[0], [] {}), "ticket replay denied");
  Check(!f.authority.DisconnectAuthenticatedSession("other", "session"), "wrong client disconnect rejected");
  Check(f.authority.DisconnectAuthenticatedSession("client", "session"), "own authenticated disconnect");
  Check(!f.authority.BindAuthenticatedSession("client", "session"), "closed session reuse denied within transport window");
  Check(!f.authority.WithCurrentScope(tickets[1], [] {}), "disconnect purges queued delivery");
  for (unsigned i = 0; i < 100; ++i) {
    const auto id = "fresh-" + std::to_string(i);
    Check(f.authority.BindAuthenticatedSession("client", id), "more than 64 sequential sessions supported");
    Check(f.authority.DisconnectAuthenticatedSession("client", id), "churn session closed");
  }
  Check(f.authority.BindAuthenticatedSession("client", "reconnect"), "fresh reconnect binds");
  auto r = f.Request(); r.session = "reconnect"; Check(!f.Deliver(r), "reconnect has zero grants");
  f.now = 60100;
  Check(!f.authority.IsBoundAuthenticatedSession("client", "reconnect"), "expired live binding rejected");
  Check(f.authority.BindAuthenticatedSession("client", "after-expiry"), "expired live and tombstone entries pruned");
  f.authority.Invalidate(); Check(!f.authority.BindAuthenticatedSession("client", "restart"), "channel invalidation cannot rebind");
  ScopedAuthority noClock{Origin, {}}; Check(!noClock.BindAuthenticatedSession("client", "session"), "missing trusted clock fails closed");
  Fixture throwing; throwing.Approve(); const auto ticket = throwing.authority.QueueScopeCheck(throwing.Request(), throwing.now);
  try { throwing.authority.WithCurrentScope(ticket, [] { throw std::runtime_error("handoff"); }); } catch (const std::runtime_error&) {}
  Check(!throwing.authority.WithCurrentScope(ticket, [] {}), "throwing handoff still consumes ticket");
  Check(throwing.Deliver(throwing.Request()), "throwing callback releases mutex");
  uint64_t sampled = 100;
  ScopedAuthority originalDeadline{Origin,[&] { return sampled; }};
  Check(originalDeadline.BindAuthenticatedSession("client","session",150),"original transport deadline installed");
  sampled = 150; Check(!originalDeadline.IsBoundAuthenticatedSession("client","session"),"binding cannot extend original transport deadline");
  Check(!originalDeadline.BindAuthenticatedSession("client","too-long",60151),"extended transport deadline denied");
  uint64_t lease_now = 100;ScopedAuthority expiringLease{Origin,[&] { return lease_now; }};
  Check(expiringLease.BindAuthenticatedSession("older","old",150)&&
        expiringLease.BindAuthenticatedSession("younger","young",250),"overlapping unequal transport lifetimes");
  ScopedGrant oldGrant{1,"old-grant","older","old","agent",{"tab"},{"click"},
                       {{ContextKind::origin,"https://example.test",FramePolicy::top}},true,1,1000};
  auto youngGrant=oldGrant;youngGrant.grant_id="young-grant";youngGrant.client="younger";youngGrant.session="young";
  Check(expiringLease.ApproveNativeGrant(oldGrant,lease_now)&&expiringLease.ApproveNativeGrant(youngGrant,lease_now),"grants outlast original transport bindings");
  Destination leaseDoc{"agent","tab","top","doc"};
  Check(expiringLease.RegisterNativeDocument({leaseDoc,ContextKind::origin,"https://example.test",true}),"lease expiry native document");
  Check(expiringLease.AcquireNativeLease("older","old","tab",lease_now),"older live transport initially owns lease");
  lease_now=150;
  Check(expiringLease.AcquireNativeLease("younger","young","tab",lease_now),"younger live session acquires expired transport owner lease without new binding");
  Check(!expiringLease.QueueScopeCheck({"older","old","click",leaseDoc},lease_now),"expired transport owner cannot act despite unexpired grant");
  const auto youngTicket=expiringLease.QueueScopeCheck({"younger","young","click",leaseDoc},lease_now);
  Check(youngTicket!=0,"younger current leased ticket admitted");
  Check(expiringLease.WithCurrentScope(youngTicket,[] {}),"younger current lease handoff allowed");
  Fixture serialized;serialized.Approve();const auto serial_ticket=serialized.authority.QueueScopeCheck(serialized.Request(),serialized.now);
  std::atomic<bool> entered=false,revoked=false;
  std::thread revoker([&] {
    while(!entered.load())std::this_thread::yield();
    serialized.authority.RevokeNativeSession("session");revoked=true;
  });
  Check(serialized.authority.WithCurrentScope(serial_ticket,[&] {
    entered=true;std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Check(!revoked.load(),"revocation serializes after synchronous handoff");
  }),"actual bounded synchronous handoff returns");
  revoker.join();Check(revoked.load()&&!serialized.Deliver(serialized.Request()),"completed revoke prevents next delivery");
}
}
int main() {
  std::cout << std::unitbuf;
  try { Basics(); Contexts(); Races(); BoundsAndLifecycle(); }
  catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << "\n"; return 1; }
  std::cout << "scoped session authority: " << checks << " checks passed\n";
}

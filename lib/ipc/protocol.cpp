#include "lib/ipc/protocol.h"
#include <algorithm>
#include <array>
namespace agi::ipc {
namespace {
void Put(std::vector<uint8_t>& b, uint64_t value, size_t n) {
  for (size_t i = 0; i < n; ++i) b.push_back(static_cast<uint8_t>(value >> (i * 8)));
}
uint64_t Get(const std::vector<uint8_t>& b, size_t at, size_t n) {
  uint64_t value = 0;
  for (size_t i = 0; i < n; ++i) value |= uint64_t(b[at + i]) << (i * 8);
  return value;
}
bool Id(const std::string& s) {
  return !s.empty() && s.size() <= 128 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
    return c >= 33 && c <= 126 && c != '*';
  });
}
void Field(std::vector<uint8_t>& b, uint8_t tag, uint8_t type, const std::vector<uint8_t>& value) {
  b.push_back(tag); b.push_back(type); Put(b, value.size(), 2);
  b.insert(b.end(), value.begin(), value.end());
}
void Number(std::vector<uint8_t>& b, uint8_t tag, uint64_t value, size_t n) {
  std::vector<uint8_t> v; Put(v, value, n); Field(b, tag, n == 8 ? 2 : 1, v);
}
void String(std::vector<uint8_t>& b, uint8_t tag, const std::string& s) {
  Field(b, tag, 3, std::vector<uint8_t>(s.begin(), s.end()));
}
}
bool IsOperation(const std::string& op) {
  static const std::array<const char*, 21> names = {"observe", "wait", "click", "hover", "focus", "fill", "fill_secret", "select", "set_checked", "press", "scroll", "navigate", "open_tab", "back", "forward", "reload", "focus_tab", "close_tab", "upload", "dialog", "drag"};
  return std::any_of(names.begin(), names.end(), [&](auto n) { return op == n; });
}
std::vector<uint8_t> Encode(const Message& m) {
  std::vector<uint8_t> b = {'A', 'I', 'P', 'C'};
  Number(b, 1, m.version, 1); Number(b, 2, static_cast<uint8_t>(m.kind), 1);
  if (m.kind == Kind::challenge || m.kind == Kind::proof) {
    Number(b, 4, m.generation, 8); Field(b, 12, 4, m.challenge);
  } else if (m.kind == Kind::intent) {
    Number(b, 3, m.sequence, 8); Number(b, 4, m.generation, 8);
    String(b, 5, m.client); String(b, 6, m.session); String(b, 7, m.profile);
    String(b, 8, m.tab); String(b, 9, m.frame); String(b, 10, m.document); String(b, 11, m.operation);
    Number(b, 14, m.channel_generation, 8);
  } else if (m.kind == Kind::result) {
    Number(b, 3, m.sequence, 8); Number(b, 4, m.generation, 8); Number(b, 13, m.result, 1);
  } else if(m.kind==Kind::transport_config) {
    Number(b,14,m.channel_generation,8);String(b,15,m.server_key);String(b,16,m.server_cert);String(b,17,m.ca_cert);
  } else if(m.kind==Kind::identity_check) {
    Number(b,3,m.sequence,8);Number(b,14,m.channel_generation,8);String(b,5,m.client);String(b,6,m.session);String(b,18,m.certificate_hash);
  } else if(m.kind==Kind::identity_result) {
    Number(b,3,m.sequence,8);Number(b,14,m.channel_generation,8);Number(b,13,m.result,1);
  } else if(m.kind==Kind::transport_ready) {
    Number(b,14,m.channel_generation,8);Number(b,19,m.port,8);
  }
  return b;
}
bool Decode(const std::vector<uint8_t>& b, Message& out) {
  if (b.size() < 14 || b.size() > kMaxTransportConfig || std::string(b.begin(), b.begin()+4) != "AIPC") return false;
  Message m; uint32_t seen = 0;
  for (size_t p = 4; p < b.size();) {
    if (b.size() - p < 4) return false;
    const auto tag = b[p], type = b[p+1]; const auto n = Get(b, p+2, 2); p += 4;
    if (!tag || tag > 19 || (seen & (1u << tag)) || n > b.size() - p) return false;
    seen |= 1u << tag;
    if (tag == 1 || tag == 2 || tag == 13) {
      if (type != 1 || n != 1) return false;
      if (tag == 1) { if(b[p]!=1&&b[p]!=2)return false;m.version=b[p]; }
      if (tag == 2) { if (b[p] < 1 || b[p] > 9) return false; m.kind = static_cast<Kind>(b[p]); }
      if (tag == 13) { if (b[p] < 1 || b[p] > 2) return false; m.result = b[p]; }
    } else if(tag==19) {
      if(type!=2||n!=8||Get(b,p,8)==0||Get(b,p,8)>65535)return false;m.port=Get(b,p,8);
    } else if (tag == 3 || tag == 4 || tag == 14) {
      if (type != 2 || n != 8 || Get(b,p,8) == 0) return false;
      (tag == 3 ? m.sequence : tag == 4 ? m.generation : m.channel_generation) = Get(b,p,8);
    } else if (tag == 12) {
      if (type != 4 || n != 32) return false;
      m.challenge.assign(b.begin()+p, b.begin()+p+n);
    } else if(tag>=15&&tag<=17) {
      if(type!=3||n==0||n>4096)return false;
      std::string s(b.begin()+p,b.begin()+p+n);if(s.find('\0')!=std::string::npos)return false;
      (tag==15?m.server_key:tag==16?m.server_cert:m.ca_cert)=std::move(s);
    } else {
      if (type != 3 || n == 0 || n > 128) return false;
      std::string s(b.begin()+p, b.begin()+p+n); if (!Id(s)) return false;
      switch(tag) { case 5:m.client=s;break;case 6:m.session=s;break;case 7:m.profile=s;break;case 8:m.tab=s;break;case 9:m.frame=s;break;case 10:m.document=s;break;case 11:m.operation=s;break; }
      if(tag==18) { if(s.size()!=64||!std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))return false;m.certificate_hash=s; }
    }
    p += n;
  }
  uint32_t required = (1u<<1)|(1u<<2);
  if (m.kind == Kind::challenge || m.kind == Kind::proof) required |= (1u<<4)|(1u<<12);
  if (m.kind == Kind::intent) { required = ((1u<<12)-2)|(1u<<14); if (!IsOperation(m.operation)) return false; }
  if (m.kind == Kind::result) required |= (1u<<3)|(1u<<4)|(1u<<13);
  if(m.kind==Kind::transport_config)required|=(1u<<14)|(1u<<15)|(1u<<16)|(1u<<17);
  if(m.kind==Kind::identity_check)required|=(1u<<3)|(1u<<5)|(1u<<6)|(1u<<14)|(1u<<18);
  if(m.kind==Kind::identity_result)required|=(1u<<3)|(1u<<13)|(1u<<14);
  if(m.kind==Kind::transport_ready)required|=(1u<<14)|(1u<<19);
  if((uint8_t(m.kind)<=5&&(m.version!=1||b.size()>kMaxMessage))||(uint8_t(m.kind)>5&&m.version!=2))return false;
  if (seen != required) return false;
  out = std::move(m); return true;
}
bool Boundary::RegisterNativeGrant(const Grant& g) {
  if (!live_ || !Id(g.client) || !Id(g.session) || !Id(g.destination.profile) || !Id(g.destination.tab) || !Id(g.destination.frame) || !Id(g.destination.document) || !g.generation || !g.deadline_ms || g.operations.empty() || g.operations.size()>21) return false;
  std::vector<std::string> unique;
  for (const auto& op : g.operations) {
    if (!IsOperation(op) || (!g.control && op!="observe" && op!="wait") || std::find(unique.begin(),unique.end(),op)!=unique.end()) return false;
    unique.push_back(op);
  }
  grants_[g.session] = g; return true;
}
void Boundary::RegisterDestination(Destination d) {
  if(live_ && Id(d.profile) && Id(d.tab) && Id(d.frame) && Id(d.document)) {
    auto key=std::make_pair(d.tab,d.frame); destinations_[key] = std::move(d);
  }
}
Decision Boundary::Admit(const Message& m, uint64_t now) {
  if (!live_ || m.kind != Kind::intent || m.channel_generation!=channel_generation_ || !m.sequence || last_sequence_==UINT64_MAX || m.sequence != last_sequence_+1) return Decision::denied;
  // Consume admitted wire sequence even for denied intents: no retry can revive it.
  last_sequence_ = m.sequence;
  auto g = grants_.find(m.session); if (g == grants_.end()) return Decision::denied;
  const auto& grant = g->second;
  if (m.client != grant.client || m.generation != grant.generation || now >= grant.deadline_ms) return Decision::denied;
  auto d = destinations_.find({m.tab,m.frame}); if (d==destinations_.end()) return Decision::denied;
  const auto& dest = d->second;
  if (dest.profile != m.profile || dest.document != m.document || grant.destination.profile != dest.profile || grant.destination.tab != dest.tab || grant.destination.frame != dest.frame || grant.destination.document != dest.document) return Decision::denied;
  if (!IsOperation(m.operation) || std::find(grant.operations.begin(),grant.operations.end(),m.operation)==grant.operations.end()) return Decision::denied;
  if (m.operation != "observe" && m.operation != "wait" && (!grant.control || !grant.lease)) return Decision::denied;
  // 007 is an authorization boundary; engine execution remains unavailable.
  return Decision::unsupported;
}
void Boundary::Invalidate() { live_=false; grants_.clear(); destinations_.clear(); }
}  // namespace agi::ipc

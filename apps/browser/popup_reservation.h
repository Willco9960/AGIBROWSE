#pragma once
#include "lifecycle.h"

namespace agi::browser {
// Exact host client identity works before OnAfterCreated binds an engine.
// The owner retains each client; no pointer or reservation comes from a page.
template<class Reservations,class Client>
std::string ResolvePopupReservation(const Lifecycle& tabs,const Reservations& pending,
    const Client* client,int opener_engine,uint64_t now) {
  if(!client || opener_engine<=0)return {};
  std::string id;
  bool matched=false;
  for(const auto& [key,reservation]:pending) {
    if(reservation.client.get()!=client)continue;
    if(matched)return {};
    matched=true;id=reservation.tab;
  }
  auto tab=tabs.Resolve(id);
  if(!matched || !tab || tab->engine || !tab->deadline || now>=tab->deadline || tab->opener.empty())return {};
  auto opener=tabs.Resolve(tab->opener);
  if(!opener || opener->engine!=opener_engine || opener->profile!=tab->profile)return {};
  return id;
}
}  // namespace agi::browser

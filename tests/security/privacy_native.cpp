#include "lib/privacy/publication.h"
#include <iostream>
#include <stdexcept>
#include <type_traits>
using namespace agi::privacy;
namespace {
unsigned checks = 0;
void Check(bool good, const char* name) {
  ++checks; if (!good) throw std::runtime_error(name);
  std::cout << "PASS " << name << '\n';
}
std::string Bytes(const RedactedState& state) {
  std::string bytes; state.Deliver([&](auto payload) { bytes = payload; }); return bytes;
}
DraftState Fixture() {
  return {{{FieldKind::password,"PRIVATE_PASSWORD_NAME_010","SENTINEL_PASSWORD_010"},
           {FieldKind::marked_sensitive,"PRIVATE_MARKED_NAME_010","SENTINEL_MARKED_010"},
           {FieldKind::text,"public","Public content"},
           {FieldKind::text,"title","prefix SENTINEL_PASSWORD_010 suffix"},
           {FieldKind::text,"url","https://example.test/?q=SENTINEL_MARKED_010"},
           {FieldKind::text,"dialog","SENTINEL_RESOLVED_010"},
           {FieldKind::text,"PRIVATE_PASSWORD_NAME_010 reflected","metadata"},
           {FieldKind::text,"error","PRIVATE_MARKED_NAME_010 reflected"}},
          {"SENTINEL_RESOLVED_010"}};
}
}
int main() {
  static_assert(!std::is_constructible_v<RedactedState, std::string>);
  try {
    auto redacted = Redact(Fixture()); Check(bool(redacted), "bounded native fixture accepted");
    auto bytes = Bytes(*redacted);
    Check(bytes=="{\"fields\":[{\"name\":\"public\",\"value\":\"Public content\"}]}", "sensitive values names and exact reflections omitted entirely");
    for (const auto* forbidden : {"SENTINEL_", "PRIVATE_", "length", "hash", "password", "marked_sensitive"})
      Check(bytes.find(forbidden)==std::string::npos, "no sensitive metadata in sanitized payload");
    std::string captured;redacted->Deliver([&](auto data){captured=data;});
    Check(captured==bytes, "native staging callback receives sanitized bytes");
    auto draft=Fixture();draft.fields[0].value="";
    Check(Bytes(*Redact(draft)).find("PRIVATE_PASSWORD_NAME_010")==std::string::npos,"empty sensitive field name omitted");
    auto denied=[&](auto mutate){auto bad=Fixture();mutate(bad);Check(!Redact(bad),"malformed unsupported or over-budget input fails closed");};
    denied([](auto& d){d.fields[0].kind=static_cast<FieldKind>(99);});
    denied([](auto& d){d.fields[0].name="";});
    denied([](auto& d){d.fields.push_back(d.fields[0]);});
    denied([](auto& d){d.fields[2].value="\xC3\xA9";});
    denied([](auto& d){d.fields[2].value="line\nvalue";});
    denied([](auto& d){d.fields[2].value=std::string(4097,'a');});
    denied([](auto& d){d.known_secrets={""};});
    denied([](auto& d){d.known_secrets.resize(33,"secret");});
    denied([](auto& d){d.fields.resize(129);});
    denied([](auto& d){d.fields.clear();for(int i=0;i<17;++i)d.fields.push_back({FieldKind::text,"field"+std::to_string(i),std::string(4096,'x')});});
    draft={{{FieldKind::text,"quote\"slash\\","value"}}, {}};
    Check(Bytes(*Redact(draft))=="{\"fields\":[{\"name\":\"quote\\\"slash\\\\\",\"value\":\"value\"}]}","closed JSON escaping");
    Check(Bytes(*Redact({} ))=="{\"fields\":[]}","empty bounded state serializes without metadata");
    Check(!WriteDiagnostic(nullptr,static_cast<Event>(999)),"unknown diagnostic event fails closed");
    for(auto event:{Event::tab_fixture_popup_registered,Event::tab_fixture_order_verified,Event::tab_fixture_move_verified,
        Event::tab_fixture_cancel_verified,Event::tab_fixture_close_verified,Event::tab_fixture_pending_expired,
        Event::tab_fixture_resources_released,Event::tab_fixture_failed,Event::tab_fixture_click_issued,
        Event::tab_fixture_click_acknowledged,Event::tab_fixture_popup_requested,
        Event::tab_fixture_visibility_adjusted,Event::tab_fixture_visibility_restored}) {
      Check(WriteDiagnostic(nullptr,event),"lifecycle proof is a closed content-free diagnostic");
      Check(!WriteDiagnostic(nullptr,event,1),"lifecycle diagnostic refuses numeric payload");
    }
    Check(WriteDiagnostic(nullptr,Event::tab_fixture_failed_stage,0),"native failed stage zero allowed");
    Check(WriteDiagnostic(nullptr,Event::tab_fixture_failed_stage,7),"native failed stage upper bound allowed");
    Check(!WriteDiagnostic(nullptr,Event::tab_fixture_failed_stage,8),"unknown lifecycle stage rejected");
    Check(!WriteDiagnostic(nullptr,Event::tab_fixture_failed_stage,UINT64_MAX),"failed stage cannot encode arbitrary numeric content");
    for(uint64_t step=1;step<=9;++step)Check(WriteDiagnostic(nullptr,Event::browser_ui_probe_step,step),"browser UI proof accepts only its closed ordered steps");
    Check(!WriteDiagnostic(nullptr,Event::browser_ui_probe_step,0),"browser UI proof has no unreported zero step");
    for(auto step:{uint64_t{0},uint64_t{10},UINT64_MAX})Check(!WriteDiagnostic(nullptr,Event::browser_ui_probe_failed_stage,step),"browser UI failure stage refuses unknown payloads");
    for(uint64_t reason=1;reason<=13;++reason)Check(WriteDiagnostic(nullptr,Event::tab_fixture_failed_reason,reason),"closed native failure reason accepted");
    for(auto reason:{uint64_t{0},uint64_t{14},UINT64_MAX})Check(!WriteDiagnostic(nullptr,Event::tab_fixture_failed_reason,reason),"failure reason refuses arbitrary numeric content");
    for(uint64_t relation=0;relation<=7;++relation)Check(WriteDiagnostic(nullptr,Event::tab_fixture_cursor_relation,relation),"closed native cursor relation accepted");
    for(auto relation:{uint64_t{8},UINT64_MAX})Check(!WriteDiagnostic(nullptr,Event::tab_fixture_cursor_relation,relation),"cursor relation refuses arbitrary numeric content");
    for(uint64_t destination=0;destination<=15;++destination)Check(WriteDiagnostic(nullptr,Event::tab_fixture_cursor_destination,destination),"closed native cursor destination accepted");
    for(auto destination:{uint64_t{16},UINT64_MAX})Check(!WriteDiagnostic(nullptr,Event::tab_fixture_cursor_destination,destination),"cursor destination refuses arbitrary numeric content");
    Check(WriteDiagnostic(nullptr,Event::tab_fixture_cursor_destination_unavailable),"unavailable geometry diagnostic is content-free");
    Check(!WriteDiagnostic(nullptr,Event::tab_fixture_cursor_destination_unavailable,1),"unavailable geometry refuses numeric payload");
    for(uint64_t state=0;state<=63;++state)Check(WriteDiagnostic(nullptr,Event::tab_fixture_native_window_state,state),"closed native window state accepted");
    for(auto state:{uint64_t{64},UINT64_MAX})Check(!WriteDiagnostic(nullptr,Event::tab_fixture_native_window_state,state),"native window state refuses arbitrary numeric content");
    Check(WriteDiagnostic(nullptr,Event::tab_fixture_native_window_state_unavailable),"unavailable native state is content-free");
    Check(!WriteDiagnostic(nullptr,Event::tab_fixture_native_window_state_unavailable,1),"unavailable native state refuses numeric payload");
    for(uint64_t cloak=0;cloak<=2;++cloak)Check(WriteDiagnostic(nullptr,Event::tab_fixture_native_window_cloak,cloak),"closed cloak status accepted");
    for(auto cloak:{uint64_t{3},UINT64_MAX})Check(!WriteDiagnostic(nullptr,Event::tab_fixture_native_window_cloak,cloak),"cloak status refuses arbitrary numeric content");
    Check(!WriteDiagnostic(nullptr,Event::fixture_ready,0x534543524554),"text-free event rejects numeric secret encoding");
    for(uint64_t stage=1;stage<=9;++stage){Check(WriteDiagnostic(nullptr,Event::profile_probe_step,stage),"closed profile proof stage accepted");Check(WriteDiagnostic(nullptr,Event::profile_probe_failed_stage,stage),"closed profile failure stage accepted");}
    for(auto stage:{uint64_t{0},uint64_t{10},UINT64_MAX}){Check(!WriteDiagnostic(nullptr,Event::profile_probe_step,stage),"profile proof rejects arbitrary value");Check(!WriteDiagnostic(nullptr,Event::profile_probe_failed_stage,stage),"profile failure rejects arbitrary value");}
    for(uint64_t reason=1;reason<=8;++reason)Check(WriteDiagnostic(nullptr,Event::profile_probe_failed_reason,reason),"closed profile failure reason accepted");
    for(auto reason:{uint64_t{0},uint64_t{9},UINT64_MAX})Check(!WriteDiagnostic(nullptr,Event::profile_probe_failed_reason,reason),"profile failure reason rejects arbitrary value");
    for(uint64_t state=0;state<=511;++state)Check(WriteDiagnostic(nullptr,Event::profile_setting_state,state),"closed profile readiness and setting classes accepted");
    for(auto state:{uint64_t{512},UINT64_MAX})Check(!WriteDiagnostic(nullptr,Event::profile_setting_state,state),"profile setting state rejects arbitrary numeric content");
    for(uint64_t result=0;result<=4;++result)Check(WriteDiagnostic(nullptr,Event::profile_native_menu_return,result),"closed native menu return category accepted");
    for(auto result:{uint64_t{5},UINT64_MAX})Check(!WriteDiagnostic(nullptr,Event::profile_native_menu_return,result),"native menu return rejects arbitrary command values");
    for(uint64_t phase=1;phase<=12;++phase)Check(WriteDiagnostic(nullptr,Event::profile_action_phase,phase),"closed native profile action phase accepted");
    for(auto phase:{uint64_t{0},uint64_t{13},UINT64_MAX})Check(!WriteDiagnostic(nullptr,Event::profile_action_phase,phase),"profile action phase rejects arbitrary numeric content");
    Check(WriteDiagnostic(nullptr,Event::profile_action_posted,0)&&WriteDiagnostic(nullptr,Event::profile_action_posted,1),"native task posting accepts only boolean result");
    for(auto posted:{uint64_t{2},UINT64_MAX})Check(!WriteDiagnostic(nullptr,Event::profile_action_posted,posted),"native task posting rejects arbitrary numeric content");
    Check(WriteDiagnostic(nullptr,Event::profile_menu_requested)&&WriteDiagnostic(nullptr,Event::profile_native_create_selected),"native profile menu proof accepts no content");
    Check(!WriteDiagnostic(nullptr,Event::profile_menu_requested,1)&&!WriteDiagnostic(nullptr,Event::profile_native_create_selected,1),"native profile menu proof rejects numeric content");
    Check(!WriteDiagnostic(nullptr,Event::main_frame_loaded,600),"HTTP diagnostic values bounded");
    Check(!WriteDiagnostic(nullptr,Event::browser_created,UINT64_MAX),"native browser ID values bounded");
    auto file=std::tmpfile();Check(file!=nullptr,"actual diagnostic file sink available");
    Check(WriteDiagnostic(file,Event::fixture_ready),"current production diagnostic sink writes closed record");
    Check(WriteDiagnostic(file,Event::main_frame_loaded,200),"current production status record written");
    Check(WriteDiagnostic(file,Event::tab_fixture_failed_reason,static_cast<uint64_t>(TabFixtureFailureReason::cursor_root_mismatch)),"native failure writes closed reason to actual sink");
    Check(WriteDiagnostic(file,Event::tab_fixture_cursor_relation,5),"native cursor relation writes closed mask to actual sink");
    Check(WriteDiagnostic(file,Event::tab_fixture_cursor_destination,3),"native cursor destination writes closed mask to actual sink");
    Check(WriteDiagnostic(file,Event::tab_fixture_visibility_adjusted),"native visibility adjustment writes content-free proof");
    Check(WriteDiagnostic(file,Event::tab_fixture_visibility_restored),"native visibility restoration writes content-free proof");
    Check(WriteDiagnostic(file,Event::tab_fixture_native_window_state,43),"native state writes closed mask to actual sink");
    Check(WriteDiagnostic(file,Event::tab_fixture_native_window_state_unavailable),"native state unavailable writes content-free marker");
    Check(WriteDiagnostic(file,Event::tab_fixture_native_window_cloak,0),"cloak unavailable writes closed status");
    Check(WriteDiagnostic(file,Event::browser_ui_probe_step,1),"browser UI proof writes closed step");
    std::rewind(file);char buffer[1024]{};auto n=std::fread(buffer,1,sizeof(buffer),file);std::fclose(file);
    Check(std::string(buffer,n)=="{\"event\":\"fixture_ready\",\"value\":0}\n{\"event\":\"main_frame_loaded\",\"value\":200}\n{\"event\":\"tab_fixture_failed_reason\",\"value\":13}\n{\"event\":\"tab_fixture_cursor_relation\",\"value\":5}\n{\"event\":\"tab_fixture_cursor_destination\",\"value\":3}\n{\"event\":\"tab_fixture_visibility_adjusted\",\"value\":0}\n{\"event\":\"tab_fixture_visibility_restored\",\"value\":0}\n{\"event\":\"tab_fixture_native_window_state\",\"value\":43}\n{\"event\":\"tab_fixture_native_window_state_unavailable\",\"value\":0}\n{\"event\":\"tab_fixture_native_window_cloak\",\"value\":0}\n{\"event\":\"browser_ui_probe_step\",\"value\":1}\n","actual diagnostic file contains fixed schema only");
    std::cout << "PASS privacy contract checks=" << checks << '\n';return 0;
  } catch(const std::exception& e) {std::cerr<<"FAIL privacy contract case: "<<e.what()<<'\n';return 1;}
}

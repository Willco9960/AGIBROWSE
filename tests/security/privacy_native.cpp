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
    Check(!WriteDiagnostic(nullptr,Event::fixture_ready,0x534543524554),"text-free event rejects numeric secret encoding");
    Check(!WriteDiagnostic(nullptr,Event::main_frame_loaded,600),"HTTP diagnostic values bounded");
    Check(!WriteDiagnostic(nullptr,Event::browser_created,UINT64_MAX),"native browser ID values bounded");
    auto file=std::tmpfile();Check(file!=nullptr,"actual diagnostic file sink available");
    Check(WriteDiagnostic(file,Event::fixture_ready),"current production diagnostic sink writes closed record");
    Check(WriteDiagnostic(file,Event::main_frame_loaded,200),"current production status record written");
    std::rewind(file);char buffer[1024]{};auto n=std::fread(buffer,1,sizeof(buffer),file);std::fclose(file);
    Check(std::string(buffer,n)=="{\"event\":\"fixture_ready\",\"value\":0}\n{\"event\":\"main_frame_loaded\",\"value\":200}\n","actual diagnostic file contains fixed schema only");
    std::cout << "PASS privacy contract checks=" << checks << '\n';return 0;
  } catch(const std::exception& e) {std::cerr<<"FAIL privacy contract case: "<<e.what()<<'\n';return 1;}
}

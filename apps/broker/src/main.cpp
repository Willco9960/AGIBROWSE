#include <string>
#include "lib/ipc/windows_channel.h"
#ifdef AGI_TRANSPORT
#include "lib/transport/server.h"
#endif
int main(int argc, char** argv) {
#ifdef _WIN32
  if(argc==2 && std::string(argv[1])=="--private-child") {
#ifdef AGI_TRANSPORT
    agi::transport::LoopbackServer transport;return agi::ipc::RunPrivateBroker(&transport);
#else
    return agi::ipc::RunPrivateBroker();
#endif
  }
#endif
  // Standalone/untrusted launches cannot establish a privileged host connection.
  return 71;
}

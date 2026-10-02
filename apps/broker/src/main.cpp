#include <string>
#include "lib/ipc/windows_channel.h"
int main(int argc, char** argv) {
#ifdef _WIN32
  if(argc==2 && std::string(argv[1])=="--private-child") return agi::ipc::RunPrivateBroker();
#endif
  // Standalone/untrusted launches cannot establish a privileged host connection.
  return 71;
}

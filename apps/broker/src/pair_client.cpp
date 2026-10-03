#include "lib/transport/server.h"
#include <iostream>
int wmain(int argc, wchar_t **argv) {
  try {
    if (argc == 4 && std::wstring(argv[1]) == L"create") {
      auto key = agi::transport::GenerateKey();
      agi::transport::ProtectFile(argv[2], agi::transport::KeyPem(key));
      agi::transport::WritePublicFile(argv[3], agi::transport::MakeCsr(key));
      std::cout << "Protected key and signed CSR created. Client SHA256: "
                << agi::transport::Pin(key) << "\n";
      return 0;
    }
    if (argc == 5 && std::wstring(argv[1]) == L"prove") {
      auto key =
          agi::transport::ReadKey(agi::transport::UnprotectFile(argv[2]));
      auto challenge = agi::transport::ReadBoundedFile(argv[3], 512);
      if (challenge.rfind("AEN1\n" + agi::transport::Pin(key) + "\n", 0) != 0)
        return 2;
      agi::transport::WritePublicFile(argv[4],
                                      agi::transport::Sign(key, challenge));
      return 0;
    }
    if (argc == 5 && std::wstring(argv[1]) == L"probe") {
      auto key =
          agi::transport::ReadKey(agi::transport::UnprotectFile(argv[2]));
      auto credential = agi::transport::DecodeCredential(
          agi::transport::ReadBoundedFile(argv[3]));
      auto port = std::stoul(argv[4]);
      if (port == 0 || port > 65535)
        return 2;
      bool result = agi::transport::Probe(static_cast<unsigned short>(port),
                                          key, credential);
      std::cout
          << (result
                  ? "Authenticated transport; zero grants: PERMISSION_DENIED\n"
                  : "Transport rejected\n");
      return result ? 0 : 2;
    }
    std::cout << "Local-only usage: create protected-key-file csr-file | prove "
                 "protected-key-file challenge-file proof-file | probe "
                 "protected-key-file credential-file port\n";
    return 2;
  } catch (...) {
    std::cerr << "Local credential operation failed closed\n";
    return 2;
  }
}

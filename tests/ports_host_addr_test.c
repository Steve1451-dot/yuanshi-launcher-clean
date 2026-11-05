#include <assert.h>
#include <stdio.h>
#include <string.h>
// ports_get_host_addr is the only thing under test; the Switch build normally
// resolves the local IPv4 through gethostid(), which reports 0 in emulators and
// on some firmware, so ports.c falls back to a UDP route probe. This test pins
// the contract of that resolution on the host (Win32 winsock route probe).
#include "../extern/libpeer/src/ports.c"

int main(void) {
#if defined(_WIN32)
  WSADATA wsa;
  assert(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
#endif
  Address addr;
  memset(&addr, 0, sizeof(addr));
  addr.family = AF_INET;
  const int ret = ports_get_host_addr(&addr, "");
  assert(ret != -1);
  if (ret == 1) {
    // A resolved address must be a real IPv4, not the all-zero placeholder.
    // Address::family is the library's own AF_INET discriminator (set by
    // addr_set_family); sin.sin_family may stay 0 in the Switch/Win32 paths.
    assert(addr.family == AF_INET);
    assert(addr.sin.sin_addr.s_addr != 0);
  }
  puts("PASS ports_host_addr");
  return 0;
}

#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "config.h"

#if CONFIG_USE_LWIP
#include "lwip/ip_addr.h"
#include "lwip/netdb.h"
#include "lwip/netif.h"
#include "lwip/sys.h"
#elif defined(__SWITCH__)
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "ports.h"

// Resolve the outbound IPv4 of this console without an interface ioctl: bind a
// throwaway UDP socket to a public resolver (no packet is sent) and read back
// the local address the route selected. The Switch's own gethostid() is often 0,
// which previously produced zero local ICE candidates and no STUN at all.
static int ports_probe_route_ipv4(Address* addr) {
  const char* targets[] = {"1.1.1.1", "223.5.5.5", "8.8.8.8"};
  for (unsigned i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) continue;
    struct sockaddr_in remote;
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    remote.sin_addr.s_addr = inet_addr(targets[i]);
    int ok = 0;
    if (connect(fd, (struct sockaddr*)&remote, sizeof(remote)) == 0) {
      struct sockaddr_in local;
      socklen_t local_len = sizeof(local);
      if (getsockname(fd, (struct sockaddr*)&local, &local_len) == 0 &&
          local.sin_addr.s_addr != 0) {
        memcpy(&addr->sin.sin_addr, &local.sin_addr, 4);
        ok = 1;
      }
    }
    close(fd);
    if (ok) return 1;
  }
  return 0;
}
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/ioctl.h>
#endif

#include "ports.h"
#include "utils.h"

int ports_get_host_addr(Address* addr, const char* iface_prefix) {
  int ret = 0;

#if CONFIG_USE_LWIP
  struct netif* netif;
  int i;
  for (netif = netif_list; netif != NULL; netif = netif->next) {
    switch (addr->family) {
      case AF_INET6:
        for (i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++) {
          if (!ip6_addr_isany(netif_ip6_addr(netif, i))) {
            memcpy(&addr->sin6.sin6_addr, netif_ip6_addr(netif, i), 16);
            ret = 1;
            break;
          }
        }
        break;
      case AF_INET:
      default:
        if (!ip_addr_isany(&netif->ip_addr)) {
          memcpy(&addr->sin.sin_addr, &netif->ip_addr.u_addr.ip4, 4);
          ret = 1;
        }
        break;
    }

    if (ret) {
      break;
    }
  }
#elif defined(__SWITCH__)
  uint32_t ip = gethostid();
  if (ip != 0 && addr->family == AF_INET) {
      memcpy(&addr->sin.sin_addr, &ip, 4);
      ret = 1;
  }
  // Emulators without a host-id ioctl report 0 here, which left the agent with
  // no local candidate at all (no STUN ever sent). Fall back to the UDP connect
  // trick: the kernel picks the interface the pod address is routed through.
  if (!ret && addr->family == AF_INET && ports_probe_route_ipv4(addr)) {
      ret = 1;
  }
#elif defined(_WIN32)
  // Windows host build (unit tests): same route-probe idea with winsock.
  {
    SOCKET fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd != INVALID_SOCKET) {
      struct sockaddr_in remote;
      memset(&remote, 0, sizeof(remote));
      remote.sin_family = AF_INET;
      remote.sin_port = htons(53);
      remote.sin_addr.s_addr = inet_addr("1.1.1.1");
      if (connect(fd, (struct sockaddr*)&remote, sizeof(remote)) == 0) {
        struct sockaddr_in local;
        int local_len = sizeof(local);
        if (getsockname(fd, (struct sockaddr*)&local, &local_len) == 0 &&
            local.sin_addr.s_addr != 0 && addr->family == AF_INET) {
          memcpy(&addr->sin.sin_addr, &local.sin_addr, 4);
          ret = 1;
        }
      }
      closesocket(fd);
    }
  }
#else

  struct ifaddrs *ifaddr, *ifa;

  if (getifaddrs(&ifaddr) == -1) {
    LOGE("getifaddrs failed: %s", strerror(errno));
    return -1;
  }

  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL) {
      continue;
    }

    if (ifa->ifa_addr->sa_family != addr->family) {
      continue;
    }

    if (iface_prefix && strlen(iface_prefix) > 0) {
      if (strncmp(ifa->ifa_name, iface_prefix, strlen(iface_prefix)) != 0) {
        continue;
      }

    } else {
      if ((ifa->ifa_flags & IFF_UP) == 0) {
        continue;
      }

      if ((ifa->ifa_flags & IFF_RUNNING) == 0) {
        continue;
      }

      if ((ifa->ifa_flags & IFF_LOOPBACK) == IFF_LOOPBACK) {
        continue;
      }
    }

    switch (ifa->ifa_addr->sa_family) {
      case AF_INET6:
        memcpy(&addr->sin6, ifa->ifa_addr, sizeof(struct sockaddr_in6));
        break;
      case AF_INET:
      default:
        memcpy(&addr->sin, ifa->ifa_addr, sizeof(struct sockaddr_in));
        break;
    }
    ret = 1;
    break;
  }
  freeifaddrs(ifaddr);
#endif
  return ret;
}

int ports_resolve_addr(const char* host, Address* addr) {
  char addr_string[ADDRSTRLEN];
  int ret = -1;
  struct addrinfo hints, *res, *p;
  int status;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if ((status = getaddrinfo(host, NULL, &hints, &res)) != 0) {
    LOGE("getaddrinfo error: %d\n", status);
    return ret;
  }

  // TODO: Support for IPv6
  addr_set_family(addr, AF_INET);
  for (p = res; p != NULL; p = p->ai_next) {
    if (p->ai_family == addr->family) {
      switch (addr->family) {
        case AF_INET6:
          memcpy(&addr->sin6, p->ai_addr, sizeof(struct sockaddr_in6));
          break;
        case AF_INET:
        default:
          memcpy(&addr->sin, p->ai_addr, sizeof(struct sockaddr_in));
          break;
      }
      ret = 0;
    }
  }

  addr_to_string(addr, addr_string, sizeof(addr_string));
  LOGI("Resolved %s -> %s", host, addr_string);
  freeaddrinfo(res);
  return ret;
}

uint32_t ports_get_epoch_time() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint32_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

uint32_t ports_get_monotonic_time(void) {
#if CONFIG_USE_LWIP
  return sys_now();
#else
  struct timespec time;
  if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
    return 0;
  return (uint32_t)time.tv_sec * 1000 + (uint32_t)(time.tv_nsec / 1000000);
#endif
}

void ports_sleep_ms(int ms) {
#if CONFIG_USE_LWIP
  sys_msleep(ms);
#elif defined(_WIN32)
  Sleep((DWORD)ms);
#else
  usleep(ms * 1000);
#endif
}

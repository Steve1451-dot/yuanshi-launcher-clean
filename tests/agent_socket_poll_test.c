#include <assert.h>
#include <sys/select.h>

static int test_select(int nfds, fd_set* readfds, fd_set* writefds,
                       fd_set* exceptfds, struct timeval* timeout);

#define select test_select
#include "../extern/libpeer/src/agent.c"
#undef select

static long expected_timeout_us;
static int select_calls;
static int packet_ready;
static int receive_result = 1;
static int send_result = 1;

static int test_select(int nfds, fd_set* readfds, fd_set* writefds,
                       fd_set* exceptfds, struct timeval* timeout) {
  assert(nfds == 4);
  assert(FD_ISSET(3, readfds));
  assert(writefds == NULL);
  assert(exceptfds == NULL);
  assert(timeout->tv_sec == 0);
  assert(timeout->tv_usec == expected_timeout_us);
  select_calls++;
  if (packet_ready < 0) errno = EINVAL;
  return packet_ready;
}

int udp_socket_recvfrom(UdpSocket* socket, Address* addr, uint8_t* buf, int len) {
  (void)addr;
  assert(socket->fd == 3);
  assert(len >= 1);
  buf[0] = 0x80;
  if (receive_result < 0) errno = EIO;
  return receive_result;
}

int udp_socket_sendto(UdpSocket* socket, Address* addr, const uint8_t* buf, int len) {
  (void)addr; (void)buf; (void)len;
  assert(socket->fd == 3);
  if (send_result < 0) errno = ENETUNREACH;
  return send_result;
}

int main(void) {
  Agent agent = {0};
  uint8_t packet[16];
  agent.udp_sockets[0].fd = 3;
  agent.udp_sockets[1].fd = -1;

  expected_timeout_us = 0;
  assert(agent_socket_recv(&agent, NULL, packet, sizeof(packet), 0) == 0);
  assert(select_calls == 1);

  packet_ready = 1;
  assert(agent_socket_recv(&agent, NULL, packet, sizeof(packet), 0) == 1);
  assert(packet[0] == 0x80);
  assert(select_calls == 2);

  packet_ready = 0;
  expected_timeout_us = 1000;
  assert(agent_socket_recv_attempts(&agent, NULL, packet, sizeof(packet), 3) == 0);
  assert(select_calls == 5);

  packet_ready = 1;
  assert(agent_socket_recv_attempts(&agent, NULL, packet, sizeof(packet), 3) == 1);
  assert(select_calls == 6);
  assert(agent.udp_datagrams_received == 2);
  packet_ready = -1;
  assert(agent_socket_recv(&agent, NULL, packet, sizeof(packet), 1) == -1);
  assert(agent.udp_select_errors == 1 && agent.last_socket_error == EINVAL);
  packet_ready = 1; receive_result = -1;
  assert(agent_socket_recv(&agent, NULL, packet, sizeof(packet), 1) == -1);
  assert(agent.udp_receive_errors == 1 && agent.last_socket_error == EIO);
  Address remote = {0}; remote.family = AF_INET;
  send_result = -1;
  assert(agent_socket_send(&agent, &remote, packet, 1) == -1);
  assert(agent.udp_send_errors == 1 && agent.last_socket_error == ENETUNREACH);
  agent_clear_candidates(&agent);
  assert(!agent.udp_send_errors && !agent.udp_receive_errors && !agent.udp_select_errors && !agent.last_socket_error);
  return 0;
}

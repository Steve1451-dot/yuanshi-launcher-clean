#include <assert.h>

#include "../extern/libpeer/src/peer_connection.c"

static int socket_reads;
static int dtls_reads;
static int sctp_records;
static int rtp_records;
static int pending_records;
static int retain_pending_records;
static int next_dtls_result;
static int consume_datagram;
static size_t plaintext_remaining;
static size_t expected_sctp_length = 3;
static uint8_t socket_packet[32];
static int socket_size;
static uint32_t test_ssrc=42;
static RtpDecoder* last_decoder;
static int rtp_poll_received=-1;
static int repair_ticks, emit_repair;
static int rtp_unprotect_result, rtcp_unprotect_result;

int agent_recv_nonblocking(Agent* agent, uint8_t* buffer, int length) {
  (void)agent;
  ++socket_reads;
  assert(socket_size <= length);
  memcpy(buffer, socket_packet, socket_size);
  const int result = socket_size;
  socket_size = 0;
  return result;
}

int mbedtls_ssl_check_pending(const mbedtls_ssl_context* ssl) {
  (void)ssl;
  return pending_records != 0;
}

int dtls_srtp_read(DtlsSrtp* dtls, uint8_t* buffer, size_t length) {
  ++dtls_reads;
  if (consume_datagram) {
    unsigned char datagram[CONFIG_MTU];
    assert(peer_connection_dtls_srtp_recv(dtls, datagram, sizeof(datagram)) == 5);
    assert(datagram[0] == 0x17);
    assert(peer_connection_dtls_srtp_recv(dtls, datagram, sizeof(datagram)) ==
           MBEDTLS_ERR_SSL_WANT_READ);
  }
  if (pending_records && !retain_pending_records)
    --pending_records;
  // Match mbedtls_ssl_read: a short destination returns only part of one
  // application record and leaves its tail pending. SCTP cannot consume
  // these two slices as independent packets (the checksum covers both).
  if (plaintext_remaining) {
    const size_t bytes = plaintext_remaining < length ? plaintext_remaining : length;
    memset(buffer, 0x55, bytes);
    plaintext_remaining -= bytes;
    pending_records = plaintext_remaining != 0;
    return (int)bytes;
  }
  if (next_dtls_result > 0) {
    assert((size_t)next_dtls_result <= length);
    memset(buffer, 0x55, next_dtls_result);
  }
  return next_dtls_result;
}

void sctp_incoming_data(Sctp* sctp, char* data, size_t length) {
  (void)sctp;
  assert(length == expected_sctp_length && data[0] == 0x55);
  ++sctp_records;
}

int rtp_decoder_decode(RtpDecoder* decoder, const uint8_t* data, size_t length) {
  last_decoder=decoder;
  assert(length == 14 && data[0] == 0x80);
  ++rtp_records;
  return 0;
}

int rtp_packet_validate(uint8_t* data, size_t length) {
  return length == 14 && data[0] == 0x80;
}
uint32_t rtp_get_ssrc(uint8_t* packet) { (void)packet; return test_ssrc; }
void rtp_decoder_poll(RtpDecoder* decoder) { (void)decoder; }
void rtp_decoder_poll_after_receive(RtpDecoder* decoder,int received) { (void)decoder;rtp_poll_received=received; }
void rtp_decoder_poll_nacks(RtpDecoder* decoder,int rtt,RtpSendNack send,void* userdata) {
  (void)decoder;assert(rtt==25);++repair_ticks;
  if(emit_repair) {assert(send(userdata,101,2));emit_repair=0;}
}
int agent_get_rtt_ms(Agent* agent) { (void)agent;return 25; }
int sctp_get_rtt_ms(Sctp* sctp) { (void)sctp;return 25; }
int dtls_srtp_probe(uint8_t* data) { return data[0] >= 0x14 && data[0] <= 0x17; }
void sctp_tick(Sctp* sctp) { (void)sctp; }
int sctp_is_connected(Sctp* sctp) { (void)sctp; return 1; }
uint32_t ports_get_epoch_time(void) { return 1; }
uint32_t ports_get_monotonic_time(void) { return 1; }
int addr_to_string(const Address* address, char* buffer, size_t size) {
  (void)address;
  if (size) buffer[0] = 0;
  return 0;
}
int agent_select_candidate_pair(Agent* agent) { (void)agent; abort(); }
int agent_connectivity_check(Agent* agent) { (void)agent; abort(); }
int agent_fail_nominated_remote(Agent* agent) { (void)agent; abort(); }
int dtls_srtp_handshake(DtlsSrtp* dtls, Address* address) {
  (void)dtls; (void)address; return 0;
}
void dtls_srtp_reset_session(DtlsSrtp* dtls) { (void)dtls; abort(); }
int sctp_create_association(Sctp* sctp, DtlsSrtp* dtls) {
  (void)sctp; (void)dtls; return -7;
}
int dtls_srtp_decrypt_rtp_packet(DtlsSrtp* dtls, uint8_t* packet, int* bytes) {
  (void)dtls; (void)packet; (void)bytes; return rtp_unprotect_result;
}
int dtls_srtp_decrypt_rtcp_packet(DtlsSrtp* dtls, uint8_t* packet, int* bytes) {
  (void)dtls; (void)packet; (void)bytes; return rtcp_unprotect_result;
}
int dtls_srtp_encrypt_rctp_packet(DtlsSrtp* dtls, uint8_t* packet, int* bytes) {
  (void)dtls; (void)packet; (void)bytes; return 0;
}
int agent_send(Agent* agent, const uint8_t* buffer, int length) {
  (void)agent;
  if(emit_repair) {assert(length==16 && buffer[1]==RTCP_RTPFB && buffer[13]==101);}
  return length;
}

static void queue_dtls(void) {
  memcpy(socket_packet, "\x17\x01\x02\x03\x04", 5);
  socket_size = 5;
}

int main(void) {
  PeerConnection* failed=calloc(1,sizeof(*failed));assert(failed);
  failed->state=PEER_CONNECTION_CONNECTED;failed->config.datachannel=DATA_CHANNEL_BINARY;
  peer_connection_loop(failed);
  PeerTransportStats failed_stats={0};assert(peer_connection_get_transport_stats(failed,&failed_stats)==0);
  assert(failed_stats.sctp_create_attempted && failed_stats.sctp_create_result==-7);
  free(failed);
  PeerConnection* peer = calloc(1, sizeof(*peer));
  assert(peer);
  peer->state = PEER_CONNECTION_COMPLETED;
  peer->dtls_srtp.user_data = peer;

  queue_dtls();
  next_dtls_result = MBEDTLS_ERR_SSL_WANT_WRITE;
  assert(peer_connection_loop(peer) == 1);
  assert(socket_reads == 1 && dtls_reads == 1 && sctp_records == 0);
  assert(peer->dtls_pending && peer->agent_ret == 5);

  memcpy(socket_packet, "\x80\x60\x00\x01\x00\x00\x00\x00\x00\x00\x00\x2a\x65\x80", 14);
  socket_size = 14;
  consume_datagram = 1;
  next_dtls_result = 3;
  assert(peer_connection_loop(peer) == 1);
  assert(socket_reads == 1 && dtls_reads == 2 && sctp_records == 1);
  assert(!peer->dtls_pending && peer->agent_ret < 0);

  consume_datagram = 0;
  pending_records = 2;
  assert(peer_connection_loop(peer) == 1);
  assert(socket_reads == 1 && dtls_reads == 3 && sctp_records == 2);
  assert(peer_connection_loop(peer) == 1);
  assert(socket_reads == 1 && dtls_reads == 4 && sctp_records == 3);
  assert(peer_connection_loop(peer) == 1);
  assert(socket_reads == 2 && rtp_records == 1);
  assert(peer->completed_udp_packets == 2 && peer->completed_dtls_packets == 1);

  queue_dtls();
  consume_datagram = 1;
  next_dtls_result = MBEDTLS_ERR_SSL_WANT_READ;
  assert(peer_connection_loop(peer) == 1);
  assert(dtls_reads == 5 && !peer->dtls_pending);
  assert(peer_connection_loop(peer) == 0);
  assert(dtls_reads == 5 && socket_reads == 4);

  memcpy(socket_packet, "\x80\x60\x00\x02\x00\x00\x00\x00\x00\x00\x00\x2a\x65\x80", 14);
  socket_size = 14;
  consume_datagram = 0;
  pending_records = 1;
  retain_pending_records = 1;
  assert(peer_connection_loop(peer) == 1);
  assert(rtp_records == 2 && dtls_reads == 6 && socket_reads == 5);
  pending_records = 0;
  retain_pending_records = 0;

  queue_dtls();
  next_dtls_result = MBEDTLS_ERR_SSL_WANT_WRITE;
  assert(peer_connection_loop(peer) == 1);
  const int reads_before_retry = socket_reads;
  for (int attempt = 0; attempt < 100; ++attempt) {
    assert(peer_connection_loop(peer) == 0);
    assert(peer->dtls_pending && peer->agent_ret == 5);
    assert(socket_reads == reads_before_retry);
  }
  consume_datagram = 1;
  next_dtls_result = MBEDTLS_ERR_SSL_WANT_READ;
  assert(peer_connection_loop(peer) == 0);
  assert(!peer->dtls_pending);

  queue_dtls();
  consume_datagram = 0;
  next_dtls_result = MBEDTLS_ERR_SSL_INVALID_RECORD;
  assert(peer_connection_loop(peer) == 1);
  assert(!peer->dtls_pending && peer->agent_ret < 0);
  queue_dtls();
  next_dtls_result = MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
  assert(peer_connection_loop(peer) == 1);
  assert(peer->state == PEER_CONNECTION_CLOSED && !peer->dtls_pending);
  free(peer);
  peer=calloc(1,sizeof(*peer));assert(peer);
  peer->state=PEER_CONNECTION_CHECKING;
  assert(peer_connection_set_receive_payload_types(peer,100,109)==0);
  assert(peer_connection_set_receive_payload_types(peer,128,109)==-1);
  assert(peer_connection_set_receive_payload_types(peer,100,100)==-1);
  peer->state=PEER_CONNECTION_COMPLETED;
  assert(peer_connection_set_receive_payload_types(peer,100,109)==0);
  pending_records=0;consume_datagram=0;
  memset(socket_packet,0,14);socket_packet[0]=0x80;socket_packet[1]=109;socket_size=14;
  assert(peer_connection_loop(peer)==1 && last_decoder==&peer->artp_decoder);
  assert(rtp_poll_received==1);
  assert(peer_connection_loop(peer)==0 && rtp_poll_received==0);
  test_ssrc=43;socket_packet[1]=100;socket_size=14;
  assert(peer_connection_loop(peer)==1 && last_decoder==&peer->vrtp_decoder);
  const int decoded_before=rtp_records;
  socket_packet[1]=101;socket_size=14;
  assert(peer_connection_loop(peer)==1 && rtp_records==decoded_before);
  const int repairs_before_idle=repair_ticks;emit_repair=1;
  assert(peer_connection_loop(peer)==0 && repair_ticks==repairs_before_idle+1 && !emit_repair);
  // A same-transport re-offer can update payloads after connection. Preserve
  // the association, SSRCs and decoder; invalid inputs must leave the old pair.
  assert(peer_connection_set_receive_payload_types(peer,102,110)==0);
  assert(peer->remote_assrc==42 && peer->remote_vssrc==43 && peer->state==PEER_CONNECTION_COMPLETED);
  assert(peer_connection_set_receive_payload_types(peer,-1,110)==-1);
  assert(peer->receive_video_payload==102 && peer->receive_audio_payload==110);
  socket_packet[1]=102;socket_size=14;
  assert(peer_connection_loop(peer)==1 && rtp_records==decoded_before+1);
  socket_packet[1]=100;socket_size=14;
  assert(peer_connection_loop(peer)==1 && rtp_records==decoded_before+1);
  PeerTransportStats counters={0};assert(peer_connection_get_transport_stats(peer,&counters)==0);
  assert(counters.rtp_audio_routed==1 && counters.rtp_video_routed==2 && counters.rtp_payload_rejected==2);
  assert(counters.rtp_completed==5 && counters.rtp_last_payload_type==100);
  test_ssrc=99;socket_packet[1]=102;socket_size=14;
  assert(peer_connection_loop(peer)==1 && rtp_records==decoded_before+1);
  assert(peer_connection_get_transport_stats(peer,&counters)==0 && counters.rtp_ssrc_rejected==1);
  rtp_unprotect_result=7;socket_packet[1]=102;socket_size=14;
  assert(peer_connection_loop(peer)==1 && rtp_records==decoded_before+1);
  assert(peer_connection_get_transport_stats(peer,&counters)==0);
  assert(counters.rtp_decrypt_failures==1 && counters.rtp_last_decrypt_error==7 && counters.rtp_last_payload_type==102);
  rtp_unprotect_result=0;
  // Later RTCP and DTLS records overwrite the receive scratch buffer. Neither
  // may change the cached RTP type, route counters, or erase a failure code.
  memset(socket_packet,0,8);socket_packet[0]=0x80;socket_packet[1]=RTCP_RR;socket_packet[3]=1;socket_size=8;
  rtcp_unprotect_result=9;
  assert(peer_connection_loop(peer)==1);
  rtcp_unprotect_result=0;socket_size=8;assert(peer_connection_loop(peer)==1);
  queue_dtls();peer->dtls_srtp.user_data=peer;consume_datagram=1;
  next_dtls_result=MBEDTLS_ERR_SSL_WANT_READ;
  assert(peer_connection_loop(peer)==1);
  assert(peer_connection_get_transport_stats(peer,&counters)==0);
  assert(counters.rtp_last_payload_type==102 && counters.rtp_last_decrypt_error==7);
  assert(counters.rtcp_completed==2 && counters.rtcp_decrypt_failures==1 && counters.rtcp_last_decrypt_error==9);
  assert(counters.rtp_audio_routed==1 && counters.rtp_video_routed==2 && counters.rtp_ssrc_rejected==1);
  peer->state=PEER_CONNECTION_FAILED;
  assert(peer_connection_set_receive_payload_types(peer,100,109)==-1);
  peer->state=PEER_CONNECTION_CLOSED;
  assert(peer_connection_set_receive_payload_types(peer,100,109)==-1);
  free(peer);

  // Incoming DTLS/SCTP packets may exceed our conservative outgoing MTU.
  // Previously the encrypted datagram fit but the plaintext was split at
  // 1300, silently losing the first SCTP fragment and blocking its stream.
  peer=calloc(1,sizeof(*peer));assert(peer);
  peer->state=PEER_CONNECTION_COMPLETED;
  peer->dtls_srtp.user_data=peer;
  queue_dtls();consume_datagram=1;pending_records=0;
  plaintext_remaining=1460;expected_sctp_length=1460;
  const int before_full_record=sctp_records;
  assert(peer_connection_loop(peer)==1);
  assert(sctp_records==before_full_record+1 && plaintext_remaining==0);
  assert(CONFIG_MTU==1300); // This fix must not enlarge outgoing packets.
  assert(sizeof(peer->temp_buf)>=sizeof(peer->agent_buf));
  free(peer);
}

#include <assert.h>

#include "../extern/libpeer/src/peer_connection.c"

static uint32_t now_ms;
static int sender_reports;
static int keyframe_requests;
static int protect_calls;
static int protect_result;
static int send_calls;
static int send_result;
static int short_send;
static uint8_t sent_packet[128];
static int sent_size;

uint32_t ports_get_monotonic_time(void) {
  return now_ms;
}

int dtls_srtp_encrypt_rctp_packet(DtlsSrtp* dtls, uint8_t* packet, int* bytes) {
  (void)dtls;
  protect_calls++;
  if (protect_result != 0)
    return protect_result;
  assert(rtcp_validate_compound(packet, *bytes));
  memset(packet + *bytes, 0xa5, 4);
  *bytes += 4;
  return 0;
}

int agent_send(Agent* agent, const uint8_t* packet, int size) {
  (void)agent;
  send_calls++;
  assert(size <= (int)sizeof(sent_packet));
  memcpy(sent_packet, packet, size);
  sent_size = size;
  return send_result < 0 ? send_result : short_send ? size-1 : size;
}

static void on_sender_report(uint32_t ssrc, uint64_t ntp_us, uint32_t timestamp,
                              void* userdata) {
  (void)userdata;
  assert(ssrc == 42 || ssrc == 43);
  assert(ntp_us == 1500000);
  assert(timestamp == 90000);
  sender_reports++;
}

static void on_keyframe_request(void* userdata) {
  (void)userdata;
  keyframe_requests++;
}

static void incoming_validation(PeerConnection* pc) {
  uint8_t compound[64] = {
    0x80, RTCP_SR, 0, 6, 0, 0, 0, 42,
    0, 0, 0, 1, 0x80, 0, 0, 0, 0, 1, 0x5f, 0x90,
    0, 0, 0, 0, 0, 0, 0, 0,
    0x81, RTCP_PSFB, 0, 2, 0, 0, 0, 42, 0, 0, 0, 1,
  };
  now_ms = 100;
  peer_connection_incoming_rtcp(pc, compound, 40);
  assert(sender_reports == 1 && keyframe_requests == 1);
  assert(pc->video_receiver_stats.last_sr == 0x00018000);
  assert(pc->video_receiver_stats.last_sr_received_ms == 100);

  for (size_t size = 0; size < 28; ++size)
    peer_connection_incoming_rtcp(pc, compound, size);
  for (size_t size = 29; size < 40; ++size)
    peer_connection_incoming_rtcp(pc, compound, size);
  for (size_t size = 41; size <= 43; ++size)
    peer_connection_incoming_rtcp(pc, compound, size);
  assert(sender_reports == 1 && keyframe_requests == 1);
  compound[3] = 1;
  peer_connection_incoming_rtcp(pc, compound, 40);
  assert(sender_reports == 1 && keyframe_requests == 1);
  compound[3] = 6;
  compound[0] = 0x81;
  peer_connection_incoming_rtcp(pc, compound, 40);
  assert(sender_reports == 1 && keyframe_requests == 1);
  compound[0] = 0x80;
  compound[7] = 43;
  now_ms = 200;
  peer_connection_incoming_rtcp(pc, compound, 28);
  assert(sender_reports == 2);
  assert(pc->video_receiver_stats.last_sr_received_ms == 100);
}

static void sending_reports(PeerConnection* pc) {
  uint8_t rtp[14] = {0x80, 96, 0, 100, 0, 0, 0, 90, 0, 0, 0, 42, 0x61, 0x80};
  now_ms = 300;
  assert(rtcp_receiver_record_rtp(&pc->video_receiver_stats, rtp, sizeof(rtp), now_ms));
  peer_connection_maybe_send_receiver_report(pc);
  assert(protect_calls == 0 && send_calls == 0);
  now_ms = 1300;
  peer_connection_maybe_send_receiver_report(pc);
  assert(protect_calls == 1 && send_calls == 1);
  assert(sent_size == 60 && sent_packet[59] == 0xa5);
  assert(rtcp_validate_compound(sent_packet, sent_size - 4));
  assert(pc->video_receiver_stats.received_prior == 1);
  peer_connection_maybe_send_receiver_report(pc);
  assert(protect_calls == 1 && send_calls == 1);

  rtp[3] = 102;
  assert(rtcp_receiver_record_rtp(&pc->video_receiver_stats, rtp, sizeof(rtp), 1400));
  protect_result = -1;
  now_ms = 2300;
  peer_connection_maybe_send_receiver_report(pc);
  assert(protect_calls == 2 && send_calls == 1);
  assert(pc->video_receiver_stats.received_prior == 1);
  now_ms = 2301;
  peer_connection_maybe_send_receiver_report(pc);
  assert(protect_calls == 2);
  protect_result = 0;
  send_result = -1;
  now_ms = 3300;
  peer_connection_maybe_send_receiver_report(pc);
  assert(protect_calls == 3 && send_calls == 2);
  assert(pc->video_receiver_stats.received_prior == 1);
  send_result = 0;
  now_ms = 4300;
  peer_connection_maybe_send_receiver_report(pc);
  assert(protect_calls == 4 && send_calls == 3);
  assert(pc->video_receiver_stats.received_prior == 2);
  assert(sent_packet[12] == 128 && sent_packet[15] == 1);
  pc->state = PEER_CONNECTION_DISCONNECTED;
  now_ms = 5300;
  peer_connection_maybe_send_receiver_report(pc);
  assert(protect_calls == 4 && send_calls == 3);
}

int main(void) {
  PeerConnection pc = {0};
  pc.remote_vssrc = 42;
  pc.vrtp_encoder.ssrc = 1;
  pc.state = PEER_CONNECTION_COMPLETED;
  pc.config.onrtpsenderreport = on_sender_report;
  pc.config.on_request_keyframe = on_keyframe_request;
  incoming_validation(&pc);
  sending_reports(&pc);
  pc.state=PEER_CONNECTION_COMPLETED;
  protect_result=-1;const int before=send_calls;
  assert(!peer_connection_send_video_nack(&pc,101,2) && send_calls==before);
  protect_result=0;short_send=1;
  assert(!peer_connection_send_video_nack(&pc,101,2));
  short_send=0;send_result=-1;
  assert(!peer_connection_send_video_nack(&pc,101,2));
  send_result=0;assert(peer_connection_send_video_nack(&pc,101,2));
  assert(sent_size==20 && sent_packet[1]==RTCP_RTPFB && sent_packet[13]==101 && sent_packet[15]==2);
  // A failed SRTP protection must never put a plaintext PLI on the socket.
  // Likewise, a short datagram write is not a successful keyframe request.
  protect_result=-1;const int before_pli=send_calls;
  assert(peer_connection_request_video_keyframe(&pc)<0 && send_calls==before_pli);
  protect_result=0;short_send=1;
  assert(peer_connection_request_video_keyframe(&pc)<0);
  short_send=0;send_result=-1;
  assert(peer_connection_request_video_keyframe(&pc)<0);
  send_result=0;assert(peer_connection_request_video_keyframe(&pc)==16);
  assert(sent_size==16 && sent_packet[0]==0x81 && sent_packet[1]==RTCP_PSFB);
  assert(sent_packet[7]==1 && sent_packet[11]==42 && sent_packet[12]==0xa5);
  pc.remote_vssrc=0;
  const int before_no_ssrc=send_calls;
  assert(peer_connection_request_video_keyframe(&pc)<0 && send_calls==before_no_ssrc);
  pc.remote_vssrc=42;pc.state=PEER_CONNECTION_DISCONNECTED;
  assert(peer_connection_request_video_keyframe(&pc)<0 && send_calls==before_no_ssrc);
  return 0;
}

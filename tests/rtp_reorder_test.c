#include "rtp.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint32_t now_ms;
static int completed_frames;
static int damage_events;
static uint32_t damage_timestamp;
static uint32_t damage_ssrc;

uint32_t ports_get_epoch_time(void) {
  return now_ms;
}
uint32_t ports_get_monotonic_time(void) { return now_ms; }

static void on_video_packet(const PeerVideoPacket* packet, void* user_data) {
  (void)user_data;
  assert(packet != NULL);
  assert(packet->size > 4);
  completed_frames++;
}

static void on_video_damage(const PeerVideoDamage* damage, void* user_data) {
  (void)user_data;
  assert(damage != NULL);
  damage_events++;
  damage_timestamp = damage->timestamp;
  damage_ssrc = damage->ssrc;
}

static size_t make_packet(uint8_t* packet, uint16_t sequence, uint32_t timestamp) {
  memset(packet, 0, 14);
  packet[0] = 0x80;
  packet[1] = 0x80 | 96;
  packet[2] = (uint8_t)(sequence >> 8);
  packet[3] = (uint8_t)sequence;
  packet[4] = (uint8_t)(timestamp >> 24);
  packet[5] = (uint8_t)(timestamp >> 16);
  packet[6] = (uint8_t)(timestamp >> 8);
  packet[7] = (uint8_t)timestamp;
  packet[11] = 1;
  packet[12] = 0x01;
  packet[13] = 0x80;
  return 14;
}

static void decode(RtpDecoder* decoder, uint16_t sequence, uint32_t timestamp) {
  uint8_t packet[14];
  assert(rtp_decoder_decode(decoder, packet,
                            make_packet(packet, sequence, timestamp)) >= 0);
}

static void fresh_gap_after_expiry(uint32_t start_ms) {
  RtpDecoder decoder;
  now_ms=start_ms;
  rtp_decoder_init(&decoder,CODEC_H264,NULL,NULL);
  rtp_decoder_set_reorder_hold_ms(&decoder,80);
  decode(&decoder,100,90000);
  decode(&decoder,102,93000); // 101 missing since t=0.
  now_ms+=75;
  decode(&decoder,104,96000); // 103 has only just become missing.
  now_ms+=5;
  rtp_decoder_poll(&decoder);
  assert(decoder.forced_sequence_skips==1 && "expiry of 101 prematurely discarded the fresh 103 gap");
  assert(decoder.reorder_buffered_packets==1 && decoder.reorder_expected_sequence==103);
  now_ms+=5;
  decode(&decoder,103,94500);
  assert(decoder.forced_sequence_skips==1 && decoder.late_packets_dropped==0);
  assert(decoder.reorder_buffered_packets==0);
  rtp_decoder_cleanup(&decoder);
}

static void gap_age_is_not_restarted(void) {
  RtpDecoder decoder;
  now_ms=20000;rtp_decoder_init(&decoder,CODEC_H264,NULL,NULL);
  rtp_decoder_set_reorder_hold_ms(&decoder,80);
  decode(&decoder,200,90000);decode(&decoder,202,93000);
  now_ms+=75;decode(&decoder,204,96000);
  decode(&decoder,201,91500); // Original hole recovered normally.
  now_ms+=5;rtp_decoder_poll(&decoder);
  assert(decoder.forced_sequence_skips==0); // Fresh 203 must not inherit t=0.
  now_ms+=74;rtp_decoder_poll(&decoder);
  assert(decoder.forced_sequence_skips==0);
  ++now_ms;rtp_decoder_poll(&decoder);
  assert(decoder.forced_sequence_skips==1 && decoder.reorder_buffered_packets==0);
  rtp_decoder_cleanup(&decoder);

  now_ms=30000;rtp_decoder_init(&decoder,CODEC_H264,NULL,NULL);
  rtp_decoder_set_reorder_hold_ms(&decoder,80);
  decode(&decoder,300,90000);decode(&decoder,302,93000);decode(&decoder,304,96000);
  now_ms+=80;rtp_decoder_poll(&decoder);
  // Both holes are already old: no extra 80 ms wait per buffered frame.
  assert(decoder.forced_sequence_skips==2 && decoder.reorder_buffered_packets==0);
  rtp_decoder_cleanup(&decoder);
}

int main(void) {
  fresh_gap_after_expiry(10000);
  fresh_gap_after_expiry(UINT32_MAX-40);
  gap_age_is_not_restarted();
  RtpDecoder decoder;
  rtp_decoder_init(&decoder, CODEC_H264, NULL, NULL);
  rtp_decoder_set_video_callback(&decoder, on_video_packet);
  rtp_decoder_set_video_damage_callback(&decoder, on_video_damage);

  now_ms = 100;
  decode(&decoder, 100, 90000);
  decode(&decoder, 102, 93000);
  assert(completed_frames == 1);
  assert(decoder.reorder_buffered_packets == 1);

  now_ms += RTP_REORDER_MAX_HOLD_MS - 1;
  decode(&decoder, 103, 94500);
  assert(completed_frames == 1);
  assert(decoder.reorder_buffered_packets == 2);

  now_ms += 1;
  rtp_decoder_poll(&decoder);
  assert(decoder.forced_sequence_skips == 1);
  assert(decoder.access_units_dropped == 1);
  assert(damage_events == 1);
  assert(damage_timestamp == 93000);
  assert(damage_ssrc == 1);
  assert(decoder.reorder_buffered_packets == 0);
  assert(completed_frames == 2);
  rtp_decoder_cleanup(&decoder);

  completed_frames = 0;
  damage_events = 0;
  now_ms = 1000;
  rtp_decoder_init(&decoder, CODEC_H264, NULL, NULL);
  rtp_decoder_set_video_callback(&decoder, on_video_packet);
  rtp_decoder_set_video_damage_callback(&decoder, on_video_damage);
  decode(&decoder, 200, 90000);
  decode(&decoder, 202, 93000);
  decode(&decoder, 201, 91500);
  assert(completed_frames == 3);
  assert(damage_events == 0);
  assert(decoder.reordered_packets == 1);
  assert(decoder.forced_sequence_skips == 0);
  assert(decoder.access_units_dropped == 0);
  assert(decoder.reorder_buffered_packets == 0);
  rtp_decoder_cleanup(&decoder);

  completed_frames = 0;
  now_ms = 2000;
  rtp_decoder_init(&decoder, CODEC_H264, NULL, NULL);
  rtp_decoder_set_video_callback(&decoder, on_video_packet);
  decode(&decoder, 65535, 90000);
  decode(&decoder, 1, 93000);
  decode(&decoder, 0, 91500);
  decode(&decoder, 0, 91500);
  assert(completed_frames == 3);
  assert(decoder.reordered_packets == 1);
  assert(decoder.late_packets_dropped == 1);
  assert(decoder.forced_sequence_skips == 0);
  rtp_decoder_cleanup(&decoder);

  // An IDR burst can overtake one retransmission by >64 packets while still
  // fitting the existing 128-slot allocation and the time budget.
  completed_frames=0;now_ms=3000;
  rtp_decoder_init(&decoder,CODEC_H264,NULL,NULL);
  rtp_decoder_set_video_callback(&decoder,on_video_packet);
  decode(&decoder,100,90000);
  for(uint16_t seq=102;seq<=181;++seq) decode(&decoder,seq,90000+(seq-100)*1500);
  now_ms+=10;
  decode(&decoder,101,91500);
  rtp_decoder_poll(&decoder); // Repair delivery is batched instead of monopolizing the owner.
  assert(decoder.forced_sequence_skips==0 && "queued retransmission was discarded before the allocated window filled");
  assert(decoder.late_packets_dropped==0 && decoder.access_units_dropped==0);
  assert(completed_frames==82 && decoder.reorder_buffered_packets==0);
  rtp_decoder_cleanup(&decoder);

  // A scheduler pause is not proof a packet is absent from the socket. Give
  // the following queued packets one bounded drain chance, then accept 401.
  completed_frames=0;now_ms=4000;
  rtp_decoder_init(&decoder,CODEC_H264,NULL,NULL);
  rtp_decoder_set_video_callback(&decoder,on_video_packet);
  decode(&decoder,400,90000);decode(&decoder,402,93000);
  now_ms+=100;decode(&decoder,403,94500);
  rtp_decoder_poll_after_receive(&decoder,1);
  assert(decoder.forced_sequence_skips==0 && decoder.reorder_buffered_packets==2);
  ++now_ms;decode(&decoder,404,96000);rtp_decoder_poll_after_receive(&decoder,1);
  decode(&decoder,401,91500);rtp_decoder_poll_after_receive(&decoder,1);
  assert(completed_frames==5 && decoder.access_units_dropped==0 && decoder.late_packets_dropped==0);
  rtp_decoder_cleanup(&decoder);

  // Sustained input cannot renew the grace forever. Nor can an empty socket
  // postpone recovery until another packet happens to arrive.
  completed_frames=0;now_ms=5000;
  rtp_decoder_init(&decoder,CODEC_H264,NULL,NULL);
  decode(&decoder,500,90000);decode(&decoder,502,93000);
  now_ms+=RTP_REORDER_MAX_HOLD_MS;
  rtp_decoder_poll_after_receive(&decoder,1);
  assert(decoder.forced_sequence_skips==0);
  now_ms+=RTP_REORDER_DRAIN_GRACE_MS;
  rtp_decoder_poll_after_receive(&decoder,1);
  assert(decoder.forced_sequence_skips==1 && decoder.reorder_buffered_packets==0);
  rtp_decoder_cleanup(&decoder);

  now_ms=6000;rtp_decoder_init(&decoder,CODEC_H264,NULL,NULL);
  decode(&decoder,600,90000);decode(&decoder,602,93000);
  now_ms+=RTP_REORDER_MAX_HOLD_MS;
  rtp_decoder_poll_after_receive(&decoder,1);rtp_decoder_poll_after_receive(&decoder,0);
  assert(decoder.forced_sequence_skips==1);
  rtp_decoder_cleanup(&decoder);

  // Endfield's bounded 80 ms recovery budget accepts a 55 ms retransmission.
  // In-order frames still deliver immediately; no permanent playout delay.
  now_ms=7000;completed_frames=0;rtp_decoder_init(&decoder,CODEC_H264,NULL,NULL);
  rtp_decoder_set_video_callback(&decoder,on_video_packet);
  rtp_decoder_set_reorder_hold_ms(&decoder,80);
  decode(&decoder,700,90000);assert(completed_frames==1);
  decode(&decoder,702,93000);now_ms+=40;rtp_decoder_poll(&decoder);
  assert(decoder.forced_sequence_skips==0);
  now_ms+=15;decode(&decoder,701,91500);
  assert(completed_frames==3 && decoder.access_units_dropped==0);
  decode(&decoder,704,96000);now_ms+=79;rtp_decoder_poll(&decoder);
  assert(decoder.forced_sequence_skips==0);
  ++now_ms;rtp_decoder_poll(&decoder);assert(decoder.forced_sequence_skips==1);
  for(unsigned invalid=0;invalid<3;++invalid) {
    rtp_decoder_set_reorder_hold_ms(&decoder,invalid==0 ? 0 : invalid==1 ? 29 : 121);
    assert(decoder.reorder_max_hold_ms==30);
  }
  rtp_decoder_cleanup(&decoder);

  // Allocation pressure and both clock/sequence rollover stay bounded.
  now_ms=8000;rtp_decoder_init(&decoder,CODEC_H264,NULL,NULL);
  decode(&decoder,800,90000);
  for(uint16_t seq=802;seq<1200;++seq) {
    decode(&decoder,seq,90000+(seq-800)*1500);
    assert(decoder.reorder_buffered_packets<=RTP_REORDER_WINDOW);
  }
  assert(decoder.forced_sequence_skips>0);
  now_ms+=RTP_REORDER_MAX_HOLD_MS;
  for(unsigned poll=0;poll<3 && decoder.reorder_buffered_packets;++poll) rtp_decoder_poll(&decoder);
  assert(decoder.reorder_buffered_packets==0);rtp_decoder_cleanup(&decoder);
  now_ms=UINT32_MAX-20;rtp_decoder_init(&decoder,CODEC_H264,NULL,NULL);
  decode(&decoder,65535,90000);decode(&decoder,1,93000);
  now_ms+=RTP_REORDER_MAX_HOLD_MS;rtp_decoder_poll_after_receive(&decoder,1);
  now_ms+=RTP_REORDER_DRAIN_GRACE_MS;rtp_decoder_poll_after_receive(&decoder,1);
  assert(decoder.forced_sequence_skips==1);rtp_decoder_cleanup(&decoder);
  return 0;
}

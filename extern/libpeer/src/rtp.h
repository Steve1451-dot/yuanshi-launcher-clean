#ifndef RTP_H_
#define RTP_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __BYTE_ORDER
#define __BIG_ENDIAN 4321
#define __LITTLE_ENDIAN 1234
#elif defined(__SWITCH__)
#define __BIG_ENDIAN 4321
#define __LITTLE_ENDIAN 1234
#define __BYTE_ORDER __LITTLE_ENDIAN
#elif __APPLE__
#include <machine/endian.h>
#else
#include <endian.h>
#endif

#include "config.h"
#include "peer_connection.h"

typedef enum RtpPayloadType {

  PT_PCMU = 0,
  PT_PCMA = 8,
  PT_G722 = 9,
  PT_H264 = 96,
  PT_OPUS = 111

} RtpPayloadType;

typedef enum RtpSsrc {

  SSRC_H264 = 1,
  SSRC_PCMA = 4,
  SSRC_PCMU = 5,
  SSRC_OPUS = 6,

} RtpSsrc;

typedef struct RtpHeader {
#if __BYTE_ORDER == __BIG_ENDIAN
  uint16_t version : 2;
  uint16_t padding : 1;
  uint16_t extension : 1;
  uint16_t csrccount : 4;
  uint16_t markerbit : 1;
  uint16_t type : 7;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
  uint16_t csrccount : 4;
  uint16_t extension : 1;
  uint16_t padding : 1;
  uint16_t version : 2;
  uint16_t type : 7;
  uint16_t markerbit : 1;
#endif
  uint16_t seq_number;
  uint32_t timestamp;
  uint32_t ssrc;
  uint32_t csrc[0];

} RtpHeader;

typedef struct RtpPacket {
  RtpHeader header;
  uint8_t payload[0];

} RtpPacket;

typedef struct RtpMap {
  int pt_h264;
  int pt_opus;
  int pt_pcma;

} RtpMap;

typedef struct RtpEncoder RtpEncoder;
typedef struct RtpDecoder RtpDecoder;
typedef void (*RtpOnPacket)(uint8_t* packet, size_t bytes, void* user_data);
typedef void (*RtpOnAudioPacket)(const PeerAudioPacket* packet, void* user_data);
typedef void (*RtpOnVideoPacket)(const PeerVideoPacket* packet, void* user_data);
typedef void (*RtpOnVideoDamage)(const PeerVideoDamage* damage, void* user_data);

// Dynamic 720p frames regularly span more than eight RTP packets. A tiny
// window turns normal UDP burst reordering into artificial packet loss.
#define RTP_REORDER_WINDOW 128
#define RTP_REORDER_MAX_HOLD_PACKETS RTP_REORDER_WINDOW
#define RTP_REORDER_MAX_WINDOW 8192
#define RTP_REORDER_MAX_HOLD_MS 30
#define RTP_REORDER_MAX_RECOVERY_MS 120
#define RTP_REORDER_DRAIN_GRACE_MS 2
#define RTP_REORDER_PACKET_CAPACITY (CONFIG_MTU + 256)

// Packet repair storage, not a video playout queue. Missing entries retain
// their own age/retry state until filled or retired by a bounded deadline.
typedef struct RtpReorderEntry {
  size_t size;
  uint32_t received_ms, missing_since_ms, last_nack_ms;
  uint16_t sequence;
  uint8_t used, missing, nack_attempts;
} RtpReorderEntry;
typedef int (*RtpSendNack)(void* user_data, uint16_t packet_id, uint16_t bitmask);

struct RtpDecoder {
  RtpPayloadType type;
  RtpOnPacket on_packet;
  RtpOnAudioPacket on_audio_packet;
  RtpOnVideoPacket on_video_packet;
  RtpOnVideoDamage on_video_damage;
  int (*decode_func)(RtpDecoder* rtp_decoder, uint8_t* data, size_t size);
  void* user_data;
  uint8_t* nalu_buf;
  size_t nalu_capacity;
  size_t nalu_offset;
  uint8_t* au_buf;
  size_t au_capacity;
  size_t au_offset;
  uint32_t au_timestamp;
  uint32_t au_ssrc;
  uint8_t au_started;
  uint8_t au_has_vcl;
  uint8_t au_has_sps;
  uint8_t au_has_pps;
  uint8_t au_damaged;
  uint8_t cached_sps[512];
  size_t cached_sps_size;
  uint8_t cached_pps[256];
  size_t cached_pps_size;
  uint16_t last_seq_number;
  uint8_t has_last_seq_number;
  uint8_t fragment_started;
  uint8_t fragment_damaged;
  uint32_t packets_received;
  uint32_t sequence_gaps;
  uint32_t nal_units_completed;
  uint32_t access_units_completed;
  uint32_t access_units_dropped;
  uint32_t single_packets;
  uint32_t stap_packets;
  uint32_t fu_packets;
  uint8_t* reorder_buf;
  RtpReorderEntry* reorder_entries;
  uint16_t reorder_capacity, reorder_max_capacity;
  uint16_t reorder_newest_sequence;
  uint8_t reorder_has_newest_sequence;
  uint8_t nack_scan_started;
  uint8_t nack_scan_wait_ms;
  uint32_t nack_scan_ms;
  uint16_t nack_scan_offset;
  uint16_t reorder_expected_sequence;
  uint8_t reorder_has_expected_sequence;
  uint8_t reorder_gap_active;
  uint32_t reorder_gap_started_ms;
  uint16_t reorder_gap_sequence;
  uint32_t reorder_max_hold_ms;
  uint8_t reorder_drain_grace_active;
  uint32_t reorder_drain_grace_started_ms;
  uint32_t reorder_buffered_packets;
  uint32_t reordered_packets;
  uint32_t late_packets_dropped;
  uint32_t forced_sequence_skips;
  uint32_t reorder_timeout_skips, reorder_capacity_skips;
  uint32_t reorder_growths, reorder_growth_failures, reorder_peak_packets;
  uint32_t reorder_growth_failure_ms;
  uint8_t reorder_growth_backoff;
  uint32_t nack_requests, nack_packets_requested, nack_retry_packets;
  uint32_t nack_recovered_packets, nack_exhausted_packets, nack_send_failures;
  uint32_t nack_repair_max_ms;
};

struct RtpEncoder {
  RtpPayloadType type;
  RtpOnPacket on_packet;
  int (*encode_func)(RtpEncoder* rtp_encoder, uint8_t* data, size_t size);
  void* user_data;
  uint16_t seq_number;
  uint32_t ssrc;
  uint32_t timestamp;
  uint32_t timestamp_increment;
  uint8_t buf[CONFIG_MTU + 128];
};

int rtp_packet_validate(uint8_t* packet, size_t size);

void rtp_encoder_init(RtpEncoder* rtp_encoder, MediaCodec codec, RtpOnPacket on_packet, void* user_data);

int rtp_encoder_encode(RtpEncoder* rtp_encoder, const uint8_t* data, size_t size);

void rtp_decoder_init(RtpDecoder* rtp_decoder, MediaCodec codec, RtpOnPacket on_packet, void* user_data);

void rtp_decoder_set_audio_callback(RtpDecoder* rtp_decoder, RtpOnAudioPacket on_packet);
void rtp_decoder_set_video_callback(RtpDecoder* rtp_decoder, RtpOnVideoPacket on_packet);
void rtp_decoder_set_video_damage_callback(RtpDecoder* rtp_decoder, RtpOnVideoDamage on_damage);

void rtp_decoder_cleanup(RtpDecoder* rtp_decoder);

int rtp_decoder_decode(RtpDecoder* rtp_decoder, const uint8_t* data, size_t size);

void rtp_decoder_poll(RtpDecoder* rtp_decoder);
// A bounded chance to read retransmissions already queued in the UDP socket
// after a scheduler stall. Idle polls never extend the loss deadline.
void rtp_decoder_poll_after_receive(RtpDecoder* rtp_decoder, int received_packet);
// Configure only before the first packet. Powers of two, max <= 8192.
// Allocation failure leaves the existing bounded window intact.
int rtp_decoder_set_recovery_window(RtpDecoder* decoder, unsigned initial, unsigned maximum);
// Owner-thread timer: safe on idle polls; callback returns nonzero on send.
void rtp_decoder_poll_nacks(RtpDecoder* decoder, int rtt_ms, RtpSendNack send, void* user_data);
static inline void rtp_decoder_set_reorder_hold_ms(RtpDecoder* decoder,uint32_t milliseconds) {
  decoder->reorder_max_hold_ms = milliseconds>=RTP_REORDER_MAX_HOLD_MS &&
      milliseconds<=RTP_REORDER_MAX_RECOVERY_MS ? milliseconds : RTP_REORDER_MAX_HOLD_MS;
}

uint32_t rtp_get_ssrc(uint8_t* packet);

#endif  // RTP_H_

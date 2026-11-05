/**
 * @file peer_connection.h
 * @brief Struct PeerConnection
 */
#ifndef PEER_CONNECTION_H_
#define PEER_CONNECTION_H_

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SdpType {
  SDP_TYPE_OFFER = 0,
  SDP_TYPE_ANSWER,
} SdpType;

typedef enum PeerConnectionState {

  PEER_CONNECTION_CLOSED = 0,
  PEER_CONNECTION_NEW,
  PEER_CONNECTION_CHECKING,
  PEER_CONNECTION_CONNECTED,
  PEER_CONNECTION_COMPLETED,
  PEER_CONNECTION_FAILED,
  PEER_CONNECTION_DISCONNECTED,

} PeerConnectionState;

typedef enum DataChannelType {

  DATA_CHANNEL_NONE = 0,
  DATA_CHANNEL_STRING,
  DATA_CHANNEL_BINARY,

} DataChannelType;

typedef enum DecpChannelType {
  DATA_CHANNEL_RELIABLE = 0x00,
  DATA_CHANNEL_RELIABLE_UNORDERED = 0x80,
  DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT = 0x01,
  DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT_UNORDERED = 0x81,
  DATA_CHANNEL_PARTIAL_RELIABLE_TIMED = 0x02,
  DATA_CHANNEL_PARTIAL_RELIABLE_TIMED_UNORDERED = 0x82,
} DecpChannelType;

typedef enum MediaCodec {

  CODEC_NONE = 0,

  /* Video */
  CODEC_H264,
  CODEC_VP8,    // not implemented yet
  CODEC_MJPEG,  // not implemented yet

  /* Audio */
  CODEC_OPUS,  // not implemented yet
  CODEC_PCMA,
  CODEC_PCMU,

} MediaCodec;

typedef struct IceServer {
  const char* urls;
  const char* username;
  const char* credential;

} IceServer;

typedef struct PeerAudioPacket {
  uint8_t* data;
  size_t size;
  uint32_t timestamp;
  uint32_t ssrc;
  uint16_t sequence;
  uint8_t payload_type;
  uint8_t marker;
} PeerAudioPacket;

typedef struct PeerVideoPacket {
  uint8_t* data;
  size_t size;
  uint32_t timestamp;
  uint32_t ssrc;
} PeerVideoPacket;

// Emitted only after the reorder/NACK repair window has failed and a complete
// H.264 access unit was destroyed. No payload is exposed: the receiver must
// gate dependent pictures and request a new IDR, not try to decode fragments.
typedef struct PeerVideoDamage {
  uint32_t timestamp;
  uint32_t ssrc;
} PeerVideoDamage;

typedef struct PeerVideoRtpStats {
  uint32_t packets_received;
  uint32_t sequence_gaps;
  uint32_t reordered_packets;
  uint32_t late_packets_dropped;
  uint32_t forced_sequence_skips;
  uint32_t reorder_buffered_packets;
  uint32_t access_units_completed;
  uint32_t access_units_dropped;
  uint32_t nack_requests;
  uint32_t nack_packets_requested;
  uint32_t nack_retry_packets, nack_recovered_packets, nack_exhausted_packets;
  uint32_t nack_send_failures, nack_repair_max_ms;
  uint32_t reorder_timeout_skips, reorder_capacity_skips;
  uint32_t reorder_capacity, reorder_peak_packets, reorder_growths, reorder_growth_failures;
} PeerVideoRtpStats;

typedef struct PeerConfiguration {
  IceServer ice_servers[5];

  MediaCodec audio_codec;
  MediaCodec video_codec;
  DataChannelType datachannel;

  void (*onaudiotrack)(uint8_t* data, size_t size, void* userdata);
  void (*onaudiopacket)(const PeerAudioPacket* packet, void* userdata);
  void (*onvideotrack)(uint8_t* data, size_t size, void* userdata);
  void (*onvideopacket)(const PeerVideoPacket* packet, void* userdata);
  void (*onvideodamage)(const PeerVideoDamage* damage, void* userdata);
  void (*onrtpsenderreport)(uint32_t ssrc, uint64_t ntp_us, uint32_t rtp_timestamp, void* userdata);
  void (*on_request_keyframe)(void* userdata);
  void* user_data;

  // 0 keeps the 30 ms default. Cloud receivers may opt into 30..120 ms
  // retransmission recovery without changing healthy in-order delivery.
  uint32_t video_reorder_hold_ms;
  // Optional on-demand packet repair storage, configured before receiving.
  // Zero keeps the legacy fixed 128 slots. Never changes decoder/playout queues.
  uint16_t video_reorder_initial_packets, video_reorder_max_packets;
} PeerConfiguration;

typedef struct PeerConnection PeerConnection;

// Numeric transport diagnostics only. No endpoints, credentials or fingerprints.
typedef struct PeerTransportStats {
  int dtls_role; // 0 client/active, 1 server/passive
  int dtls_result;
  int dtls_remote_pin_present;
  int dtls_attempts;
  uint32_t dtls_datagrams_sent;
  uint32_t dtls_datagrams_received;
  int ice_local_candidates, ice_remote_candidates, ice_candidate_pairs;
  uint32_t ice_requests_sent, ice_responses_received, ice_responses_rejected;
  uint32_t udp_datagrams_received, udp_send_errors, udp_receive_errors, udp_select_errors;
  int udp_last_error;
  int sctp_create_attempted, sctp_create_result;
  // Numeric media accounting. RTP-shaped input is counted before authentication;
  // no observed RTP does not prove the remote pod never sent media.
  uint32_t rtp_completed, rtp_decrypt_failures, rtcp_completed, rtcp_decrypt_failures;
  uint32_t rtp_last_payload_type;
  uint32_t rtp_payload_rejected, rtp_ssrc_rejected;
  uint32_t rtp_audio_routed, rtp_video_routed;
  int rtp_last_decrypt_error, rtcp_last_decrypt_error;
} PeerTransportStats;
int peer_connection_get_transport_stats(PeerConnection* pc, PeerTransportStats* stats);
// Network-owner negotiated receive payloads, including same-transport re-offers
// after COMPLETED. Does not rotate ICE/DTLS, reset media, or change codec/pins.
int peer_connection_set_receive_payload_types(PeerConnection* pc, int video, int audio);

int peer_connection_get_video_rtp_stats(PeerConnection* pc, PeerVideoRtpStats* stats);

const char* peer_connection_state_to_string(PeerConnectionState state);

PeerConnectionState peer_connection_get_state(PeerConnection* pc);

void* peer_connection_get_sctp(PeerConnection* pc);

// 1 once the SCTP association over the DTLS transport is established, i.e.
// remote data channels can open; 0 before that or after a failure.
int peer_connection_sctp_connected(PeerConnection* pc);

PeerConnection* peer_connection_create(PeerConfiguration* config);

void peer_connection_set_diagnostics_enabled(int enabled);

void peer_connection_destroy(PeerConnection* pc);

void peer_connection_close(PeerConnection* pc);

// Returns 1 when a media/control datagram was consumed, otherwise 0.
int peer_connection_loop(PeerConnection* pc);

int peer_connection_create_datachannel(PeerConnection* pc, DecpChannelType channel_type, uint16_t priority, uint32_t reliability_parameter, char* label, char* protocol);

int peer_connection_create_datachannel_sid(PeerConnection* pc, DecpChannelType channel_type, uint16_t priority, uint32_t reliability_parameter, char* label, char* protocol, uint16_t sid);

/**
 * @brief send message to data channel
 * @param[in] peer connection
 * @param[in] message buffer
 * @param[in] length of message
 */
int peer_connection_datachannel_send(PeerConnection* pc, char* message, size_t len);

int peer_connection_datachannel_send_sid(PeerConnection* pc, char* message, size_t len, uint16_t sid);

int peer_connection_datachannel_send_binary_sid(PeerConnection* pc, char* message, size_t len, uint16_t sid);

int peer_connection_send_audio(PeerConnection* pc, const uint8_t* packet, size_t bytes);

int peer_connection_send_video(PeerConnection* pc, const uint8_t* packet, size_t bytes);

int peer_connection_request_video_keyframe(PeerConnection* pc);

void peer_connection_set_remote_description(PeerConnection* pc, const char* sdp, SdpType sdp_type);

void peer_connection_set_local_description(PeerConnection* pc, const char* sdp, SdpType sdp_type);

const char* peer_connection_create_offer(PeerConnection* pc);

// Opt in before creating an offer. The default remains passive/server for
// existing callers; active/client offers initialize DTLS in the matching role.
// Does not change certificate/fingerprint validation or answerer negotiation.
int peer_connection_set_offer_dtls_client(PeerConnection* pc, int enabled);

const char* peer_connection_create_answer(PeerConnection* pc);

/**
 * @brief register callback function to handle packet loss from RTCP receiver report
 * @param[in] peer connection
 * @param[in] callback function void (*cb)(float fraction_loss, uint32_t total_loss, void *userdata)
 * @param[in] userdata for callback function
 */
void peer_connection_on_receiver_packet_loss(PeerConnection* pc,
                                             void (*on_receiver_packet_loss)(float fraction_loss, uint32_t total_loss, void* userdata));

/**
 * @brief Set the callback function to handle onicecandidate event.
 * @param A PeerConnection.
 * @param A callback function to handle onicecandidate event.
 * @param A userdata which is pass to callback function.
 */
void peer_connection_onicecandidate(PeerConnection* pc, void (*onicecandidate)(char* sdp_text, void* userdata));

/**
 * @brief Set the callback function to handle oniceconnectionstatechange event.
 * @param A PeerConnection.
 * @param A callback function to handle oniceconnectionstatechange event.
 * @param A userdata which is pass to callback function.
 */
void peer_connection_oniceconnectionstatechange(PeerConnection* pc,
                                                void (*oniceconnectionstatechange)(PeerConnectionState state, void* userdata));

/**
 * @brief register callback function to handle event of datachannel
 * @param[in] peer connection
 * @param[in] callback function when message received
 * @param[in] callback function when connection is opened
 * @param[in] callback function when connection is closed
 */
void peer_connection_ondatachannel(PeerConnection* pc,
                                   void (*onmessage)(char* msg, size_t len, void* userdata, uint16_t sid),
                                   void (*onopen)(void* userdata),
                                   void (*onclose)(void* userdata));

// Called once when a remote peer opens a DCEP channel. The label is valid only
// for the duration of the callback; copy it if it must outlive the call.
// ppid is the DCEP control PPID (50) or 0 for the legacy CA variant.
void peer_connection_on_remote_datachannel(PeerConnection* pc,
    void (*onchannelopen)(void* userdata, uint16_t sid, const char* label, uint32_t ppid));

int peer_connection_lookup_sid(PeerConnection* pc, const char* label, uint16_t* sid);

char* peer_connection_lookup_sid_label(PeerConnection* pc, uint16_t sid);

/**
 * @brief adds a new remote candidate to the peer connection
 * @param[in] peer connection
 * @param[in] ice candidate
 */
int peer_connection_add_ice_candidate(PeerConnection* pc, char* ice_candidate);

int peer_connection_get_ice_candidate_pair_stats(PeerConnection* pc,
                                                 int* total,
                                                 int* frozen,
                                                 int* inprogress,
                                                 int* succeeded,
                                                 int* failed);

int peer_connection_get_rtt_ms(PeerConnection* pc);

#ifdef __cplusplus
}
#endif

#endif  // PEER_CONNECTION_H_

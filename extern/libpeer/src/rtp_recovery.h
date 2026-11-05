#ifndef RTP_RECOVERY_H_
#define RTP_RECOVERY_H_
#include "rtp.h"
// Internal depacketizer entry point; public peer API remains unchanged.
int rtp_decoder_decode_reordered(RtpDecoder* decoder, const uint8_t* data, size_t size);
#endif

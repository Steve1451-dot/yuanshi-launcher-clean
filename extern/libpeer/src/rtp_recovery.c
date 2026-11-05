#include "rtp_recovery.h"
#include "ports.h"
#include <stdlib.h>
#include <string.h>

static int resize_window(RtpDecoder* d, unsigned capacity) {
  uint8_t* bytes=malloc((size_t)capacity*RTP_REORDER_PACKET_CAPACITY);
  RtpReorderEntry* entries=calloc(capacity,sizeof(*entries));
  if (!bytes || !entries) { free(bytes);free(entries);return -1; }
  for (unsigned i=0;i<d->reorder_capacity;++i) {
    const RtpReorderEntry* old=&d->reorder_entries[i];
    if (!old->used && !old->missing) continue;
    const size_t slot=old->sequence%capacity;
    entries[slot]=*old;
    if (old->used) memcpy(bytes+slot*RTP_REORDER_PACKET_CAPACITY,
                         d->reorder_buf+(size_t)i*RTP_REORDER_PACKET_CAPACITY,old->size);
  }
  free(d->reorder_buf);free(d->reorder_entries);
  d->reorder_buf=bytes;d->reorder_entries=entries;d->reorder_capacity=(uint16_t)capacity;
  return 0;
}

int rtp_decoder_set_recovery_window(RtpDecoder* d,unsigned initial,unsigned maximum) {
  if (!d || d->type!=PT_H264 || d->reorder_has_expected_sequence ||
      initial<RTP_REORDER_WINDOW || maximum<initial || maximum>RTP_REORDER_MAX_WINDOW ||
      (initial&(initial-1)) || (maximum&(maximum-1))) return -1;
  if (d->reorder_capacity!=initial && resize_window(d,initial)!=0) return -1;
  d->reorder_max_capacity=(uint16_t)maximum;
  return 0;
}

static RtpReorderEntry* entry_for(RtpDecoder* d,uint16_t sequence) {
  return &d->reorder_entries[sequence%d->reorder_capacity];
}

static void drain(RtpDecoder* d,unsigned* budget) {
  while (*budget && d->reorder_buffered_packets) {
    const size_t slot=d->reorder_expected_sequence%d->reorder_capacity;
    RtpReorderEntry* entry=&d->reorder_entries[slot];
    if (!entry->used || entry->sequence!=d->reorder_expected_sequence) break;
    d->decode_func(d,d->reorder_buf+slot*RTP_REORDER_PACKET_CAPACITY,entry->size);
    memset(entry,0,sizeof(*entry));--d->reorder_buffered_packets;
    ++d->reorder_expected_sequence;
    --*budget;
  }
}

static void gap_timer(RtpDecoder* d) {
  if (!d->reorder_buffered_packets) {
    d->reorder_gap_active=0;d->reorder_drain_grace_active=0;return;
  }
  RtpReorderEntry* entry=entry_for(d,d->reorder_expected_sequence);
  if (entry->used && entry->sequence==d->reorder_expected_sequence) {
    d->reorder_gap_active=0;d->reorder_drain_grace_active=0;return;
  }
  if (!entry->missing || entry->sequence!=d->reorder_expected_sequence) return;
  if (!d->reorder_gap_active || d->reorder_gap_sequence!=d->reorder_expected_sequence) {
    d->reorder_gap_active=1;d->reorder_gap_sequence=d->reorder_expected_sequence;
    d->reorder_gap_started_ms=entry->missing_since_ms;d->reorder_drain_grace_active=0;
  }
}

void rtp_decoder_poll(RtpDecoder* d) { rtp_decoder_poll_after_receive(d,0); }

void rtp_decoder_poll_after_receive(RtpDecoder* d,int received_packet) {
  if (!d || d->type!=PT_H264 || !d->reorder_entries) return;
  unsigned budget=64;
  drain(d,&budget);gap_timer(d);
  if (!d->reorder_gap_active) return;
  const uint32_t now=ports_get_monotonic_time();
  if ((uint32_t)(now-d->reorder_gap_started_ms)<d->reorder_max_hold_ms) return;
  if (received_packet) {
    if (!d->reorder_drain_grace_active) {
      d->reorder_drain_grace_active=1;d->reorder_drain_grace_started_ms=now;return;
    }
    if ((uint32_t)(now-d->reorder_drain_grace_started_ms)<RTP_REORDER_DRAIN_GRACE_MS) return;
  }
  while (budget && d->reorder_gap_active &&
         (uint32_t)(now-d->reorder_gap_started_ms)>=d->reorder_max_hold_ms) {
    RtpReorderEntry* entry=entry_for(d,d->reorder_expected_sequence);
    memset(entry,0,sizeof(*entry));++d->reorder_expected_sequence;
    ++d->forced_sequence_skips;++d->reorder_timeout_skips;
    --budget;drain(d,&budget);d->reorder_gap_active=0;d->reorder_drain_grace_active=0;gap_timer(d);
  }
}

static void capacity_advance(RtpDecoder* d,uint16_t target,unsigned* budget) {
  // One bounded metadata pass, even for a huge forward sequence jump. The
  // depacketizer sees the discontinuity and retains its damaged-AU protection.
  const uint16_t advance=(uint16_t)(target-d->reorder_expected_sequence);
  for (unsigned i=0;i<d->reorder_capacity;++i) {
    RtpReorderEntry* entry=&d->reorder_entries[i];
    if ((!entry->used && !entry->missing) || (int16_t)(entry->sequence-target)>=0) continue;
    if (entry->used) --d->reorder_buffered_packets;
    memset(entry,0,sizeof(*entry));
  }
  d->reorder_expected_sequence=target;d->forced_sequence_skips+=advance;
  d->reorder_capacity_skips+=advance;d->reorder_gap_active=0;d->reorder_drain_grace_active=0;
  drain(d,budget);
}

int rtp_decoder_decode_reordered(RtpDecoder* d,const uint8_t* bytes,size_t size) {
  if (!d->reorder_buf || !d->reorder_entries || size>RTP_REORDER_PACKET_CAPACITY ||
      !rtp_packet_validate((uint8_t*)bytes,size)) return -1;
  const uint16_t seq=(uint16_t)(((uint16_t)bytes[2]<<8)|bytes[3]);
  const uint32_t now=ports_get_monotonic_time();
  unsigned work_budget=64;
  if (!d->reorder_has_expected_sequence) {
    d->reorder_has_expected_sequence=1;d->reorder_expected_sequence=seq;
  }
  int16_t distance=(int16_t)(seq-d->reorder_expected_sequence);
  if (distance<0) {++d->late_packets_dropped;return 0;}
  unsigned wanted=d->reorder_capacity;
  while (distance>=(int)wanted && wanted<d->reorder_max_capacity) wanted*=2;
  if (wanted>d->reorder_capacity && (!d->reorder_growth_backoff ||
      (uint32_t)(now-d->reorder_growth_failure_ms)>=1000)) {
    if (resize_window(d,wanted)==0) {++d->reorder_growths;d->reorder_growth_backoff=0;}
    else {++d->reorder_growth_failures;d->reorder_growth_backoff=1;d->reorder_growth_failure_ms=now;}
  }
  if (distance>=d->reorder_capacity) {
    capacity_advance(d,(uint16_t)(seq-d->reorder_capacity+1),&work_budget);
    distance=(int16_t)(seq-d->reorder_expected_sequence);
  }
  int first=0;
  if (d->reorder_has_newest_sequence) {
    first=(int16_t)(d->reorder_newest_sequence-d->reorder_expected_sequence)+1;
    if (first<0) first=0;
  }
  for (int offset=first;offset<distance;++offset) {
    const uint16_t missing=(uint16_t)(d->reorder_expected_sequence+offset);
    RtpReorderEntry* entry=entry_for(d,missing);
    if (entry->used || entry->missing) continue;
    entry->sequence=missing;entry->missing=1;entry->missing_since_ms=now;
  }
  if (!d->reorder_has_newest_sequence || (int16_t)(seq-d->reorder_newest_sequence)>0) {
    d->reorder_has_newest_sequence=1;d->reorder_newest_sequence=seq;
  }
  RtpReorderEntry* entry=entry_for(d,seq);
  if (entry->used) return 0; // Duplicate buffered packet.
  if (entry->missing && entry->sequence==seq && entry->nack_attempts) {
    ++d->nack_recovered_packets;
    const uint32_t age=now-entry->missing_since_ms;
    if (age>d->nack_repair_max_ms) d->nack_repair_max_ms=age;
  }
  memset(entry,0,sizeof(*entry));
  if (distance==0) {
    const int result=d->decode_func(d,(uint8_t*)bytes,size);
    ++d->reorder_expected_sequence;
    if(work_budget) --work_budget;
    drain(d,&work_budget);gap_timer(d);return result;
  }
  const size_t slot=seq%d->reorder_capacity;
  memcpy(d->reorder_buf+slot*RTP_REORDER_PACKET_CAPACITY,bytes,size);
  entry->size=size;entry->sequence=seq;entry->used=1;entry->received_ms=now;
  ++d->reorder_buffered_packets;++d->reordered_packets;
  if (d->reorder_buffered_packets>d->reorder_peak_packets) d->reorder_peak_packets=d->reorder_buffered_packets;
  gap_timer(d);return 0;
}

static int nack_due(const RtpReorderEntry* e,uint32_t now,uint32_t first,uint32_t retry,uint32_t hold) {
  if (!e->missing || e->nack_attempts>=5 || (uint32_t)(now-e->missing_since_ms)>=hold) return 0;
  return !e->nack_attempts ? (uint32_t)(now-e->missing_since_ms)>=first :
                            (uint32_t)(now-e->last_nack_ms)>=retry;
}

void rtp_decoder_poll_nacks(RtpDecoder* d,int rtt_ms,RtpSendNack send,void* userdata) {
  if (!d || d->type!=PT_H264 || !d->reorder_entries || !d->reorder_buffered_packets || !send) return;
  const uint32_t now=ports_get_monotonic_time();
  if (d->nack_scan_started && (uint32_t)(now-d->nack_scan_ms)<d->nack_scan_wait_ms) return;
  // APK-style initial reorder allowance and five-attempt cap. Retry spacing
  // must fit this receiver's deadline, rather than copying Android's 100 ms
  // retry into an 80/120 ms window. Legacy peers retain prompt first NACK.
  const int adaptive=d->reorder_max_capacity>RTP_REORDER_WINDOW;
  const uint32_t first=adaptive ? 16 : 0;
  uint32_t retry=adaptive ? (rtt_ms>0 ? (uint32_t)rtt_ms : 25) : 10;
  if (retry<10) retry=10;
  if (retry>d->reorder_max_hold_ms/3) retry=d->reorder_max_hold_ms/3;
  unsigned packets=0,checked=0;
  uint32_t idle_wait_ms=5;
  unsigned offset=d->nack_scan_offset%d->reorder_capacity;
  // At most two RTCP datagrams every 5 ms; round-robin prevents a large loss
  // burst from starving later holes. No allocations or per-packet logging.
  while (checked<d->reorder_capacity && packets<2) {
    const uint16_t pid=(uint16_t)(d->reorder_expected_sequence+offset);
    RtpReorderEntry* entry=entry_for(d,pid);
    if (entry->sequence==pid && nack_due(entry,now,first,retry,d->reorder_max_hold_ms)) {
      uint16_t mask=0;
      for (unsigned bit=0;bit<16 && offset+bit+1<d->reorder_capacity;++bit) {
        const uint16_t seq=(uint16_t)(pid+bit+1);
        RtpReorderEntry* next=entry_for(d,seq);
        if (next->sequence==seq && nack_due(next,now,first,retry,d->reorder_max_hold_ms)) mask|=(uint16_t)(1u<<bit);
      }
      ++packets;
      if (send(userdata,pid,mask)) {
        ++d->nack_requests;
        for (unsigned bit=0;bit<17;++bit) {
          if (bit && !(mask&(1u<<(bit-1)))) continue;
          RtpReorderEntry* requested=entry_for(d,(uint16_t)(pid+bit));
          if (requested->nack_attempts) ++d->nack_retry_packets;
          ++requested->nack_attempts;++d->nack_packets_requested;requested->last_nack_ms=now;
          if (requested->nack_attempts==5) ++d->nack_exhausted_packets;
        }
      } else ++d->nack_send_failures;
      offset+=17;checked+=17;
    } else {
      if (entry->sequence==pid && entry->missing && entry->nack_attempts<5 &&
          (uint32_t)(now-entry->missing_since_ms)<d->reorder_max_hold_ms) {
        const uint32_t elapsed=entry->nack_attempts ? (uint32_t)(now-entry->last_nack_ms) :
                                                               (uint32_t)(now-entry->missing_since_ms);
        const uint32_t interval=entry->nack_attempts ? retry : first;
        if (elapsed<interval && interval-elapsed<idle_wait_ms) idle_wait_ms=interval-elapsed;
      }
      ++offset;++checked;
    }
    if (offset>=d->reorder_capacity) offset=0;
  }
  // An idle scan is work too. Previously only sends armed this gate, so the
  // first-reorder wait and exhausted retries could rescan all 2048 slots on
  // every busy peer poll. Keep idle work bounded without rounding a known
  // first/retry deadline up by a whole 5 ms. After any send attempt, retain the
  // original two-datagram/5-ms limit, including local transport failures.
  d->nack_scan_started=1;d->nack_scan_ms=now;
  d->nack_scan_wait_ms=(uint8_t)(packets ? 5 : idle_wait_ms);
  d->nack_scan_offset=(uint16_t)offset;
}

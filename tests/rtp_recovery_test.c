#include "rtp.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t now_ms;
static unsigned delivered, sent, failures;
static uint16_t last_pid, last_mask;
uint32_t ports_get_monotonic_time(void) { return now_ms; }
uint32_t ports_get_epoch_time(void) { return 999999; } // Recovery must not use wall time.

static void video(const PeerVideoPacket* packet, void* userdata) {
  (void)userdata; assert(packet && packet->size > 4); ++delivered;
}
static int nack(void* userdata, uint16_t pid, uint16_t mask) {
  (void)userdata; last_pid=pid; last_mask=mask;
  if (failures) { --failures; return 0; }
  ++sent; return 1;
}
static void packet(RtpDecoder* decoder, uint16_t seq) {
  uint8_t bytes[14]={0x80, 0xe0};
  bytes[2]=(uint8_t)(seq>>8); bytes[3]=(uint8_t)seq;
  const uint32_t timestamp=(uint32_t)seq*1500;
  bytes[4]=(uint8_t)(timestamp>>24);bytes[5]=(uint8_t)(timestamp>>16);
  bytes[6]=(uint8_t)(timestamp>>8);bytes[7]=(uint8_t)timestamp;
  bytes[11]=1;bytes[12]=0x01;bytes[13]=0x80;
  assert(rtp_decoder_decode(decoder,bytes,sizeof(bytes))>=0);
}
static void init(RtpDecoder* decoder) {
  delivered=sent=failures=0;
  rtp_decoder_init(decoder,CODEC_H264,NULL,NULL);
  assert(rtp_decoder_set_recovery_window(decoder,512,2048)==0);
  rtp_decoder_set_reorder_hold_ms(decoder,120);
  rtp_decoder_set_video_callback(decoder,video);
}
static void tick(RtpDecoder* decoder,int rtt) {
  rtp_decoder_poll_nacks(decoder,rtt,nack,NULL);
  rtp_decoder_poll(decoder);
}

static void tail_retry(uint32_t start) {
  RtpDecoder d;now_ms=start;init(&d);
  packet(&d,100);packet(&d,102);
  tick(&d,-1);assert(sent==0);
  now_ms=start+15;tick(&d,-1);assert(sent==0);
  now_ms=start+16;tick(&d,-1);assert(sent==1 && last_pid==101 && last_mask==0);
  now_ms=start+41;tick(&d,-1);assert(sent==2); // No further RTP arrives.
  now_ms=start+45;packet(&d,101);tick(&d,-1);
  assert(delivered==3 && d.forced_sequence_skips==0);
  assert(d.nack_recovered_packets==1 && d.nack_retry_packets==1);
  assert(d.nack_repair_max_ms==45 && d.reorder_buffered_packets==0);
  now_ms=start+100;tick(&d,-1);assert(sent==2);
  rtp_decoder_cleanup(&d);
}
static void bounded_retries(void) {
  RtpDecoder d;now_ms=1000;init(&d);packet(&d,65535);packet(&d,1);
  for (now_ms=1000;now_ms<1120;++now_ms) tick(&d,10);
  assert(sent==5 && last_pid==0 && d.nack_exhausted_packets==1);
  assert(d.forced_sequence_skips==0);
  tick(&d,10);assert(sent==5 && d.reorder_timeout_skips==1 && d.forced_sequence_skips==1);
  now_ms=1200;tick(&d,10);assert(sent==5);
  rtp_decoder_cleanup(&d);
}
static void idle_scan_throttling(uint32_t start) {
  RtpDecoder d;now_ms=start;init(&d);packet(&d,100);packet(&d,102);
  tick(&d,10);
  assert(d.nack_scan_started && d.nack_scan_ms==start && d.nack_scan_wait_ms==5);
  for(unsigned i=1;i<5;++i) {
    now_ms=start+i;tick(&d,10);
    assert(d.nack_scan_ms==start && !sent); // No send does not mean rescan on every poll.
  }
  now_ms=start+15;tick(&d,10);
  assert(d.nack_scan_ms==now_ms && d.nack_scan_wait_ms==1 && !sent);
  now_ms=start+16;tick(&d,10);assert(sent==1); // Keep the 16 ms initial deadline.
  for(unsigned i=17;i<=61;++i) {now_ms=start+i;tick(&d,10);}
  assert(sent==5 && d.nack_exhausted_packets==1);
  const uint32_t scanned=d.nack_scan_ms;
  now_ms=scanned+1;tick(&d,10);
  assert(d.nack_scan_ms==scanned && sent==5); // Exhausted holes cannot cause a hot loop either.
  rtp_decoder_cleanup(&d);
}
static void masks_and_failures(void) {
  RtpDecoder d;now_ms=2000;init(&d);
  packet(&d,100);packet(&d,102);packet(&d,104);packet(&d,106);
  now_ms+=16;failures=1;tick(&d,10);
  assert(sent==0 && d.nack_send_failures==1);
  tick(&d,10);assert(sent==0); // Busy owner polling cannot hammer the send queue.
  now_ms+=5;tick(&d,10);assert(sent==1 && last_pid==101 && last_mask==10);
  packet(&d,101);now_ms+=10;tick(&d,10);
  assert(sent==2 && last_pid==103 && last_mask==2);
  packet(&d,103);packet(&d,105);
  assert(delivered==7 && d.nack_recovered_packets==3 && !d.forced_sequence_skips);
  rtp_decoder_cleanup(&d);
}
static void burst_and_capacity(void) {
  RtpDecoder d;now_ms=3000;init(&d);packet(&d,100);
  for(uint16_t seq=102;seq<702;++seq) packet(&d,seq);
  assert(d.reorder_capacity==1024 && d.reorder_growths==1 && !d.forced_sequence_skips);
  now_ms+=60;packet(&d,101);
  assert(delivered==65 && d.reorder_buffered_packets>0); // One old AU plus at most 64 in this owner call.
  for(unsigned poll=0;poll<12 && d.reorder_buffered_packets;++poll) rtp_decoder_poll(&d);
  assert(delivered==602 && !d.reorder_buffered_packets && !d.access_units_dropped);
  assert(rtp_decoder_set_recovery_window(&d,512,8192)<0); // Never reconfigure a live ring.
  packet(&d,10000); // Large sequence jump is bounded, not 9000 per-packet loops.
  assert(d.reorder_capacity==2048 && d.reorder_capacity_skips>0);
  now_ms+=120;
  for(unsigned poll=0;poll<40 && d.reorder_buffered_packets;++poll) tick(&d,10);
  assert(!d.reorder_buffered_packets);
  rtp_decoder_cleanup(&d);assert(!d.reorder_entries && !d.reorder_buf && !d.reorder_capacity);
  now_ms=4000;init(&d);
  assert(rtp_decoder_set_recovery_window(&d,512,8192)==0);
  assert(rtp_decoder_set_recovery_window(&d,511,8192)<0);
  assert(rtp_decoder_set_recovery_window(&d,512,16384)<0);
  rtp_decoder_cleanup(&d);
}
static void old_and_new_holes(void) {
  RtpDecoder d;now_ms=5000;init(&d);packet(&d,100);packet(&d,102);
  now_ms+=110;packet(&d,104);now_ms+=10;tick(&d,-1);
  assert(d.forced_sequence_skips==1 && d.reorder_expected_sequence==103);
  now_ms+=10;packet(&d,103);
  assert(d.forced_sequence_skips==1 && !d.reorder_buffered_packets);
  rtp_decoder_cleanup(&d);
}
static uint32_t repair_at[1024];
static uint8_t repair_planned[1024];
static int lost(unsigned index) {return index>0 && index<1023 && (index%37==0 || index%127<8);}
static int trace_nack(void* userdata,uint16_t pid,uint16_t mask) {
  (void)userdata;
  for(unsigned bit=0;bit<17;++bit) {
    if(bit && !(mask&(1u<<(bit-1)))) continue;
    const unsigned index=(uint16_t)(pid+bit-65000);
    assert(index<1024); // Never ask beyond a packet actually observed later.
    if(lost(index) && !repair_planned[index]) {
      repair_planned[index]=1;repair_at[index]=now_ms+20;
    }
  }
  return 1;
}
static void deterministic_loss_trace(void) {
  RtpDecoder d;now_ms=0;init(&d);
  memset(repair_at,0,sizeof(repair_at));memset(repair_planned,0,sizeof(repair_planned));
  for(now_ms=0;now_ms<2200;++now_ms) {
    if(now_ms<2048 && now_ms%2==0) {
      const unsigned index=now_ms/2;
      if(!lost(index)) packet(&d,(uint16_t)(65000+index));
    }
    for(unsigned index=0;index<1024;++index) if(repair_at[index] && repair_at[index]==now_ms) {
      packet(&d,(uint16_t)(65000+index));repair_at[index]=0;
    }
    rtp_decoder_poll_nacks(&d,20,trace_nack,NULL);rtp_decoder_poll(&d);
  }
  printf("synthetic trace: source=1024 delivered=%u repaired=%u timeouts=%u capacity_skips=%u\n",
         delivered,d.nack_recovered_packets,d.reorder_timeout_skips,d.reorder_capacity_skips);
  assert(delivered==1024 && d.nack_recovered_packets>70);
  assert(d.forced_sequence_skips==0 && d.access_units_dropped==0 && d.reorder_buffered_packets==0);
  rtp_decoder_cleanup(&d);
}
int main(void) {
  tail_retry(0);tail_retry(UINT32_MAX-20);
  bounded_retries();masks_and_failures();burst_and_capacity();old_and_new_holes();
  idle_scan_throttling(6000);idle_scan_throttling(UINT32_MAX-20);
  deterministic_loss_trace();
  return 0;
}

#include "rtp.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static uint32_t now_ms;
static int fail_alloc,fail_calloc;
static void* recovery_malloc(size_t size) {return fail_alloc ? NULL : malloc(size);}
static void* recovery_calloc(size_t count,size_t size) {return fail_calloc ? NULL : calloc(count,size);}
#define malloc recovery_malloc
#define calloc recovery_calloc
#include "../extern/libpeer/src/rtp_recovery.c"
#undef malloc
#undef calloc
uint32_t ports_get_monotonic_time(void) {return now_ms;}
uint32_t ports_get_epoch_time(void) {return 0;}
static void receive(RtpDecoder* decoder,uint16_t seq) {
  uint8_t bytes[14]={0x80,0xe0};bytes[2]=(uint8_t)(seq>>8);bytes[3]=(uint8_t)seq;
  bytes[11]=1;bytes[12]=0x01;bytes[13]=0x80;
  assert(rtp_decoder_decode(decoder,bytes,sizeof(bytes))>=0);
}
int main(void) {
  for(int kind=0;kind<2;++kind) {
    RtpDecoder d;fail_alloc=fail_calloc=0;now_ms=0;
    rtp_decoder_init(&d,CODEC_H264,NULL,NULL);
    uint8_t* original=d.reorder_buf;
    fail_alloc=kind==0;fail_calloc=kind==1;
    assert(rtp_decoder_set_recovery_window(&d,512,2048)<0);
    assert(d.reorder_buf==original && d.reorder_capacity==128);
    fail_alloc=fail_calloc=0;
    assert(rtp_decoder_set_recovery_window(&d,512,2048)==0);
    receive(&d,100);receive(&d,102);original=d.reorder_buf;
    fail_alloc=kind==0;fail_calloc=kind==1;
    receive(&d,700);
    assert(d.reorder_buf==original && d.reorder_capacity==512 && d.reorder_growth_failures==1);
    receive(&d,1300);assert(d.reorder_growth_failures==1); // OOM cannot become an allocation storm.
    fail_alloc=fail_calloc=0;now_ms=1001;
    receive(&d,2200);assert(d.reorder_capacity==2048 && d.reorder_growths==1);
    rtp_decoder_cleanup(&d);
  }
}

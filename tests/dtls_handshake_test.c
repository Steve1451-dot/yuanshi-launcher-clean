// Real Mbed TLS DTLS and libsrtp over bounded in-memory datagrams. No sockets,
// accounts, captured sessions, certificates or key material are written out.
#include "dtls_srtp.h"
#include "socket.h"
#include <assert.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif
// The vendored Mbed TLS config has Switch platform entropy disabled. Mirror
// the production Switch entropy registration with host OS entropy in this
// test's DTLS object only; do not use the upstream null-entropy test mode.
static int host_entropy(void* unused,unsigned char* output,size_t length,size_t* written) {
  (void)unused;
#ifdef _WIN32
  assert(BCryptGenRandom(NULL,output,(ULONG)length,BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0);
  *written=length;
#else
  FILE* random=fopen("/dev/urandom","rb");assert(random);
  *written=fread(output,1,length,random);fclose(random);assert(*written==length);
#endif
  return 0;
}
void test_entropy_init(mbedtls_entropy_context* ctx) {
  mbedtls_entropy_init(ctx);
  assert(mbedtls_entropy_add_source(ctx,host_entropy,NULL,32,MBEDTLS_ENTROPY_SOURCE_STRONG)==0);
}

typedef struct Endpoint {
  DtlsSrtp dtls;
  struct Endpoint* remote;
  unsigned char packets[64][4096];
  size_t lengths[64];
  size_t head, count;
  int drop_next, dropped, sent;
} Endpoint;
static uint64_t test_now_ms = 1000;
uint64_t cloudgenshin_test_now_ms(void) { return test_now_ms; }
static int send_datagram(void* context,const unsigned char* data,size_t size) {
  Endpoint* local=((DtlsSrtp*)context)->user_data;
  ++local->sent;
  if(local->drop_next) { --local->drop_next; ++local->dropped; return (int)size; }
  Endpoint* remote=local->remote;
  assert(size<=4096 && remote->count<64);
  const size_t index=(remote->head+remote->count++)%64;
  memcpy(remote->packets[index],data,size);remote->lengths[index]=size;
  return (int)size;
}
static int receive_datagram(void* context,unsigned char* data,size_t size) {
  Endpoint* endpoint=((DtlsSrtp*)context)->user_data;
  if(!endpoint->count) return MBEDTLS_ERR_SSL_WANT_READ;
  const size_t length=endpoint->lengths[endpoint->head];assert(length<=size);
  memcpy(data,endpoint->packets[endpoint->head],length);
  endpoint->head=(endpoint->head+1)%64;endpoint->count--;
  return (int)length;
}
// The public DTLS object has default socket callbacks; this test supplies its
// own memory callbacks immediately after init, so these must never be used.
int udp_socket_sendto(UdpSocket* s,Address* a,const uint8_t* p,int n){(void)s;(void)a;(void)p;(void)n;abort();}
int udp_socket_recvfrom(UdpSocket* s,Address* a,uint8_t* p,int n){(void)s;(void)a;(void)p;(void)n;abort();}
void ports_sleep_ms(int ms){(void)ms;abort();}
static void run(int bad_fingerprint, int lose_first_client_packet) {
  Endpoint* client=calloc(1,sizeof(*client));Endpoint* server=calloc(1,sizeof(*server));assert(client && server);
  client->remote=server;server->remote=client;
  client->drop_next=lose_first_client_packet;
  int init_result=dtls_srtp_init(&client->dtls,DTLS_SRTP_ROLE_CLIENT,client);
  if(init_result) fprintf(stderr,"DTLS init failed code=%d\n",init_result);
  assert(init_result==0);
  init_result=dtls_srtp_init(&server->dtls,DTLS_SRTP_ROLE_SERVER,server);
  if(init_result) fprintf(stderr,"DTLS init failed code=%d\n",init_result);
  assert(init_result==0);
  assert(mbedtls_pk_get_bitlen(&client->dtls.pkey)==256);
  strcpy(client->dtls.remote_fingerprint,server->dtls.local_fingerprint);
  strcpy(server->dtls.remote_fingerprint,client->dtls.local_fingerprint);
  if(bad_fingerprint) client->dtls.remote_fingerprint[0]=client->dtls.remote_fingerprint[0]=='0' ? '1' : '0';
  client->dtls.udp_send=server->dtls.udp_send=send_datagram;
  client->dtls.udp_recv=server->dtls.udp_recv=receive_datagram;
  // Repeated nonblocking polls must preserve the server's handshake state.
  assert(dtls_srtp_handshake(&server->dtls,NULL)==MBEDTLS_ERR_SSL_WANT_READ);
  assert(server->dtls.state==DTLS_SRTP_STATE_HANDSHAKE);
  assert(dtls_srtp_handshake(&server->dtls,NULL)==MBEDTLS_ERR_SSL_WANT_READ);
  int client_done=0,server_done=0,failed=0;
  for(int i=0;i<10000 && !(client_done && server_done) && !failed;i++) {
    test_now_ms += 10; // Deterministic elapsed wall time, no sleeps or busy wait.
    DtlsSrtp* endpoints[]={&client->dtls,&server->dtls};int* completed[]={&client_done,&server_done};
    for(int j=0;j<2;j++) if(!*completed[j]) {
      const int result=dtls_srtp_handshake(endpoints[j],NULL);
      if(result==0) *completed[j]=1;
      else if(result!=MBEDTLS_ERR_SSL_WANT_READ && result!=MBEDTLS_ERR_SSL_WANT_WRITE) {
        assert(bad_fingerprint && j==0 && result==MBEDTLS_ERR_X509_CERT_VERIFY_FAILED);failed=1;break;
      }
    }
  }
  if(bad_fingerprint) assert(failed && !client_done);
  else {
    assert(client_done && server_done);
    assert(client->dropped==lose_first_client_packet);
    if(lose_first_client_packet) assert(client->sent>1);
    uint8_t packet[256]={0x80,0x60,0,1,0,0,0,0,0,0,0,1,0x65,0x80};int bytes=14;
    assert(srtp_protect(client->dtls.srtp_out,packet,&bytes)==srtp_err_status_ok && bytes>14);
    assert(srtp_unprotect(server->dtls.srtp_in,packet,&bytes)==srtp_err_status_ok && bytes==14 && packet[12]==0x65);
    // Reproduce the receiver boundary with real encrypted DTLS records. Only
    // this in-memory server uses a larger send MTU; production sends stay at
    // their conservative MTU and still need to receive larger peer packets.
    mbedtls_ssl_set_mtu(&server->dtls.ssl, 1600);
    unsigned char payload[1460], received[2048];
    memset(payload, 0x55, sizeof(payload));
    assert(dtls_srtp_write(&server->dtls, payload, sizeof(payload)) == sizeof(payload));
    // Mbed TLS does not reject the undersized destination: it leaves a tail.
    assert(dtls_srtp_read(&client->dtls, received, 1300) == 1300);
    assert(mbedtls_ssl_check_pending(&client->dtls.ssl));
    assert(dtls_srtp_read(&client->dtls, received, sizeof(received)) == 160);
    assert(!mbedtls_ssl_check_pending(&client->dtls.ssl));
    assert(dtls_srtp_write(&server->dtls, payload, sizeof(payload)) == sizeof(payload));
    assert(dtls_srtp_read(&client->dtls, received, sizeof(received)) == sizeof(payload));
    assert(memcmp(payload, received, sizeof(payload)) == 0);
    assert(!mbedtls_ssl_check_pending(&client->dtls.ssl));
  }
  dtls_srtp_deinit(&client->dtls);dtls_srtp_deinit(&server->dtls);free(client);free(server);
}
int main(void) {
  assert(srtp_init()==srtp_err_status_ok);run(0,0);run(1,0);run(0,1);assert(srtp_shutdown()==srtp_err_status_ok);
  puts("PASS dtls_real_handshake_srtp_and_fingerprint_rejection");
}

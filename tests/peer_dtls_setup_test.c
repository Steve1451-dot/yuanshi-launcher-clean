#include <assert.h>
#include "../extern/libpeer/src/peer_connection.c"

void agent_set_remote_description(Agent* agent,char* description){(void)agent;(void)description;}
void agent_update_candidate_pairs(Agent* agent){(void)agent;}
void agent_clear_candidates(Agent* agent){(void)agent;}
void agent_create_ice_credential(Agent* agent){strcpy(agent->local_ufrag,"test");strcpy(agent->local_upwd,"test");}
void agent_gather_candidate(Agent* agent,const char* url,const char* username,const char* credential){(void)agent;(void)url;(void)username;(void)credential;}
void agent_get_local_description(Agent* agent,char* description,int size){(void)agent;assert(size>0);description[0]=0;}
int agent_recv_nonblocking(Agent* agent,uint8_t* buffer,int size){(void)agent;(void)buffer;(void)size;return -1;}
int agent_send(Agent* agent,const uint8_t* data,int size){(void)agent;(void)data;return size;}
void dtls_srtp_reset_session(DtlsSrtp* dtls){(void)dtls;}
void dtls_srtp_deinit(DtlsSrtp* dtls){dtls->initialized=0;}
int dtls_srtp_init(DtlsSrtp* dtls,DtlsSrtpRole role,void* data){dtls->role=role;dtls->user_data=data;dtls->initialized=1;strcpy(dtls->local_fingerprint,"AA");return 0;}
// Referenced only by the unchanged SSRC update branch, not this test's offers.
void rtp_decoder_cleanup(RtpDecoder* decoder){(void)decoder;}
int rtp_decoder_set_recovery_window(RtpDecoder* decoder,unsigned initial,unsigned maximum){(void)decoder;(void)initial;(void)maximum;return 0;}
void rtp_decoder_init(RtpDecoder* decoder,MediaCodec codec,void(*callback)(uint8_t*,size_t,void*),void* data){(void)decoder;(void)codec;(void)callback;(void)data;}
void rtp_decoder_set_video_callback(RtpDecoder* decoder,void(*callback)(const PeerVideoPacket*,void*)){(void)decoder;(void)callback;}
void rtp_decoder_set_video_damage_callback(RtpDecoder* decoder,void(*callback)(const PeerVideoDamage*,void*)){(void)decoder;(void)callback;}
void rtp_decoder_set_audio_callback(RtpDecoder* decoder,void(*callback)(const PeerAudioPacket*,void*)){(void)decoder;(void)callback;}
int main(void) {
  assert(peer_connection_set_offer_dtls_client(NULL,1)==-1);
  for(int active=0;active<2;++active) {
    PeerConnection* peer=calloc(1,sizeof(*peer));assert(peer);
    assert(peer_connection_set_offer_dtls_client(peer,2)==-1);
    if(active) assert(peer_connection_set_offer_dtls_client(peer,1)==0);
    const char* offer=peer_connection_create_offer(peer);assert(offer);
    assert(strstr(offer,active ? "a=setup:active" : "a=setup:passive"));
    PeerTransportStats stats;assert(peer_connection_get_transport_stats(peer,&stats)==0);
    assert(stats.dtls_role==(active ? DTLS_SRTP_ROLE_CLIENT : DTLS_SRTP_ROLE_SERVER));
    assert(peer_connection_set_offer_dtls_client(peer,!active)==-1);
    free(peer);
  }
  const char* remote[]={"active","passive","actpass"};
  for(int i=0;i<3;i++) {
    PeerConnection* peer=calloc(1,sizeof(*peer));assert(peer);
    char offer[128];snprintf(offer,sizeof(offer),"v=0\r\na=setup:%s\r\n",remote[i]);
    peer_connection_set_remote_description(peer,offer,SDP_TYPE_OFFER);
    const char* answer=peer_connection_create_answer(peer);assert(answer);
    assert(strstr(answer,i==0 ? "a=setup:passive" : "a=setup:active"));
    PeerTransportStats stats;assert(peer_connection_get_transport_stats(peer,&stats)==0);
    assert(stats.dtls_role==(i==0 ? DTLS_SRTP_ROLE_SERVER : DTLS_SRTP_ROLE_CLIENT));
    peer->agent.local_candidates_count=1;peer->agent.remote_candidates_count=2;
    peer->agent.candidate_pairs_num=2;peer->agent.binding_requests_sent=3;
    peer->agent.last_socket_error=5;
    assert(peer_connection_get_transport_stats(peer,&stats)==0);
    assert(stats.ice_local_candidates==1 && stats.ice_remote_candidates==2 && stats.ice_candidate_pairs==2);
    assert(stats.ice_requests_sent==3 && stats.udp_last_error==5);
    free(peer);
  }
  // The answer digest must be parsed from after "a=fingerprint:sha-256 ", not
  // from offset 22 (which kept "sha-256" glued to the front and made every real
  // DTLS handshake fail with a fingerprint mismatch). A fresh peer has empty
  // ICE credentials, so set_remote_description reaches the parsing loop only:
  // exactly the code path that was wrong.
  {
    PeerConnection* peer=calloc(1,sizeof(*peer));assert(peer);
    const char* digest="11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00";
    char answer[256];
    // The remote-description loop skips a LEADING "v=0\r\n" and then requires
    // every line to start with "\r\n" (the SDP it parses is candidate text that
    // begins with \r\n). Real answers carry a candidate section, so this shape
    // matches what reaches the parser.
    snprintf(answer,sizeof(answer),
             "v=0\r\n\r\na=candidate:1 1 udp 1 10.0.0.1 5000 typ host\r\n"
             "a=fingerprint:sha-256 %s\r\na=setup:active\r\n",digest);
    peer_connection_set_remote_description(peer,answer,SDP_TYPE_ANSWER);
    // A plain assert() aborts the process with exit code 0xc0000409 on MinGW,
    // which ctest then reports without any output; fail explicitly instead so
    // a regression names itself.
    if (strcmp(peer->dtls_srtp.remote_fingerprint,digest)!=0) {
      fprintf(stderr,"FAIL answer fingerprint not parsed from the hash value: got [%s]\n",
              peer->dtls_srtp.remote_fingerprint);
      return 1;
    }
    // a=setup:active in the answer means the pod is the DTLS client.
    if (peer->answer_dtls_role!=DTLS_SRTP_ROLE_SERVER) {
      fprintf(stderr,"FAIL answer a=setup:active did not select the server role\n");
      return 1;
    }
    free(peer);
  }
  {
    PeerConnection* peer=calloc(1,sizeof(*peer));assert(peer);
    const char* digest="11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00";
    char offer[256];
    snprintf(offer,sizeof(offer),"v=0\r\na=fingerprint:sha-256 %s\r\na=setup:actpass\r\n",digest);
    peer_connection_set_remote_description(peer,offer,SDP_TYPE_OFFER);
    if(strcmp(peer->dtls_srtp.remote_fingerprint,digest)!=0) return 1;
    if(!peer_connection_create_answer(peer) || strcmp(peer->dtls_srtp.remote_fingerprint,digest)!=0) {
      fprintf(stderr,"FAIL creating local answer erased the authenticated remote offer fingerprint\n");
      return 1;
    }
    PeerTransportStats identity_stats;assert(peer_connection_get_transport_stats(peer,&identity_stats)==0);
    assert(identity_stats.dtls_remote_pin_present==1);
    // A NEW description without a pin must not inherit the old identity.
    peer_connection_set_remote_description(peer,"v=0\r\na=setup:actpass\r\n",SDP_TYPE_OFFER);
    if(peer->dtls_srtp.remote_fingerprint[0]!=0) {
      fprintf(stderr,"FAIL new remote description inherited stale fingerprint\n");return 1;
    }
    assert(peer_connection_get_transport_stats(peer,&identity_stats)==0 && identity_stats.dtls_remote_pin_present==0);
    free(peer);
  }
  {
    PeerConnection* peer=calloc(1,sizeof(*peer));assert(peer);
    peer->config.video_codec=CODEC_H264;peer->config.video_reorder_hold_ms=80;
    peer->remote_vssrc=1;peer->vrtp_decoder.reorder_max_hold_ms=1;
    peer_connection_set_remote_description(peer,
      "v=0\r\n\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\na=ssrc:22 cname:synthetic\r\n",SDP_TYPE_ANSWER);
    assert(peer->remote_vssrc==22 && peer->vrtp_decoder.reorder_max_hold_ms==80);
    peer->config.video_reorder_hold_ms=0;
    peer_connection_set_remote_description(peer,
      "v=0\r\n\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\na=ssrc:23 cname:synthetic\r\n",SDP_TYPE_ANSWER);
    assert(peer->remote_vssrc==23 && peer->vrtp_decoder.reorder_max_hold_ms==30);
    free(peer);
  }
  puts("PASS peer_dtls_setup_roles_and_remote_identity_lifetime");
}

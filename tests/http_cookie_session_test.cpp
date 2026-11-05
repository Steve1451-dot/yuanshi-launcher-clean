#include "http_client.hpp"
#include <cassert>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace {
struct FakeCurl {
    long verify_peer=-1, verify_host=-1, follow=-1;
    CURLSH* shared=nullptr;
    bool cookie_engine=false, cleaned=false, performed=false;
    size_t (*write)(char*,size_t,size_t,void*)=nullptr;
    void* userdata=nullptr;
} state;
CURLoption reject_option=CURLOPT_LASTENTRY;
CURLcode perform_result=CURLE_OK;
char cookie_owner;
bool Throws(const auto& action) {
    try {action();} catch(const std::runtime_error&) {return true;}
    return false;
}
}
extern "C" {
CURL* curl_easy_init() {state={};return reinterpret_cast<CURL*>(&state);}
void curl_easy_cleanup(CURL*) {state.cleaned=true;}
CURLcode curl_easy_setopt(CURL*,CURLoption option,...) {
    if(option==reject_option) return CURLE_UNKNOWN_OPTION;
    va_list args;va_start(args,option);
    switch(option) {
    case CURLOPT_SSL_VERIFYPEER:state.verify_peer=va_arg(args,long);break;
    case CURLOPT_SSL_VERIFYHOST:state.verify_host=va_arg(args,long);break;
    case CURLOPT_FOLLOWLOCATION:state.follow=va_arg(args,long);break;
    case CURLOPT_SHARE:state.shared=va_arg(args,CURLSH*);break;
    case CURLOPT_COOKIEFILE:assert(std::strcmp(va_arg(args,const char*),"")==0);state.cookie_engine=true;break;
    case CURLOPT_COOKIEJAR:assert(false && "Cookies must not be written to a file");break;
    case CURLOPT_WRITEFUNCTION:state.write=va_arg(args,decltype(state.write));break;
    case CURLOPT_WRITEDATA:state.userdata=va_arg(args,void*);break;
    default:break;
    }
    va_end(args);return CURLE_OK;
}
CURLcode curl_easy_perform(CURL*) {
    state.performed=true;
    assert(state.verify_peer==1 && state.verify_host==2);
    if(perform_result!=CURLE_OK) return perform_result;
    char body[]="{}";
    return state.write(body,1,2,state.userdata)==2 ? CURLE_OK : CURLE_WRITE_ERROR;
}
CURLcode curl_easy_getinfo(CURL*,CURLINFO info,...) {
    va_list args;va_start(args,info);
    if(info==CURLINFO_RESPONSE_CODE) *va_arg(args,long*)=200;
    else if(info==CURLINFO_CONNECT_TIME) *va_arg(args,double*)=0.01;
    else if(info==CURLINFO_NAMELOOKUP_TIME_T) *va_arg(args,curl_off_t*)=100;
    else if(info==CURLINFO_CONNECT_TIME_T) *va_arg(args,curl_off_t*)=10000;
    else if(info==CURLINFO_APPCONNECT_TIME_T) *va_arg(args,curl_off_t*)=30000;
    else if(info==CURLINFO_PRETRANSFER_TIME_T) *va_arg(args,curl_off_t*)=31000;
    else if(info==CURLINFO_STARTTRANSFER_TIME_T) *va_arg(args,curl_off_t*)=0;
    else if(info==CURLINFO_TOTAL_TIME_T) *va_arg(args,curl_off_t*)=30000000;
    else if(info==CURLINFO_SIZE_UPLOAD_T) *va_arg(args,curl_off_t*)=2;
    else assert(false);
    va_end(args);return CURLE_OK;
}
const char* curl_easy_strerror(CURLcode) {return "synthetic transport failure";}
curl_version_info_data* curl_version_info(CURLversion) {static curl_version_info_data version{};return &version;}
curl_slist* curl_slist_append(curl_slist* list,const char* text) {
    auto* node=static_cast<curl_slist*>(std::calloc(1,sizeof(curl_slist)));
    node->data=static_cast<char*>(std::malloc(std::strlen(text)+1));std::strcpy(node->data,text);
    if(!list) return node;
    auto* tail=list;while(tail->next) tail=tail->next;tail->next=node;return list;
}
void curl_slist_free_all(curl_slist* list) {
    while(list) {auto* next=list->next;std::free(list->data);std::free(list);list=next;}
}
}
int main() {
    const opennow::HttpClient client;
    auto* cookies=reinterpret_cast<CURLSH*>(&cookie_owner);
    const auto request=[&](opennow::HttpTransferControl options={}) {
        return client.Post("https://example.invalid/login","Test",{"Content-Type: application/json"},"{}",{},options);
    };
    assert(request().body=="{}" && state.cleaned && !state.shared && !state.cookie_engine);
    assert(request({.max_body_bytes=4096,.follow_redirects=false,.cookie_session=cookies}).status_code==200);
    assert(state.shared==cookies && state.cookie_engine && state.follow==0 && state.cleaned);
    // A subsequent ordinary request must not inherit the login's cookie engine.
    request();assert(!state.shared && !state.cookie_engine);
    for(const auto option:{CURLOPT_SHARE,CURLOPT_COOKIEFILE,CURLOPT_SSL_VERIFYPEER,CURLOPT_SSL_VERIFYHOST}) {
        reject_option=option;
        assert(Throws([&]{request({.cookie_session=cookies});}));
        assert(state.cleaned && !state.performed);
    }
    reject_option=CURLOPT_LASTENTRY;
    assert(Throws([&]{request({.max_body_bytes=1});}));
    assert(state.cleaned);
    perform_result=CURLE_OPERATION_TIMEDOUT;
    try {request();assert(false);} catch(const opennow::HttpRequestError& error) {
        assert(state.cleaned && error.curl_code==CURLE_OPERATION_TIMEDOUT && error.http_status==200);
        const auto& d=error.diagnostics;
        assert(d.captured && d.requires_tls && d.lookup_ms==1 && d.connect_ms==10 && d.tls_ms==30);
        assert(d.ready_ms==31 && d.first_byte_ms==0 && d.total_ms==30000 && d.uploaded_bytes==2);
    }
}

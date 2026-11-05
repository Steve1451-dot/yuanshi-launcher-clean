#include "http_verification_header_probe.hpp"
#include <cassert>
#include <string>
int main() {
    opennow::VerificationHeaderProbe probe;
    probe.consume("Set-Cookie: private\r\n");assert(!probe.aigis_present);
    probe.consume("x-rpc-aigis: \r\n");assert(!probe.aigis_present);
    probe.consume("X-RPC-AIGIS: synthetic-challenge\r\n");assert(probe.aigis_present);
    probe.consume("HTTP/1.1 100 Continue\r\n");assert(!probe.aigis_present);
    probe.consume("x-rpc-aigis-unknown: x\r\n");assert(!probe.aigis_present);
    probe.consume("x-rpc-aigis:"+std::string(16384,'x'));assert(!probe.aigis_present);
    probe.consume("x-rpc-aigis: synthetic\r\n");assert(probe.aigis_present);
    probe.consume("HTTP/2 200\r\n");assert(!probe.aigis_present);
}

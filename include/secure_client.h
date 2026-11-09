#pragma once
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "tls_diagnostics.h"
// Use the core 3.x verifier, not the Core2/core 2.x private bundle adapter.
extern const uint8_t tab5BundleEnd[] asm("_binary_certs_tab5_crt_bundle_bin_end");
#include "project_bundle.h"
void serviceLiveDuringHttp();
class SecureClient : public WiFiClientSecure {
  uint32_t verifyFlags=0;
  bool yieldReads=true;
public:
  explicit SecureClient(const uint8_t *bundle,bool cooperativeReads=true):yieldReads(cooperativeReads) {
    // SDK-indexed roots stay in flash; do not parse 118 PEM roots into internal RAM.
    setCACertBundle(bundle, size_t(tab5BundleEnd-bundle));
    setHandshakeTimeout(12);
  }
  // HTTPClient can poll available() continuously while TLS data dribbles in.
  // A yielding-but-ready task can starve the pinned P4 CPU0 idle watchdog task.
  // Block for a scheduler tick, retaining the normal HTTP and handshake deadlines.
  int available() override {
    if(yieldReads){vTaskDelay(1);serviceLiveDuringHttp();}
    return WiFiClientSecure::available();
  }
  uint32_t verificationFlags() const { return verifyFlags; }
  void stop() override {
    if (sslclient) {
      uint32_t flags=mbedtls_ssl_get_verify_result(&sslclient->ssl_ctx);
      if(flags!=0xffffffffu) verifyFlags|=flags;
      verifyFlags|=project_bundle_last_verify_flags();
    }
    WiFiClientSecure::stop();
  }
};

/* HTTP/3 dependency detection probe (see the Makefile HAVE_HTTP3 block).
 * Compiles and links only when ngtcp2 + ngtcp2_crypto_ossl + nghttp3 (and the
 * OpenSSL native QUIC handshake) are available; otherwise HAVE_HTTP3 stays 0
 * and src/n_http3.c builds as the not-available stub. */
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <nghttp3/nghttp3.h>

int main(void) {
    ngtcp2_settings settings;
    ngtcp2_settings_default(&settings);
    (void)ngtcp2_crypto_ossl_init;
    (void)nghttp3_version;
    return 0;
}

/*
 * Smoke test for an OpenSSL build, static or DLL: the library matches its headers, libcrypto
 * computes a known digest and an AEAD round trip, libssl completes a TLS 1.3 handshake
 * in-process, and a BIO writes to the application's stdout. Exit code 0 means every check passed.
 */
#include <stdio.h>
#include <string.h>

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/opensslv.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

static int failures;

/* Errors are printed by the application, not handed to OpenSSL as a FILE*: a DLL built with
   the static CRT has a CRT of its own (see bio_on_app_stdout). */
static int print_error(const char *str, size_t len, void *unused)
{
    (void)unused;
    return fwrite(str, 1, len, stdout) == len;
}

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        ERR_print_errors_cb(print_error, NULL);
        failures++;
    }
}

static int version_matches_headers(void)
{
    return OpenSSL_version_num() == OPENSSL_VERSION_NUMBER
        && strcmp(OpenSSL_version(OPENSSL_VERSION_STRING), OPENSSL_VERSION_STR) == 0;
}

static int sha256_vector(void)
{
    /* FIPS 180-2, appendix B.1: SHA-256("abc"). */
    static const unsigned char expected[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;

    return EVP_Digest("abc", 3, md, &len, EVP_sha256(), NULL) == 1
        && len == sizeof expected
        && memcmp(md, expected, sizeof expected) == 0;
}

static int aes_gcm_round_trip(void)
{
    static const unsigned char msg[] = "static OpenSSL smoke test";
    unsigned char key[32], iv[12], tag[16], ct[sizeof msg], pt[sizeof msg];
    int len, ct_len, pt_len, ok = 0;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();

    if (ctx == NULL || RAND_bytes(key, sizeof key) != 1 || RAND_bytes(iv, sizeof iv) != 1)
        goto done;

    if (EVP_EncryptInit_ex2(ctx, EVP_aes_256_gcm(), key, iv, NULL) != 1
        || EVP_EncryptUpdate(ctx, ct, &len, msg, sizeof msg) != 1)
        goto done;
    ct_len = len;
    if (EVP_EncryptFinal_ex(ctx, ct + ct_len, &len) != 1
        || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, sizeof tag, tag) != 1)
        goto done;
    ct_len += len;

    if (EVP_DecryptInit_ex2(ctx, EVP_aes_256_gcm(), key, iv, NULL) != 1
        || EVP_DecryptUpdate(ctx, pt, &len, ct, ct_len) != 1)
        goto done;
    pt_len = len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, sizeof tag, tag) != 1
        || EVP_DecryptFinal_ex(ctx, pt + pt_len, &len) != 1)
        goto done;
    pt_len += len;

    ok = pt_len == (int)sizeof msg && memcmp(pt, msg, sizeof msg) == 0;
done:
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

static X509 *self_signed(EVP_PKEY *key)
{
    X509 *cert = X509_new();
    X509_NAME *name;

    if (cert == NULL)
        return NULL;
    name = X509_get_subject_name(cert);
    if (X509_set_version(cert, X509_VERSION_3) != 1
        || ASN1_INTEGER_set(X509_get_serialNumber(cert), 1) != 1
        || X509_gmtime_adj(X509_getm_notBefore(cert), 0) == NULL
        || X509_gmtime_adj(X509_getm_notAfter(cert), 3600) == NULL
        || X509_set_pubkey(cert, key) != 1
        || X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                      (const unsigned char *)"localhost", -1, -1, 0) != 1
        || X509_set_issuer_name(cert, name) != 1
        || X509_sign(cert, key, EVP_sha256()) == 0) {
        X509_free(cert);
        return NULL;
    }
    return cert;
}

static int want_retry(SSL *ssl, int ret)
{
    int err = SSL_get_error(ssl, ret);

    return err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE;
}

/* Client and server in this process, joined by a BIO pair; the client verifies the server's
   freshly signed certificate and hostname, then sends one application record. */
static int tls13_handshake(void)
{
    EVP_PKEY *key = EVP_PKEY_Q_keygen(NULL, NULL, "EC", "P-256");
    X509 *cert = key != NULL ? self_signed(key) : NULL;
    SSL_CTX *sctx = SSL_CTX_new(TLS_server_method());
    SSL_CTX *cctx = SSL_CTX_new(TLS_client_method());
    SSL *server = NULL, *client = NULL;
    BIO *sbio = NULL, *cbio = NULL;
    int i, server_done = 0, client_done = 0, ok = 0;
    char buf[16];

    if (cert == NULL || sctx == NULL || cctx == NULL
        || SSL_CTX_use_certificate(sctx, cert) != 1
        || SSL_CTX_use_PrivateKey(sctx, key) != 1
        || SSL_CTX_set_min_proto_version(sctx, TLS1_3_VERSION) != 1
        || SSL_CTX_set_min_proto_version(cctx, TLS1_3_VERSION) != 1
        || X509_STORE_add_cert(SSL_CTX_get_cert_store(cctx), cert) != 1)
        goto done;
    SSL_CTX_set_verify(cctx, SSL_VERIFY_PEER, NULL);

    server = SSL_new(sctx);
    client = SSL_new(cctx);
    if (server == NULL || client == NULL
        || SSL_set1_host(client, "localhost") != 1
        || BIO_new_bio_pair(&sbio, 0, &cbio, 0) != 1)
        goto done;
    SSL_set_bio(server, sbio, sbio);
    SSL_set_bio(client, cbio, cbio);
    sbio = cbio = NULL; /* now owned by the SSL objects */
    SSL_set_accept_state(server);
    SSL_set_connect_state(client);

    for (i = 0; i < 100 && !(client_done && server_done); i++) {
        int ret;

        if (!client_done) {
            ret = SSL_do_handshake(client);
            if (ret == 1)
                client_done = 1;
            else if (!want_retry(client, ret))
                goto done;
        }
        if (!server_done) {
            ret = SSL_do_handshake(server);
            if (ret == 1)
                server_done = 1;
            else if (!want_retry(server, ret))
                goto done;
        }
    }

    ok = client_done && server_done
        && SSL_version(client) == TLS1_3_VERSION
        && SSL_get_verify_result(client) == X509_V_OK
        && SSL_write(client, "ping", 4) == 4
        && SSL_read(server, buf, sizeof buf) == 4
        && memcmp(buf, "ping", 4) == 0;
done:
    BIO_free(sbio);
    BIO_free(cbio);
    SSL_free(client);
    SSL_free(server);
    SSL_CTX_free(cctx);
    SSL_CTX_free(sctx);
    X509_free(cert);
    EVP_PKEY_free(key);
    return ok;
}

/* With DLLs built with the static CRT, the application's FILE* reaches OpenSSL only through
   applink.c compiled into the application; without it OpenSSL aborts the process. */
static int bio_on_app_stdout(void)
{
    BIO *bio = BIO_new_fp(stdout, BIO_NOCLOSE);
    int ok = bio != NULL
        && BIO_printf(bio, "BIO writes to the application's stdout\n") > 0
        && BIO_flush(bio) == 1;

    BIO_free(bio);
    return ok;
}

int main(void)
{
    printf("library: %s\n", OpenSSL_version(OPENSSL_VERSION));
    printf("headers: %s\n", OPENSSL_VERSION_TEXT);
    printf("%s\n", OpenSSL_version(OPENSSL_PLATFORM));

    check(version_matches_headers(), "library version matches headers");
    check(sha256_vector(), "SHA-256 test vector");
    check(aes_gcm_round_trip(), "AES-256-GCM round trip");
    check(tls13_handshake(), "TLS 1.3 handshake over a BIO pair");
    fflush(stdout);
    check(bio_on_app_stdout(), "BIO on the application's stdout");

    if (failures != 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}

/*
 * Functional tests for an OpenSSL build, static or DLL: concurrent use from several threads,
 * known-answer tests against published vectors, asymmetric round trips with negative cases,
 * and the default and legacy providers. Exit code 0 means every check passed.
 *
 * Every vector is copied from the source named next to it; none is produced by OpenSSL itself.
 */
#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# include <process.h>
#else
# define _POSIX_C_SOURCE 200809L /* readlink, for a local run against a Linux OpenSSL */
# include <pthread.h>
# include <unistd.h>
#endif

#include <stdio.h>
#include <string.h>

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/provider.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>

static int failures;

/* Errors are printed by the application, not handed to OpenSSL as a FILE* (see smoke.c). */
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
    ERR_clear_error();
}

static void section(const char *name)
{
    printf("\n[%s]\n", name);
}

/* Hex text as the sources print it; spaces and colons are skipped. Returns the byte count, or
   0 when the text is malformed or longer than max. */
static size_t unhex(const char *hex, unsigned char *out, size_t max)
{
    size_t n = 0;
    int high = -1;

    for (; *hex != '\0'; hex++) {
        int v;

        if (*hex == ' ' || *hex == ':')
            continue;
        if (*hex >= '0' && *hex <= '9')
            v = *hex - '0';
        else if (*hex >= 'a' && *hex <= 'f')
            v = *hex - 'a' + 10;
        else
            return 0;
        if (high < 0) {
            high = v;
        } else {
            if (n == max)
                return 0;
            out[n++] = (unsigned char)(high << 4 | v);
            high = -1;
        }
    }
    return high < 0 ? n : 0;
}

static int equals_hex(const unsigned char *buf, size_t len, const char *hex)
{
    unsigned char expected[512];
    size_t n = unhex(hex, expected, sizeof expected);

    return n != 0 && n == len && memcmp(buf, expected, len) == 0;
}

/* ---- Threads ---------------------------------------------------------------------------- */

#define THREADS 8
#define THREAD_ITERATIONS 25

static const char sha256_abc[] =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"; /* FIPS 180-2 B.1 */

struct worker {
    int ok;
};

static int sha256_abc_matches(void)
{
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;

    return EVP_Digest("abc", 3, md, &len, EVP_sha256(), NULL) == 1
        && equals_hex(md, len, sha256_abc);
}

static int ecdsa_round_trip(EVP_PKEY *key, const unsigned char *msg, size_t msg_len)
{
    unsigned char sig[256];
    size_t sig_len = sizeof sig;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    int ok = ctx != NULL
        && EVP_DigestSignInit_ex(ctx, NULL, "SHA256", NULL, NULL, key, NULL) == 1
        && EVP_DigestSign(ctx, sig, &sig_len, msg, msg_len) == 1
        && EVP_DigestVerifyInit_ex(ctx, NULL, "SHA256", NULL, NULL, key, NULL) == 1
        && EVP_DigestVerify(ctx, sig, sig_len, msg, msg_len) == 1;

    EVP_MD_CTX_free(ctx);
    return ok;
}

static void work(struct worker *w)
{
    EVP_PKEY *key = EVP_PKEY_Q_keygen(NULL, NULL, "EC", "P-256");
    unsigned char random[32];
    int i;

    w->ok = key != NULL;
    for (i = 0; w->ok && i < THREAD_ITERATIONS; i++)
        w->ok = sha256_abc_matches()
            && RAND_bytes(random, sizeof random) == 1
            && ecdsa_round_trip(key, random, sizeof random);
    EVP_PKEY_free(key);
}

#ifdef _WIN32
static unsigned __stdcall worker_main(void *arg)
{
    work(arg);
    return 0;
}
#else
static void *worker_main(void *arg)
{
    work(arg);
    return NULL;
}
#endif

/* Runs before anything else touches OpenSSL, so the threads also race through its lazy
   initialisation. */
static int concurrent_use(void)
{
    struct worker workers[THREADS];
    int i, started = 0, ok = 1;
#ifdef _WIN32
    HANDLE threads[THREADS];

    for (i = 0; i < THREADS; i++, started++) {
        threads[i] = (HANDLE)_beginthreadex(NULL, 0, worker_main, &workers[i], 0, NULL);
        if (threads[i] == 0)
            break;
    }
    for (i = 0; i < started; i++) {
        WaitForSingleObject(threads[i], INFINITE);
        CloseHandle(threads[i]);
    }
#else
    pthread_t threads[THREADS];

    for (i = 0; i < THREADS; i++, started++)
        if (pthread_create(&threads[i], NULL, worker_main, &workers[i]) != 0)
            break;
    for (i = 0; i < started; i++)
        pthread_join(threads[i], NULL);
#endif
    if (started != THREADS)
        return 0;
    for (i = 0; i < THREADS; i++)
        ok = ok && workers[i].ok;
    return ok;
}

/* ---- Known-answer tests ------------------------------------------------------------------ */

static int digest_matches(const char *name, const char *msg, const char *expected)
{
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_MD *md_alg = EVP_MD_fetch(NULL, name, NULL);
    int ok = md_alg != NULL
        && EVP_Digest(msg, strlen(msg), md, &len, md_alg, NULL) == 1
        && equals_hex(md, len, expected);

    EVP_MD_free(md_alg);
    return ok;
}

static void digests(void)
{
    section("digests: FIPS 180-2 and FIPS 202, message \"abc\"");
    check(digest_matches("SHA1", "abc", "a9993e364706816aba3e25717850c26c9cd0d89d"), "SHA-1");
    check(digest_matches("SHA256", "abc", sha256_abc), "SHA-256");
    check(digest_matches("SHA512", "abc",
                         "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                         "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"),
          "SHA-512");
    check(digest_matches("SHA3-256", "abc",
                         "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532"),
          "SHA3-256");
}

static int hmac_matches(const char *digest, const char *expected)
{
    /* RFC 4231, test case 2 */
    static const char key[] = "Jefe";
    static const char data[] = "what do ya want for nothing?";
    unsigned char mac[EVP_MAX_MD_SIZE];
    size_t len = 0;

    return EVP_Q_mac(NULL, "HMAC", NULL, digest, NULL, key, strlen(key),
                     (const unsigned char *)data, strlen(data), mac, sizeof mac, &len) != NULL
        && equals_hex(mac, len, expected);
}

static void macs(void)
{
    section("HMAC: RFC 4231, test case 2");
    check(hmac_matches("SHA256", "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"),
          "HMAC-SHA-256");
    check(hmac_matches("SHA512",
                       "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea250554"
                       "9758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737"),
          "HMAC-SHA-512");
}

static int aes_cbc_vector(void)
{
    /* RFC 3602, case #2: AES-128-CBC, two blocks, no padding */
    unsigned char key[16], iv[16], pt[32], out[64];
    int len, total, ok = 0;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();

    if (ctx == NULL
        || unhex("c286696d887c9aa0611bbb3e2025a45a", key, sizeof key) != sizeof key
        || unhex("562e17996d093d28ddb3ba695a2e6f58", iv, sizeof iv) != sizeof iv
        || unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
                 pt, sizeof pt) != sizeof pt)
        goto done;
    if (EVP_EncryptInit_ex2(ctx, EVP_aes_128_cbc(), key, iv, NULL) != 1
        || EVP_CIPHER_CTX_set_padding(ctx, 0) != 1
        || EVP_EncryptUpdate(ctx, out, &len, pt, sizeof pt) != 1)
        goto done;
    total = len;
    if (EVP_EncryptFinal_ex(ctx, out + total, &len) != 1)
        goto done;
    total += len;
    if (!equals_hex(out, total, "d296cd94c2cccf8a3a863028b5e1dc0a"
                                "7586602d253cfff91b8266bea6d61ab1"))
        goto done;

    if (EVP_DecryptInit_ex2(ctx, EVP_aes_128_cbc(), key, iv, NULL) != 1
        || EVP_CIPHER_CTX_set_padding(ctx, 0) != 1
        || EVP_DecryptUpdate(ctx, out, &len, out, total) != 1)
        goto done;
    total = len;
    if (EVP_DecryptFinal_ex(ctx, out + total, &len) != 1)
        goto done;
    total += len;
    ok = total == (int)sizeof pt && memcmp(out, pt, sizeof pt) == 0;
done:
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

/*
 * One AEAD vector: encrypts and compares ciphertext and tag, decrypts it back, then checks that
 * a flipped tag bit and a flipped ciphertext bit are both rejected.
 */
struct aead_vector {
    const char *cipher, *key, *nonce, *aad, *pt, *ct, *tag;
};

static int aead_open(EVP_CIPHER *cipher, const unsigned char *key, const unsigned char *nonce,
                     size_t nonce_len, const unsigned char *aad, size_t aad_len,
                     const unsigned char *ct, size_t ct_len, unsigned char *tag, size_t tag_len,
                     unsigned char *pt_out)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    OSSL_PARAM params[2];
    int len, ok;
    size_t ivlen = nonce_len;

    params[0] = OSSL_PARAM_construct_size_t(OSSL_CIPHER_PARAM_AEAD_IVLEN, &ivlen);
    params[1] = OSSL_PARAM_construct_end();
    ok = ctx != NULL
        && EVP_DecryptInit_ex2(ctx, cipher, NULL, NULL, params) == 1
        && EVP_DecryptInit_ex2(ctx, NULL, key, nonce, NULL) == 1
        && EVP_DecryptUpdate(ctx, NULL, &len, aad, (int)aad_len) == 1
        && EVP_DecryptUpdate(ctx, pt_out, &len, ct, (int)ct_len) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, (int)tag_len, tag) == 1
        && EVP_DecryptFinal_ex(ctx, pt_out + len, &len) == 1;
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

static int aead_matches(const struct aead_vector *v, int *rejects_tampering)
{
    unsigned char key[32], nonce[16], aad[64], pt[256], ct[256], tag[16], out[256];
    size_t key_len = unhex(v->key, key, sizeof key), nonce_len = unhex(v->nonce, nonce, sizeof nonce);
    size_t aad_len = unhex(v->aad, aad, sizeof aad), pt_len = unhex(v->pt, pt, sizeof pt);
    EVP_CIPHER *cipher = EVP_CIPHER_fetch(NULL, v->cipher, NULL);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    OSSL_PARAM params[2];
    int len, ct_len, ok = 0;

    *rejects_tampering = 0;
    params[0] = OSSL_PARAM_construct_size_t(OSSL_CIPHER_PARAM_AEAD_IVLEN, &nonce_len);
    params[1] = OSSL_PARAM_construct_end();
    if (cipher == NULL || ctx == NULL || key_len == 0 || nonce_len == 0 || aad_len == 0
        || pt_len == 0 || (size_t)EVP_CIPHER_get_key_length(cipher) != key_len)
        goto done;

    if (EVP_EncryptInit_ex2(ctx, cipher, NULL, NULL, params) != 1
        || EVP_EncryptInit_ex2(ctx, NULL, key, nonce, NULL) != 1
        || EVP_EncryptUpdate(ctx, NULL, &len, aad, (int)aad_len) != 1
        || EVP_EncryptUpdate(ctx, ct, &len, pt, (int)pt_len) != 1)
        goto done;
    ct_len = len;
    if (EVP_EncryptFinal_ex(ctx, ct + ct_len, &len) != 1
        || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, sizeof tag, tag) != 1)
        goto done;
    ct_len += len;
    if (!equals_hex(ct, ct_len, v->ct) || !equals_hex(tag, sizeof tag, v->tag))
        goto done;

    if (!aead_open(cipher, key, nonce, nonce_len, aad, aad_len, ct, ct_len, tag, sizeof tag, out)
        || memcmp(out, pt, pt_len) != 0)
        goto done;
    ok = 1;

    tag[0] ^= 1;
    *rejects_tampering = !aead_open(cipher, key, nonce, nonce_len, aad, aad_len, ct, ct_len,
                                    tag, sizeof tag, out);
    tag[0] ^= 1;
    ct[0] ^= 1;
    *rejects_tampering = *rejects_tampering
        && !aead_open(cipher, key, nonce, nonce_len, aad, aad_len, ct, ct_len, tag, sizeof tag,
                      out);
done:
    EVP_CIPHER_CTX_free(ctx);
    EVP_CIPHER_free(cipher);
    return ok;
}

static void ciphers(void)
{
    static const struct aead_vector gcm = {
        /* NIST CAVP gcmEncryptExtIV256.rsp: Keylen 256, IVlen 96, PTlen 128, AADlen 128,
           Taglen 128, Count 0 */
        "AES-256-GCM",
        "92e11dcdaa866f5ce790fd24501f92509aacf4cb8b1339d50c9c1240935dd08b",
        "ac93a1a6145299bde902f21a",
        "1e0889016f67601c8ebea4943bc23ad6",
        "2d71bcfa914e4ac045b2aa60955fad24",
        "8995ae2e6df3dbf96fac7b7137bae67f",
        "eca5aa77d51d4a0a14d9c51e1da474ab",
    };
    static const struct aead_vector chacha = {
        /* RFC 8439, 2.8.2; nonce = constant 07000000 || IV 4041424344454647 */
        "ChaCha20-Poly1305",
        "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f",
        "070000004041424344454647",
        "50515253c0c1c2c3c4c5c6c7",
        "4c616469657320616e642047656e746c656d656e206f662074686520636c6173"
        "73206f66202739393a204966204920636f756c64206f6666657220796f75206f"
        "6e6c79206f6e652074697020666f7220746865206675747572652c2073756e73"
        "637265656e20776f756c642062652069742e",
        "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6"
        "3dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36"
        "92ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc"
        "3ff4def08e4b7a9de576d26586cec64b6116",
        "1ae10b594f09e26a7e902ecbd0600691",
    };
    int rejects;

    section("ciphers");
    check(aes_cbc_vector(), "AES-128-CBC: RFC 3602, case #2");
    check(aead_matches(&gcm, &rejects), "AES-256-GCM: NIST CAVP vector");
    check(rejects, "AES-256-GCM: rejects a flipped tag bit and a flipped ciphertext bit");
    check(aead_matches(&chacha, &rejects), "ChaCha20-Poly1305: RFC 8439, 2.8.2");
    check(rejects, "ChaCha20-Poly1305: rejects a flipped tag bit and a flipped ciphertext bit");
}

static int hkdf_vector(void)
{
    /* RFC 5869, A.1 */
    unsigned char ikm[22], salt[13], info[10], okm[42];
    EVP_KDF *kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
    EVP_KDF_CTX *ctx = kdf != NULL ? EVP_KDF_CTX_new(kdf) : NULL;
    OSSL_PARAM params[5];
    int ok;

    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, "SHA256", 0);
    params[1] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, ikm, sizeof ikm);
    params[2] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, salt, sizeof salt);
    params[3] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, info, sizeof info);
    params[4] = OSSL_PARAM_construct_end();
    ok = ctx != NULL
        && unhex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b", ikm, sizeof ikm) == sizeof ikm
        && unhex("000102030405060708090a0b0c", salt, sizeof salt) == sizeof salt
        && unhex("f0f1f2f3f4f5f6f7f8f9", info, sizeof info) == sizeof info
        && EVP_KDF_derive(ctx, okm, sizeof okm, params) == 1
        && equals_hex(okm, sizeof okm, "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db0"
                                       "2d56ecc4c5bf34007208d5b887185865");
    EVP_KDF_CTX_free(ctx);
    EVP_KDF_free(kdf);
    return ok;
}

static int pbkdf2_vector(void)
{
    /* RFC 7914, section 11: P="passwd", S="salt", c=1, dkLen=64 */
    unsigned char dk[64];

    return PKCS5_PBKDF2_HMAC("passwd", 6, (const unsigned char *)"salt", 4, 1, EVP_sha256(),
                             sizeof dk, dk) == 1
        && equals_hex(dk, sizeof dk,
                      "55 ac 04 6e 56 e3 08 9f ec 16 91 c2 25 44 b6 05"
                      "f9 41 85 21 6d de 04 65 e6 8b 9d 57 c2 0d ac bc"
                      "49 ca 9c cc f1 79 b6 45 99 16 64 b3 9d 77 ef 31"
                      "7c 71 b8 45 b1 e3 0b d5 09 11 20 41 d3 a1 97 83");
}

static void kdfs(void)
{
    section("key derivation");
    check(hkdf_vector(), "HKDF-SHA-256: RFC 5869, A.1");
    check(pbkdf2_vector(), "PBKDF2-HMAC-SHA-256: RFC 7914, section 11");
}

static EVP_PKEY *raw_key(int type, int is_private, const char *hex)
{
    unsigned char raw[64];
    size_t len = unhex(hex, raw, sizeof raw);

    if (len == 0)
        return NULL;
    return is_private ? EVP_PKEY_new_raw_private_key(type, NULL, raw, len)
                   : EVP_PKEY_new_raw_public_key(type, NULL, raw, len);
}

static int public_key_is(EVP_PKEY *key, const char *hex)
{
    unsigned char raw[64];
    size_t len = sizeof raw;

    return key != NULL && EVP_PKEY_get_raw_public_key(key, raw, &len) == 1
        && equals_hex(raw, len, hex);
}

static int derive_matches(EVP_PKEY *own, EVP_PKEY *peer, const char *expected)
{
    unsigned char secret[128];
    size_t len = sizeof secret;
    EVP_PKEY_CTX *ctx = own != NULL && peer != NULL ? EVP_PKEY_CTX_new(own, NULL) : NULL;
    int ok = ctx != NULL
        && EVP_PKEY_derive_init(ctx) == 1
        && EVP_PKEY_derive_set_peer(ctx, peer) == 1
        && EVP_PKEY_derive(ctx, secret, &len) == 1
        && (expected == NULL || equals_hex(secret, len, expected));

    EVP_PKEY_CTX_free(ctx);
    return ok;
}

static int x25519_vector(void)
{
    /* RFC 7748, 6.1 */
    EVP_PKEY *alice = raw_key(EVP_PKEY_X25519, 1,
                              "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    EVP_PKEY *bob_public = raw_key(EVP_PKEY_X25519, 0,
                                   "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    int ok = public_key_is(alice, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a")
        && derive_matches(alice, bob_public,
                          "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");

    EVP_PKEY_free(alice);
    EVP_PKEY_free(bob_public);
    return ok;
}

static int ed25519_vector(int *rejects_other_message)
{
    /* RFC 8032, 7.1, TEST 2 */
    static const unsigned char msg[] = { 0x72 }, other[] = { 0x73 };
    EVP_PKEY *key = raw_key(EVP_PKEY_ED25519, 1,
                            "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb");
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned char sig[64];
    size_t sig_len = sizeof sig;
    int ok;

    ok = ctx != NULL
        && public_key_is(key, "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c")
        && EVP_DigestSignInit_ex(ctx, NULL, NULL, NULL, NULL, key, NULL) == 1
        && EVP_DigestSign(ctx, sig, &sig_len, msg, sizeof msg) == 1
        && equals_hex(sig, sig_len,
                      "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
                      "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00")
        && EVP_DigestVerifyInit_ex(ctx, NULL, NULL, NULL, NULL, key, NULL) == 1
        && EVP_DigestVerify(ctx, sig, sig_len, msg, sizeof msg) == 1;
    *rejects_other_message = ok
        && EVP_DigestVerifyInit_ex(ctx, NULL, NULL, NULL, NULL, key, NULL) == 1
        && EVP_DigestVerify(ctx, sig, sig_len, other, sizeof other) != 1;
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return ok;
}

static void curves(void)
{
    int rejects;

    section("X25519 and Ed25519");
    check(x25519_vector(), "X25519: RFC 7748, 6.1");
    check(ed25519_vector(&rejects), "Ed25519: RFC 8032, TEST 2");
    check(rejects, "Ed25519: rejects the signature for another message");
}

/* ---- Asymmetric round trips and negative cases -------------------------------------------- */

static int sign(EVP_PKEY *key, int rsa_pss, const unsigned char *msg, size_t msg_len,
                unsigned char *sig, size_t *sig_len)
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_PKEY_CTX *pctx = NULL;
    int ok = ctx != NULL
        && EVP_DigestSignInit_ex(ctx, &pctx, "SHA256", NULL, NULL, key, NULL) == 1
        && (!rsa_pss || EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) == 1)
        && EVP_DigestSign(ctx, sig, sig_len, msg, msg_len) == 1;

    EVP_MD_CTX_free(ctx);
    return ok;
}

static int verifies(EVP_PKEY *key, int rsa_pss, const unsigned char *msg, size_t msg_len,
                    const unsigned char *sig, size_t sig_len)
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_PKEY_CTX *pctx = NULL;
    int ok = ctx != NULL
        && EVP_DigestVerifyInit_ex(ctx, &pctx, "SHA256", NULL, NULL, key, NULL) == 1
        && (!rsa_pss || EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) == 1)
        && EVP_DigestVerify(ctx, sig, sig_len, msg, msg_len) == 1;

    EVP_MD_CTX_free(ctx);
    return ok;
}

/* Signs, verifies, then expects refusal for a changed message, a changed signature and, when
   other is given, another key. */
static void signature_checks(const char *name, EVP_PKEY *key, EVP_PKEY *other, int rsa_pss)
{
    static const unsigned char msg[] = "functional test message";
    unsigned char changed[sizeof msg], sig[512];
    size_t sig_len = sizeof sig;
    char what[96];
    int signed_ok = key != NULL && sign(key, rsa_pss, msg, sizeof msg, sig, &sig_len);

    snprintf(what, sizeof what, "%s: sign and verify", name);
    check(signed_ok && verifies(key, rsa_pss, msg, sizeof msg, sig, sig_len), what);
    if (!signed_ok)
        return;

    memcpy(changed, msg, sizeof msg);
    changed[0] ^= 1;
    snprintf(what, sizeof what, "%s: rejects a changed message", name);
    check(!verifies(key, rsa_pss, changed, sizeof changed, sig, sig_len), what);

    sig[sig_len / 2] ^= 1;
    snprintf(what, sizeof what, "%s: rejects a changed signature", name);
    check(!verifies(key, rsa_pss, msg, sizeof msg, sig, sig_len), what);
    sig[sig_len / 2] ^= 1;

    if (other != NULL) {
        snprintf(what, sizeof what, "%s: rejects another key", name);
        check(!verifies(other, rsa_pss, msg, sizeof msg, sig, sig_len), what);
    }
}

static int rsa_oaep(EVP_PKEY *key, int encrypt, const unsigned char *in, size_t in_len,
                    unsigned char *out, size_t *out_len)
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(key, NULL);
    int ok = ctx != NULL
        && (encrypt ? EVP_PKEY_encrypt_init(ctx) : EVP_PKEY_decrypt_init(ctx)) == 1
        && EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) == 1
        && EVP_PKEY_CTX_set_rsa_oaep_md_name(ctx, "SHA256", NULL) == 1
        && (encrypt ? EVP_PKEY_encrypt(ctx, out, out_len, in, in_len)
                    : EVP_PKEY_decrypt(ctx, out, out_len, in, in_len)) == 1;

    EVP_PKEY_CTX_free(ctx);
    return ok;
}

static void rsa(void)
{
    static const unsigned char secret[] = "a secret for RSA-OAEP";
    unsigned char ct[512], pt[512];
    size_t ct_len = sizeof ct, pt_len = sizeof pt;
    EVP_PKEY *key = EVP_PKEY_Q_keygen(NULL, NULL, "RSA", (size_t)2048);
    int encrypted;

    section("RSA-2048");
    check(key != NULL, "RSA: generate a 2048-bit key");
    if (key == NULL)
        return;
    signature_checks("RSA-PSS SHA-256", key, NULL, 1);

    encrypted = rsa_oaep(key, 1, secret, sizeof secret, ct, &ct_len);
    check(encrypted && rsa_oaep(key, 0, ct, ct_len, pt, &pt_len)
              && pt_len == sizeof secret && memcmp(pt, secret, pt_len) == 0,
          "RSA-OAEP SHA-256: encrypt and decrypt");
    if (encrypted) {
        ct[ct_len / 2] ^= 1;
        pt_len = sizeof pt;
        check(!rsa_oaep(key, 0, ct, ct_len, pt, &pt_len), "RSA-OAEP: rejects a changed ciphertext");
    }
    EVP_PKEY_free(key);
}

static void ec(void)
{
    EVP_PKEY *a = EVP_PKEY_Q_keygen(NULL, NULL, "EC", "P-256");
    EVP_PKEY *b = EVP_PKEY_Q_keygen(NULL, NULL, "EC", "P-256");
    unsigned char ab[64], ba[64];
    size_t ab_len = sizeof ab, ba_len = sizeof ba;
    EVP_PKEY_CTX *ctx;
    int ok;

    section("ECDSA and ECDH P-256");
    check(a != NULL && b != NULL, "EC: generate two P-256 keys");
    if (a == NULL || b == NULL)
        goto done;
    signature_checks("ECDSA P-256 SHA-256", a, b, 0);

    ctx = EVP_PKEY_CTX_new(a, NULL);
    ok = ctx != NULL && EVP_PKEY_derive_init(ctx) == 1 && EVP_PKEY_derive_set_peer(ctx, b) == 1
        && EVP_PKEY_derive(ctx, ab, &ab_len) == 1;
    EVP_PKEY_CTX_free(ctx);
    ctx = EVP_PKEY_CTX_new(b, NULL);
    ok = ok && ctx != NULL && EVP_PKEY_derive_init(ctx) == 1
        && EVP_PKEY_derive_set_peer(ctx, a) == 1 && EVP_PKEY_derive(ctx, ba, &ba_len) == 1
        && ab_len == ba_len && memcmp(ab, ba, ab_len) == 0;
    EVP_PKEY_CTX_free(ctx);
    check(ok, "ECDH P-256: both sides derive the same secret");
done:
    EVP_PKEY_free(a);
    EVP_PKEY_free(b);
}

/* ---- Providers ------------------------------------------------------------------------- */

static int executable_dir(char *dir, size_t size)
{
    char *slash;
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(NULL, dir, (DWORD)size);

    if (len == 0 || len >= size)
        return 0;
    slash = strrchr(dir, '\\');
#else
    ssize_t len = readlink("/proc/self/exe", dir, size - 1);

    if (len <= 0)
        return 0;
    dir[len] = '\0';
    slash = strrchr(dir, '/');
#endif
    if (slash == NULL)
        return 0;
    *slash = '\0';
    return 1;
}

#ifdef _WIN32
/* Prints the file a loaded module came from and checks that it sits in exe_dir. */
static void module_location(HMODULE module, const char *what, const char *exe_dir)
{
    char path[MAX_PATH + 1], label[96];
    char *slash = NULL;
    DWORD len = GetModuleFileNameA(module, path, sizeof path);

    if (len != 0 && len < sizeof path) {
        printf("     %s: %s\n", what, path);
        slash = strrchr(path, '\\');
        if (slash != NULL)
            *slash = '\0';
    }
    snprintf(label, sizeof label, "%s comes from the executable's directory", what);
    check(slash != NULL && _stricmp(path, exe_dir) == 0, label);
}

/* The module holding libcrypto's code: the executable in the static build, our DLL next to it
   in the DLL build. The version string lives in libcrypto's data, unlike a function address,
   which for an imported function would point at a stub in the executable. */
static void libcrypto_location(const char *exe_dir)
{
    HMODULE module = NULL;

    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            OpenSSL_version(OPENSSL_VERSION), &module)) {
        check(0, "libcrypto: module of the running code");
        return;
    }
    module_location(module, "libcrypto", exe_dir);
}

/* In the static build no legacy.dll is loaded: the provider is part of libcrypto. */
static void legacy_module_location(const char *exe_dir)
{
    HMODULE module = GetModuleHandleA("legacy.dll");

    if (module == NULL)
        printf("     legacy provider: built into libcrypto\n");
    else
        module_location(module, "legacy.dll", exe_dir);
}

/* The compiled-in module directory is C:\Program Files\OpenSSL\bin, where a machine may hold
   another OpenSSL's modules; GitHub's Windows images do. */
static void foreign_module_at_compiled_in_dir(void)
{
    const char *info = OpenSSL_version(OPENSSL_MODULES_DIR); /* MODULESDIR: "<dir>" */
    const char *first = strchr(info, '"'), *last = first != NULL ? strchr(first + 1, '"') : NULL;
    char path[MAX_PATH + 1];

    printf("     compiled-in %s\n", info);
    if (last != NULL && (size_t)(last - first) + sizeof "\\legacy.dll" < sizeof path) {
        snprintf(path, sizeof path, "%.*s\\legacy.dll", (int)(last - first - 1), first + 1);
        printf("     %s: %s\n", path,
               GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES ? "absent" : "present");
    }
}
#endif

static void providers(void)
{
    OSSL_PROVIDER *def, *legacy;
    char dir[1024];
    int have_dir = executable_dir(dir, sizeof dir);

    section("providers");
    /* Loading any provider explicitly stops the default one from loading on its own. */
    def = OSSL_PROVIDER_load(NULL, "default");
    check(def != NULL, "default provider loads");

    /* An application of the DLL build ships legacy.dll next to itself and points OpenSSL there;
       the compiled-in directory is not ours (see foreign_module_at_compiled_in_dir). The static
       build has the provider inside libcrypto and finds it regardless of the path. */
#ifdef _WIN32
    if (have_dir)
        libcrypto_location(dir);
    foreign_module_at_compiled_in_dir();
#endif
    check(have_dir && OSSL_PROVIDER_set_default_search_path(NULL, dir) == 1,
          "module search path set to the executable's directory");
    legacy = OSSL_PROVIDER_load(NULL, "legacy");
    check(legacy != NULL, "legacy provider loads");
#ifdef _WIN32
    if (legacy != NULL && have_dir)
        legacy_module_location(dir);
#endif
    check(legacy != NULL && digest_matches("MD4", "abc", "a448017aaf21d8525fc10ae87aa6729d"),
          "MD4 from the legacy provider: RFC 1320");
    check(digest_matches("SHA256", "abc", sha256_abc), "default provider still serves SHA-256");

    if (legacy != NULL)
        OSSL_PROVIDER_unload(legacy);
    if (def != NULL)
        OSSL_PROVIDER_unload(def);
}

int main(void)
{
    section("threads");
    check(concurrent_use(), "8 threads from first use: SHA-256, RAND, ECDSA P-256");

    digests();
    macs();
    ciphers();
    kdfs();
    curves();
    rsa();
    ec();
    providers();

    if (failures != 0) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "eval.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <limits.h>
#include <regex.h>
#include <zlib.h>
#include <dlfcn.h>
#include <ffi.h>

/* === PROTOTYPES === */
/* Forward declarations de TUDO que precisa. Ordem nao importa. */

#include "eval.h"
#include "ast.h"
#include "env.h"
#include "value.h"
#include "parser.h"


/* ============================================================
 * ÍNDICE DO ARQUIVO eval.c
 * ============================================================
 *
 * Este arquivo contém, em ordem:
 *
 *   1. Includes e prototypes
 *   2. Definicoes internas (JscThread, GC, runtime_error)
 *   3. Crypto — MD5, SHA-1, SHA-256, SHA-512, HMAC, base64, hex, xor, random, uuid
 *   4. JSON + TIME (Bloco B)
 *   5. NET — sockets, HTTP, DNS, scan
 *   6. FS — arquivos
 *   7. MATH — constantes e funcoes
 *   8. STRING — manipulacao
 *   9. REGEX, ZIP, CSV
 *  10. HACK — reconnaissance
 *  11. THREADS — spawn, join, sincronizacao
 *  12. Evaluator core — eval, eval_binary, eval_call, etc.
 *  13. TRY/CATCH
 *  14. Modulos — make_*_module e eval_import
 *  15. Natives — input, spawn, natives_init
 *
 * TODO: refatorar em arquivos separados no futuro.
 * ============================================================ */


/* runtime_error */
static void runtime_error(EvalState *state, const char *msg, Node *node);

/* Evaluator */
JscValue *eval(Node *node, Env *env, EvalState *state);
JscValue *eval_literal(Node *node, Env *env, EvalState *state);
JscValue *eval_ident(Node *node, Env *env, EvalState *state);
JscValue *eval_binary(Node *node, Env *env, EvalState *state);
JscValue *eval_unary(Node *node, Env *env, EvalState *state);
JscValue *eval_ternary(Node *node, Env *env, EvalState *state);
JscValue *eval_call(Node *node, Env *env, EvalState *state);
JscValue *eval_method_call(NodeCall *n, Env *env, EvalState *state);
JscValue *eval_program(Node *node, Env *env, EvalState *state);
JscValue *eval_block(Node *node, Env *env, EvalState *state);
JscValue *eval_printj(Node *node, Env *env, EvalState *state);
JscValue *eval_var_decl(Node *node, Env *env, EvalState *state);
JscValue *eval_if(Node *node, Env *env, EvalState *state);
JscValue *eval_while(Node *node, Env *env, EvalState *state);
JscValue *eval_for(Node *node, Env *env, EvalState *state);
JscValue *eval_for_each(Node *node, Env *env, EvalState *state);
JscValue *eval_func_decl(Node *node, Env *env, EvalState *state);
JscValue *eval_return(Node *node, Env *env, EvalState *state);
JscValue *eval_class_decl(Node *node, Env *env, EvalState *state);
JscValue *eval_try(Node *node, Env *env, EvalState *state);
JscValue *eval_array(Node *node, Env *env, EvalState *state);
JscValue *eval_map(Node *node, Env *env, EvalState *state);
JscValue *eval_index(Node *node, Env *env, EvalState *state);
JscValue *eval_field(Node *node, Env *env, EvalState *state);
JscValue *eval_assign(Node *node, Env *env, EvalState *state);
JscValue *eval_import(Node *node, Env *env, EvalState *state);

/* Natives */
JscValue *native_import_c(void *call, void *env, void *state);
JscValue *native_call_c(void *call, void *env, void *state);

JscValue *native_input(void *call, void *env, void *state);
JscValue *native_spawn(void *call, void *env, void *state);
static JscValue *make_py_module(void);
void natives_init(Env *global);

/* Threads */
void gc_mark_thread(void *thread_ptr);

/* Net helper */
static int net_connect_timeout(const char *host, int porta, int timeout_ms);

/* FS helpers */
JscValue *fs_read(void *call, void *env, void *state);
JscValue *fs_write(void *call, void *env, void *state);
JscValue *fs_append(void *call, void *env, void *state);
JscValue *fs_exists(void *call, void *env, void *state);
JscValue *fs_is_file(void *call, void *env, void *state);
JscValue *fs_is_dir(void *call, void *env, void *state);
JscValue *fs_size(void *call, void *env, void *state);
JscValue *fs_remove(void *call, void *env, void *state);
JscValue *fs_mkdir(void *call, void *env, void *state);
JscValue *fs_list(void *call, void *env, void *state);
JscValue *fs_copy(void *call, void *env, void *state);
JscValue *fs_lines(void *call, void *env, void *state);

/* Crypto hashes declarados no proprio bloco */

/* Net extras */
JscValue *net_dns_lookup(void *call, void *env, void *state);
JscValue *net_ip_publico(void *call, void *env, void *state);
JscValue *net_traceroute(void *call, void *env, void *state);

/* Hack */
JscValue *hack_banner(void *call, void *env, void *state);
JscValue *hack_headers(void *call, void *env, void *state);
JscValue *hack_paths(void *call, void *env, void *state);
JscValue *hack_subdomain(void *call, void *env, void *state);
JscValue *hack_cdn_check(void *call, void *env, void *state);

/* Crypto extras */
JscValue *crypto_uuid(void *call, void *env, void *state);
JscValue *crypto_crack_md5(void *call, void *env, void *state);
JscValue *crypto_crack_sha1(void *call, void *env, void *state);
JscValue *crypto_crack_sha256(void *call, void *env, void *state);

/* Time */
JscValue *time_now(void *call, void *env, void *state);
JscValue *time_sleep(void *call, void *env, void *state);
JscValue *time_clock(void *call, void *env, void *state);

/* OS */
JscValue *os_env(void *call, void *env, void *state);
JscValue *os_getcwd(void *call, void *env, void *state);
JscValue *os_pid(void *call, void *env, void *state);

/* String */
JscValue *string_pad_left(void *call, void *env, void *state);
JscValue *string_starts_with(void *call, void *env, void *state);
JscValue *string_ends_with(void *call, void *env, void *state);
JscValue *string_reverse(void *call, void *env, void *state);

/* Regex */
JscValue *regex_match(void *call, void *env, void *state);
JscValue *regex_replace(void *call, void *env, void *state);
JscValue *regex_find_all(void *call, void *env, void *state);

/* Zip */
JscValue *zip_compress(void *call, void *env, void *state);
JscValue *zip_decompress(void *call, void *env, void *state);

/* CSV */
JscValue *csv_parse(void *call, void *env, void *state);

/* Math extras */
JscValue *math_gcd(void *call, void *env, void *state);
JscValue *math_factorial(void *call, void *env, void *state);
JscValue *math_is_prime(void *call, void *env, void *state);

/* Modulos (static) */
static JscValue *make_math_module(void);
static JscValue *make_fs_module(void);
static JscValue *make_crypto_module(void);
static JscValue *make_net_module(void);
static JscValue *make_time_module(void);
static JscValue *make_os_module(void);
static JscValue *make_string_module(void);
static JscValue *make_regex_module(void);
static JscValue *make_zip_module(void);
static JscValue *make_csv_module(void);
static JscValue *make_hack_module(void);

/* === FIM PROTOTYPES === */

/* Flag global definida em value.c */
extern volatile int gc_in_thread;

/* Contador global de threads ativas */
volatile int gc_active_threads = 0;

typedef struct JscThread {
    pthread_t      handle;
    int            id;
    int            done;
    int            had_error;
    char           error_msg[256];
    JscValue      *result;
    NodeFuncDecl  *body;
    JscValue     **args;
    size_t         n_args;
    Env           *closure;
} JscThread;

/* ============================================================
 * MD5 — implementação padrão RFC 1321
 * ============================================================ */

typedef struct {
    uint32_t state[4];
    uint64_t count;
    unsigned char buffer[64];
} MD5_CTX;

static void md5_transform(uint32_t state[4], const unsigned char block[64]) {
    uint32_t a = state[0], b = state[1], cc = state[2], d = state[3];
    uint32_t M[16];
    for (int i = 0; i < 16; i++) {
        M[i] = (uint32_t)block[i*4] |
               ((uint32_t)block[i*4+1] << 8) |
               ((uint32_t)block[i*4+2] << 16) |
               ((uint32_t)block[i*4+3] << 24);
    }

    #define F(x,y,z) (((x) & (y)) | (~(x) & (z)))
    #define G(x,y,z) (((x) & (z)) | ((y) & ~(z)))
    #define H(x,y,z) ((x) ^ (y) ^ (z))
    #define I(x,y,z) ((y) ^ ((x) | ~(z)))
    #define ROTL(x,n) (((x) << (n)) | ((x) >> (32 - (n))))
    #define STEP(f, a, b, c, d, x, t, s) \
        (a) += f((b), (c), (d)) + (x) + (t); \
        (a) = ROTL((a), (s)); \
        (a) += (b);

    /* Round 1 */
    STEP(F, a, b, cc, d, M[0],  0xd76aa478, 7)
    STEP(F, d, a, b, cc, M[1],  0xe8c7b756, 12)
    STEP(F, cc, d, a, b, M[2],  0x242070db, 17)
    STEP(F, b, cc, d, a, M[3],  0xc1bdceee, 22)
    STEP(F, a, b, cc, d, M[4],  0xf57c0faf, 7)
    STEP(F, d, a, b, cc, M[5],  0x4787c62a, 12)
    STEP(F, cc, d, a, b, M[6],  0xa8304613, 17)
    STEP(F, b, cc, d, a, M[7],  0xfd469501, 22)
    STEP(F, a, b, cc, d, M[8],  0x698098d8, 7)
    STEP(F, d, a, b, cc, M[9],  0x8b44f7af, 12)
    STEP(F, cc, d, a, b, M[10], 0xffff5bb1, 17)
    STEP(F, b, cc, d, a, M[11], 0x895cd7be, 22)
    STEP(F, a, b, cc, d, M[12], 0x6b901122, 7)
    STEP(F, d, a, b, cc, M[13], 0xfd987193, 12)
    STEP(F, cc, d, a, b, M[14], 0xa679438e, 17)
    STEP(F, b, cc, d, a, M[15], 0x49b40821, 22)

    /* Round 2 */
    STEP(G, a, b, cc, d, M[1],  0xf61e2562, 5)
    STEP(G, d, a, b, cc, M[6],  0xc040b340, 9)
    STEP(G, cc, d, a, b, M[11], 0x265e5a51, 14)
    STEP(G, b, cc, d, a, M[0],  0xe9b6c7aa, 20)
    STEP(G, a, b, cc, d, M[5],  0xd62f105d, 5)
    STEP(G, d, a, b, cc, M[10], 0x02441453, 9)
    STEP(G, cc, d, a, b, M[15], 0xd8a1e681, 14)
    STEP(G, b, cc, d, a, M[4],  0xe7d3fbc8, 20)
    STEP(G, a, b, cc, d, M[9],  0x21e1cde6, 5)
    STEP(G, d, a, b, cc, M[14], 0xc33707d6, 9)
    STEP(G, cc, d, a, b, M[3],  0xf4d50d87, 14)
    STEP(G, b, cc, d, a, M[8],  0x455a14ed, 20)
    STEP(G, a, b, cc, d, M[13], 0xa9e3e905, 5)
    STEP(G, d, a, b, cc, M[2],  0xfcefa3f8, 9)
    STEP(G, cc, d, a, b, M[7],  0x676f02d9, 14)
    STEP(G, b, cc, d, a, M[12], 0x8d2a4c8a, 20)

    /* Round 3 */
    STEP(H, a, b, cc, d, M[5],  0xfffa3942, 4)
    STEP(H, d, a, b, cc, M[8],  0x8771f681, 11)
    STEP(H, cc, d, a, b, M[11], 0x6d9d6122, 16)
    STEP(H, b, cc, d, a, M[14], 0xfde5380c, 23)
    STEP(H, a, b, cc, d, M[1],  0xa4beea44, 4)
    STEP(H, d, a, b, cc, M[4],  0x4bdecfa9, 11)
    STEP(H, cc, d, a, b, M[7],  0xf6bb4b60, 16)
    STEP(H, b, cc, d, a, M[10], 0xbebfbc70, 23)
    STEP(H, a, b, cc, d, M[13], 0x289b7ec6, 4)
    STEP(H, d, a, b, cc, M[0],  0xeaa127fa, 11)
    STEP(H, cc, d, a, b, M[3],  0xd4ef3085, 16)
    STEP(H, b, cc, d, a, M[6],  0x04881d05, 23)
    STEP(H, a, b, cc, d, M[9],  0xd9d4d039, 4)
    STEP(H, d, a, b, cc, M[12], 0xe6db99e5, 11)
    STEP(H, cc, d, a, b, M[15], 0x1fa27cf8, 16)
    STEP(H, b, cc, d, a, M[2],  0xc4ac5665, 23)

    /* Round 4 */
    STEP(I, a, b, cc, d, M[0],  0xf4292244, 6)
    STEP(I, d, a, b, cc, M[7],  0x432aff97, 10)
    STEP(I, cc, d, a, b, M[14], 0xab9423a7, 15)
    STEP(I, b, cc, d, a, M[5],  0xfc93a039, 21)
    STEP(I, a, b, cc, d, M[12], 0x655b59c3, 6)
    STEP(I, d, a, b, cc, M[3],  0x8f0ccc92, 10)
    STEP(I, cc, d, a, b, M[10], 0xffeff47d, 15)
    STEP(I, b, cc, d, a, M[1],  0x85845dd1, 21)
    STEP(I, a, b, cc, d, M[8],  0x6fa87e4f, 6)
    STEP(I, d, a, b, cc, M[15], 0xfe2ce6e0, 10)
    STEP(I, cc, d, a, b, M[6],  0xa3014314, 15)
    STEP(I, b, cc, d, a, M[13], 0x4e0811a1, 21)
    STEP(I, a, b, cc, d, M[4],  0xf7537e82, 6)
    STEP(I, d, a, b, cc, M[11], 0xbd3af235, 10)
    STEP(I, cc, d, a, b, M[2],  0x2ad7d2bb, 15)
    STEP(I, b, cc, d, a, M[9],  0xeb86d391, 21)

    state[0] += a;
    state[1] += b;
    state[2] += cc;
    state[3] += d;
}

static void md5_init(MD5_CTX *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->count = 0;
}

static void md5_update(MD5_CTX *ctx, const unsigned char *data, size_t len) {
    size_t i, index, part_len;
    index = (size_t)((ctx->count >> 3) & 0x3F);
    ctx->count += (uint64_t)len << 3;
    part_len = 64 - index;

    if (len >= part_len) {
        memcpy(&ctx->buffer[index], data, part_len);
        md5_transform(ctx->state, ctx->buffer);
        for (i = part_len; i + 63 < len; i += 64) {
            md5_transform(ctx->state, &data[i]);
        }
        index = 0;
    } else {
        i = 0;
    }
    memcpy(&ctx->buffer[index], &data[i], len - i);
}

static void md5_final(unsigned char digest[16], MD5_CTX *ctx) {
    unsigned char bits[8];
    size_t index, pad_len;
    uint64_t count_bits = ctx->count;

    for (int i = 0; i < 8; i++) bits[i] = (unsigned char)(count_bits >> (8*i));

    static const unsigned char padding[64] = { 0x80 };
    index = (size_t)((ctx->count >> 3) & 0x3F);
    pad_len = (index < 56) ? (56 - index) : (120 - index);
    md5_update(ctx, padding, pad_len);
    md5_update(ctx, bits, 8);

    for (int i = 0; i < 4; i++) {
        digest[i*4]   = (unsigned char)(ctx->state[i]);
        digest[i*4+1] = (unsigned char)(ctx->state[i] >> 8);
        digest[i*4+2] = (unsigned char)(ctx->state[i] >> 16);
        digest[i*4+3] = (unsigned char)(ctx->state[i] >> 24);
    }
}

static void bytes_to_hex(const unsigned char *bytes, size_t len, char *out) {
    for (size_t i = 0; i < len; i++) {
        sprintf(out + i*2, "%02x", bytes[i]);
    }
    out[len*2] = '\0';
}

JscValue *crypto_md5(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "crypto.md5() espera 1 argumento", (Node *)n);
        return NULL;
    }

    JscValue *s = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *src = (s->type == VAL_STRING && s->as.s) ? s->as.s : "";

    MD5_CTX ctx;
    md5_init(&ctx);
    md5_update(&ctx, (const unsigned char *)src, strlen(src));
    unsigned char digest[16];
    md5_final(digest, &ctx);

    char hex[33];
    bytes_to_hex(digest, 16, hex);

    jsc_value_free(s);
    return jsc_string(hex);
}

/* ============================================================
 * SHA-1 — implementação padrão FIPS 180-4
 * ============================================================ */

typedef struct {
    uint32_t state[5];
    uint64_t count;
    unsigned char buffer[64];
} SHA1_CTX;

static void sha1_transform(uint32_t state[5], const unsigned char block[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) |
               ((uint32_t)block[i*4+3]);
    }
    for (int i = 16; i < 80; i++) {
        w[i] = ((w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16]) << 1) |
               ((w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16]) >> 31);
    }

    uint32_t a = state[0], b = state[1], cc = state[2], d = state[3], e = state[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & cc) | (~b & d);          k = 0x5A827999; }
        else if (i < 40) { f = b ^ cc ^ d;                    k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
        else             { f = b ^ cc ^ d;                    k = 0xCA62C1D6; }

        uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
        e = d;
        d = cc;
        cc = (b << 30) | (b >> 2);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += cc;
    state[3] += d;
    state[4] += e;
}

static void sha1_init(SHA1_CTX *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
}

static void sha1_update(SHA1_CTX *ctx, const unsigned char *data, size_t len) {
    size_t i, index, part_len;
    index = (size_t)((ctx->count >> 3) & 0x3F);
    ctx->count += (uint64_t)len << 3;
    part_len = 64 - index;

    if (len >= part_len) {
        memcpy(&ctx->buffer[index], data, part_len);
        sha1_transform(ctx->state, ctx->buffer);
        for (i = part_len; i + 63 < len; i += 64) {
            sha1_transform(ctx->state, &data[i]);
        }
        index = 0;
    } else {
        i = 0;
    }
    memcpy(&ctx->buffer[index], &data[i], len - i);
}

static void sha1_final(unsigned char digest[20], SHA1_CTX *ctx) {
    unsigned char bits[8];
    size_t index, pad_len;
    uint64_t count_bits = ctx->count;

    for (int i = 0; i < 8; i++) bits[i] = (unsigned char)(count_bits >> (8*(7 - i)));

    static const unsigned char padding[64] = { 0x80 };
    index = (size_t)((ctx->count >> 3) & 0x3F);
    pad_len = (index < 56) ? (56 - index) : (120 - index);
    sha1_update(ctx, padding, pad_len);
    sha1_update(ctx, bits, 8);

    for (int i = 0; i < 5; i++) {
        digest[i*4]   = (unsigned char)(ctx->state[i] >> 24);
        digest[i*4+1] = (unsigned char)(ctx->state[i] >> 16);
        digest[i*4+2] = (unsigned char)(ctx->state[i] >> 8);
        digest[i*4+3] = (unsigned char)(ctx->state[i]);
    }
}

JscValue *crypto_sha1(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "crypto.sha1() espera 1 argumento", (Node *)n);
        return NULL;
    }

    JscValue *s = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *src = (s->type == VAL_STRING && s->as.s) ? s->as.s : "";

    SHA1_CTX ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, (const unsigned char *)src, strlen(src));
    unsigned char digest[20];
    sha1_final(digest, &ctx);

    char hex[41];
    bytes_to_hex(digest, 20, hex);

    jsc_value_free(s);
    return jsc_string(hex);
}

/* ============================================================
 * SHA-256 — implementação padrão FIPS 180-4
 * ============================================================ */

typedef struct {
    uint32_t state[8];
    uint64_t count;
    unsigned char buffer[64];
} SHA256_CTX;

static const uint32_t SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define SHA256_ROTR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_transform(uint32_t state[8], const unsigned char block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) |
               ((uint32_t)block[i*4+3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = SHA256_ROTR(w[i-15], 7) ^ SHA256_ROTR(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = SHA256_ROTR(w[i-2], 17) ^ SHA256_ROTR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    uint32_t a = state[0], b = state[1], cc = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = SHA256_ROTR(e, 6) ^ SHA256_ROTR(e, 11) ^ SHA256_ROTR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + SHA256_K[i] + w[i];
        uint32_t S0 = SHA256_ROTR(a, 2) ^ SHA256_ROTR(a, 13) ^ SHA256_ROTR(a, 22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += cc; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx) {
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

static void sha256_update(SHA256_CTX *ctx, const unsigned char *data, size_t len) {
    size_t i, index, part_len;
    index = (size_t)((ctx->count >> 3) & 0x3F);
    ctx->count += (uint64_t)len << 3;
    part_len = 64 - index;

    if (len >= part_len) {
        memcpy(&ctx->buffer[index], data, part_len);
        sha256_transform(ctx->state, ctx->buffer);
        for (i = part_len; i + 63 < len; i += 64) {
            sha256_transform(ctx->state, &data[i]);
        }
        index = 0;
    } else {
        i = 0;
    }
    memcpy(&ctx->buffer[index], &data[i], len - i);
}

static void sha256_final(unsigned char digest[32], SHA256_CTX *ctx) {
    unsigned char bits[8];
    size_t index, pad_len;
    uint64_t count_bits = ctx->count;

    for (int i = 0; i < 8; i++) bits[i] = (unsigned char)(count_bits >> (8*(7 - i)));

    static const unsigned char padding[64] = { 0x80 };
    index = (size_t)((ctx->count >> 3) & 0x3F);
    pad_len = (index < 56) ? (56 - index) : (120 - index);
    sha256_update(ctx, padding, pad_len);
    sha256_update(ctx, bits, 8);

    for (int i = 0; i < 8; i++) {
        digest[i*4]   = (unsigned char)(ctx->state[i] >> 24);
        digest[i*4+1] = (unsigned char)(ctx->state[i] >> 16);
        digest[i*4+2] = (unsigned char)(ctx->state[i] >> 8);
        digest[i*4+3] = (unsigned char)(ctx->state[i]);
    }
}

JscValue *crypto_sha256(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "crypto.sha256() espera 1 argumento", (Node *)n);
        return NULL;
    }

    JscValue *s = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *src = (s->type == VAL_STRING && s->as.s) ? s->as.s : "";

    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const unsigned char *)src, strlen(src));
    unsigned char digest[32];
    sha256_final(digest, &ctx);

    char hex[65];
    bytes_to_hex(digest, 32, hex);

    jsc_value_free(s);
    return jsc_string(hex);
}

/* ============================================================
 * SHA-512 — implementação padrão FIPS 180-4
 * ============================================================ */

typedef struct {
    uint64_t state[8];
    uint64_t count_lo;
    uint64_t count_hi;
    unsigned char buffer[128];
} SHA512_CTX;

static const uint64_t SHA512_K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

#define SHA512_ROTR(x,n) (((x) >> (n)) | ((x) << (64 - (n))))

static void sha512_transform(uint64_t state[8], const unsigned char block[128]) {
    uint64_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint64_t)block[i*8] << 56) |
               ((uint64_t)block[i*8+1] << 48) |
               ((uint64_t)block[i*8+2] << 40) |
               ((uint64_t)block[i*8+3] << 32) |
               ((uint64_t)block[i*8+4] << 24) |
               ((uint64_t)block[i*8+5] << 16) |
               ((uint64_t)block[i*8+6] << 8) |
               ((uint64_t)block[i*8+7]);
    }
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = SHA512_ROTR(w[i-15], 1) ^ SHA512_ROTR(w[i-15], 8) ^ (w[i-15] >> 7);
        uint64_t s1 = SHA512_ROTR(w[i-2], 19) ^ SHA512_ROTR(w[i-2], 61) ^ (w[i-2] >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    uint64_t a = state[0], b = state[1], cc = state[2], d = state[3];
    uint64_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 80; i++) {
        uint64_t S1 = SHA512_ROTR(e, 14) ^ SHA512_ROTR(e, 18) ^ SHA512_ROTR(e, 41);
        uint64_t ch = (e & f) ^ (~e & g);
        uint64_t t1 = h + S1 + ch + SHA512_K[i] + w[i];
        uint64_t S0 = SHA512_ROTR(a, 28) ^ SHA512_ROTR(a, 34) ^ SHA512_ROTR(a, 39);
        uint64_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        uint64_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += cc; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void sha512_init(SHA512_CTX *ctx) {
    ctx->state[0] = 0x6a09e667f3bcc908ULL;
    ctx->state[1] = 0xbb67ae8584caa73bULL;
    ctx->state[2] = 0x3c6ef372fe94f82bULL;
    ctx->state[3] = 0xa54ff53a5f1d36f1ULL;
    ctx->state[4] = 0x510e527fade682d1ULL;
    ctx->state[5] = 0x9b05688c2b3e6c1fULL;
    ctx->state[6] = 0x1f83d9abfb41bd6bULL;
    ctx->state[7] = 0x5be0cd19137e2179ULL;
    ctx->count_lo = 0;
    ctx->count_hi = 0;
}

static void sha512_update(SHA512_CTX *ctx, const unsigned char *data, size_t len) {
    size_t i, index, part_len;
    uint64_t old_lo = ctx->count_lo;
    ctx->count_lo += (uint64_t)len << 3;
    if (ctx->count_lo < old_lo) ctx->count_hi++;
    ctx->count_hi += (uint64_t)len >> 61;

    index = (size_t)((old_lo >> 3) & 0x7F);
    part_len = 128 - index;

    if (len >= part_len) {
        memcpy(&ctx->buffer[index], data, part_len);
        sha512_transform(ctx->state, ctx->buffer);
        for (i = part_len; i + 127 < len; i += 128) {
            sha512_transform(ctx->state, &data[i]);
        }
        index = 0;
    } else {
        i = 0;
    }
    memcpy(&ctx->buffer[index], &data[i], len - i);
}

static void sha512_final(unsigned char digest[64], SHA512_CTX *ctx) {
    unsigned char bits[16];
    uint64_t lo = ctx->count_lo;
    uint64_t hi = ctx->count_hi;

    for (int i = 0; i < 8; i++) bits[i]     = (unsigned char)(hi >> (8*(7 - i)));
    for (int i = 0; i < 8; i++) bits[i + 8] = (unsigned char)(lo >> (8*(7 - i)));

    static const unsigned char padding[128] = { 0x80 };
    size_t index = (size_t)((lo >> 3) & 0x7F);
    size_t pad_len = (index < 112) ? (112 - index) : (240 - index);
    sha512_update(ctx, padding, pad_len);
    sha512_update(ctx, bits, 16);

    for (int i = 0; i < 8; i++) {
        digest[i*8]   = (unsigned char)(ctx->state[i] >> 56);
        digest[i*8+1] = (unsigned char)(ctx->state[i] >> 48);
        digest[i*8+2] = (unsigned char)(ctx->state[i] >> 40);
        digest[i*8+3] = (unsigned char)(ctx->state[i] >> 32);
        digest[i*8+4] = (unsigned char)(ctx->state[i] >> 24);
        digest[i*8+5] = (unsigned char)(ctx->state[i] >> 16);
        digest[i*8+6] = (unsigned char)(ctx->state[i] >> 8);
        digest[i*8+7] = (unsigned char)(ctx->state[i]);
    }
}

JscValue *crypto_sha512(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "crypto.sha512() espera 1 argumento", (Node *)n);
        return NULL;
    }

    JscValue *s = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *src = (s->type == VAL_STRING && s->as.s) ? s->as.s : "";

    SHA512_CTX ctx;
    sha512_init(&ctx);
    sha512_update(&ctx, (const unsigned char *)src, strlen(src));
    unsigned char digest[64];
    sha512_final(digest, &ctx);

    char hex[129];
    bytes_to_hex(digest, 64, hex);

    jsc_value_free(s);
    return jsc_string(hex);
}

/* ============================================================
 * HMAC-SHA256
 * ============================================================ */

static void hmac_sha256(const unsigned char *key, size_t key_len,
                        const unsigned char *msg, size_t msg_len,
                        unsigned char out[32]) {
    unsigned char k[64];
    unsigned char k_ipad[64];
    unsigned char k_opad[64];

    if (key_len > 64) {
        SHA256_CTX tmp;
        sha256_init(&tmp);
        sha256_update(&tmp, key, key_len);
        sha256_final(k, &tmp);
        key_len = 32;
    } else {
        memcpy(k, key, key_len);
    }
    memset(k + key_len, 0, 64 - key_len);

    for (int i = 0; i < 64; i++) {
        k_ipad[i] = k[i] ^ 0x36;
        k_opad[i] = k[i] ^ 0x5c;
    }

    SHA256_CTX ctx;
    unsigned char inner[32];
    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, 64);
    sha256_update(&ctx, msg, msg_len);
    sha256_final(inner, &ctx);

    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(out, &ctx);
}

JscValue *crypto_hmac_sha256(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "crypto.hmac_sha256() espera 2 argumentos", (Node *)n);
        return NULL;
    }

    JscValue *msg_v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    JscValue *key_v = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(msg_v); return NULL; }

    const char *msg = (msg_v->type == VAL_STRING && msg_v->as.s) ? msg_v->as.s : "";
    const char *key = (key_v->type == VAL_STRING && key_v->as.s) ? key_v->as.s : "";

    unsigned char digest[32];
    hmac_sha256((const unsigned char *)key, strlen(key),
                (const unsigned char *)msg, strlen(msg),
                digest);

    char hex[65];
    bytes_to_hex(digest, 32, hex);

    jsc_value_free(msg_v);
    jsc_value_free(key_v);
    return jsc_string(hex);
}

/* ============================================================
 * BLOCO NET — jsc net$+ (sockets TCP, DNS, scan, HTTP)
 * ============================================================ */

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>




/* net.resolve(host) → ip */
JscValue *net_resolve(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "net.resolve() espera 1 argumento", (Node *)n);
        return NULL;
    }

    JscValue *h = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *host = (h->type == VAL_STRING && h->as.s) ? h->as.s : "";

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    JscValue *result = NULL;
    if (getaddrinfo(host, NULL, &hints, &res) == 0) {
        struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf));
        result = jsc_string(buf);
        freeaddrinfo(res);
    } else {
        runtime_error(state, "net.resolve: nao consegui resolver", (Node *)n);
    }

    jsc_value_free(h);
    return result;
}

/* Helper: connect com timeout em milissegundos */
static int net_connect_timeout(const char *host, int porta, int timeout_ms) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { return -1; }

    /* Modo non-blocking */
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)porta);

    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        /* Tenta resolver */
        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host, NULL, &hints, &res) != 0) {
            close(sock);
            return -1;
        }
        struct sockaddr_in *a = (struct sockaddr_in *)res->ai_addr;
        addr.sin_addr = a->sin_addr;
        freeaddrinfo(res);
    }

    int r = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (r == 0) {
        /* Conectou direto */
        fcntl(sock, F_SETFL, flags);
        return sock;
    }

    if (errno != EINPROGRESS) {
        close(sock);
        return -1;
    }

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    r = select(sock + 1, NULL, &wfds, NULL, &tv);
    if (r <= 0) {
        close(sock);
        return -1;
    }

    int so_error = 0;
    socklen_t len = sizeof(so_error);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
    if (so_error != 0) {
        close(sock);
        return -1;
    }

    fcntl(sock, F_SETFL, flags);
    return sock;
}

/* ============================================================
 * LOTE 4 P2 — pacote hacker (reconnaissance)
 * ============================================================ */

#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netdb.h>
#include <arpa/inet.h>

/* ---------- HACK.BANNER ---------- */

JscValue *hack_banner(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "hack.banner() espera 2 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *h = eval(n->args.items[0], env, state);
    JscValue *p = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(h); jsc_value_free(p); return NULL; }

    char *host = strdup((h->type == VAL_STRING && h->as.s) ? h->as.s : "");
    int porta = (int)jsc_value_to_int(p);
    jsc_value_free(h); jsc_value_free(p);

    int sock = net_connect_timeout(host, porta, 2000);
    if (sock < 0) {
        free(host);
        return jsc_string("");
    }

    /* Envia apenas um \n pra provocar resposta */
    send(sock, "\r\n", 2, 0);

    /* Timeout curto no recv */
    struct timeval tv;
    tv.tv_sec = 3; tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buf[1024];
    ssize_t r = recv(sock, buf, sizeof(buf) - 1, 0);
    close(sock);
    free(host);

    if (r <= 0) return jsc_string("");
    buf[r] = '\0';
    return jsc_string(buf);
}

/* ---------- HACK.HEADERS ---------- */

JscValue *hack_headers(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "hack.headers() espera 1 argumento (url)", (Node *)n);
        return NULL;
    }
    JscValue *u = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    char *url = strdup((u->type == VAL_STRING && u->as.s) ? u->as.s : "");
    jsc_value_free(u);

    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        runtime_error(state, "hack.headers: use http:// ou https://", (Node *)n);
        free(url);
        return NULL;
    }

    int is_https = (strncmp(url, "https://", 8) == 0);
    const char *host_start = url + (is_https ? 8 : 7);
    const char *path = strchr(host_start, '/');
    char host[256];
    int porta = is_https ? 443 : 80;

    if (path) {
        size_t hl = path - host_start;
        if (hl >= sizeof(host)) hl = sizeof(host) - 1;
        memcpy(host, host_start, hl);
        host[hl] = '\0';
    } else {
        strncpy(host, host_start, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
        path = "/";
    }

    /* https não suportado por enquanto */
    if (is_https) {
        runtime_error(state, "hack.headers: https ainda nao suportado", (Node *)n);
        free(url);
        return NULL;
    }

    int sock = net_connect_timeout(host, porta, 5000);
    if (sock < 0) {
        runtime_error(state, "hack.headers: falha ao conectar", (Node *)n);
        free(url);
        return NULL;
    }

    char req[1024];
    snprintf(req, sizeof(req),
             "HEAD %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: JSC$+/1.0\r\nConnection: close\r\n\r\n",
             path, host);
    send(sock, req, strlen(req), 0);

    struct timeval tv;
    tv.tv_sec = 5; tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    size_t cap = 8192, total = 0;
    char *buf = (char *)malloc(cap);
    ssize_t r;
    while ((r = recv(sock, buf + total, cap - total - 1, 0)) > 0) {
        total += r;
        if (total + 1024 >= cap) { cap *= 2; buf = (char *)realloc(buf, cap); }
    }
    buf[total] = '\0';
    close(sock);
    free(url);

    JscValue *result = jsc_string(buf);
    free(buf);
    return result;
}

/* ---------- HACK.PATHS ---------- */

JscValue *hack_paths(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "hack.paths() espera 2 argumentos (url, array de paths)", (Node *)n);
        return NULL;
    }
    JscValue *u = eval(n->args.items[0], env, state);
    JscValue *w = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(u); jsc_value_free(w); return NULL; }

    char *url = strdup((u->type == VAL_STRING && u->as.s) ? u->as.s : "");
    jsc_value_free(u);

    if (w->type != VAL_ARRAY) {
        runtime_error(state, "hack.paths: segundo argumento deve ser array", (Node *)n);
        free(url); jsc_value_free(w);
        return NULL;
    }

    /* Parse URL */
    if (strncmp(url, "http://", 7) != 0) {
        runtime_error(state, "hack.paths: use http://", (Node *)n);
        free(url); jsc_value_free(w);
        return NULL;
    }
    const char *host_start = url + 7;
    const char *slash = strchr(host_start, '/');
    char host[256];
    if (slash) {
        size_t hl = slash - host_start;
        if (hl >= sizeof(host)) hl = sizeof(host) - 1;
        memcpy(host, host_start, hl);
        host[hl] = '\0';
    } else {
        strncpy(host, host_start, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
    }

    JscValue *achados = jsc_array();

    for (size_t i = 0; i < w->as.array.count; i++) {
        JscValue *path_v = w->as.array.items[i];
        if (path_v->type != VAL_STRING) continue;
        const char *path = path_v->as.s ? path_v->as.s : "";
        if (path[0] != '/') continue;

        int sock = net_connect_timeout(host, 80, 1500);
        if (sock < 0) continue;

        char req[1024];
        snprintf(req, sizeof(req),
                 "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: JSC$+\r\nConnection: close\r\n\r\n",
                 path, host);
        send(sock, req, strlen(req), 0);

        struct timeval tv;
        tv.tv_sec = 2; tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        char buf[256];
        ssize_t r = recv(sock, buf, sizeof(buf) - 1, 0);
        close(sock);
        if (r <= 0) continue;
        buf[r] = '\0';

        /* Verifica se é 200, 301, 302, 403 (existe) */
        if (strstr(buf, "200 ") || strstr(buf, "301 ") ||
            strstr(buf, "302 ") || strstr(buf, "403 ")) {
            /* Extrai a primeira linha */
            char *nl = strchr(buf, '\r');
            if (!nl) nl = strchr(buf, '\n');
            if (nl) *nl = '\0';
            char entry[512];
            snprintf(entry, sizeof(entry), "%s -> %s", path, buf);
            jsc_array_push(achados, jsc_string(entry));
        }
    }

    free(url);
    jsc_value_free(w);
    return achados;
}

/* ---------- HACK.SUBDOMAIN ---------- */

JscValue *hack_subdomain(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "hack.subdomain() espera 2 argumentos (dominio, array de subdominios)", (Node *)n);
        return NULL;
    }
    JscValue *d = eval(n->args.items[0], env, state);
    JscValue *w = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(d); jsc_value_free(w); return NULL; }

    char *dominio = strdup((d->type == VAL_STRING && d->as.s) ? d->as.s : "");
    jsc_value_free(d);

    if (w->type != VAL_ARRAY) {
        runtime_error(state, "hack.subdomain: segundo argumento deve ser array", (Node *)n);
        free(dominio); jsc_value_free(w);
        return NULL;
    }

    JscValue *achados = jsc_array();

    for (size_t i = 0; i < w->as.array.count; i++) {
        JscValue *sub_v = w->as.array.items[i];
        if (sub_v->type != VAL_STRING) continue;
        const char *sub = sub_v->as.s ? sub_v->as.s : "";
        if (!sub[0]) continue;

        char full[512];
        snprintf(full, sizeof(full), "%s.%s", sub, dominio);

        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(full, NULL, &hints, &res) == 0) {
            struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
            char entry[1024];
            snprintf(entry, sizeof(entry), "%s -> %s", full, ip);
            jsc_array_push(achados, jsc_string(entry));
            freeaddrinfo(res);
        }
    }

    free(dominio);
    jsc_value_free(w);
    return achados;
}

/* ---------- HACK.CDN_CHECK ---------- */

JscValue *hack_cdn_check(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "hack.cdn_check() espera 1 argumento (host ou ip)", (Node *)n);
        return NULL;
    }
    JscValue *h = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *host = (h->type == VAL_STRING && h->as.s) ? h->as.s : "";

    /* Resolve pra pegar IP */
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, NULL, &hints, &res) != 0) {
        jsc_value_free(h);
        return jsc_string("desconhecido");
    }

    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
    unsigned int ip = ntohl(addr->sin_addr.s_addr);
    freeaddrinfo(res);
    jsc_value_free(h);

    /* Faixas conhecidas */
    /* Cloudflare: 103.21.244.0/22, 104.16.0.0/13, 172.64.0.0/13, etc */
    if ((ip & 0xFFFFFC00) == 0x671CF000) return jsc_string("Cloudflare");
    if ((ip & 0xFFF80000) == 0x68100000) return jsc_string("Cloudflare");
    if ((ip & 0xFFF80000) == 0xAC400000) return jsc_string("Cloudflare");

    /* Amazon AWS: 52.0.0.0/8, 54.0.0.0/8 */
    if ((ip & 0xFF000000) == 0x34000000) return jsc_string("AWS");
    if ((ip & 0xFF000000) == 0x36000000) return jsc_string("AWS");

    /* Google: 142.250.0.0/15, 172.217.0.0/16 */
    if ((ip & 0xFFFE0000) == 0x8EFA0000) return jsc_string("Google");
    if ((ip & 0xFFFF0000) == 0xACD90000) return jsc_string("Google");

    /* Azure: 13.x, 20.x, 40.x, 51.x */
    if ((ip & 0xFF000000) == 0x0D000000) return jsc_string("Azure");
    if ((ip & 0xFF000000) == 0x14000000) return jsc_string("Azure");

    return jsc_string("direto");
}

/* ---------- CRYPTO.CRACK_* ---------- */

JscValue *crypto_crack_md5(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "crypto.crack_md5() espera 2 argumentos (hash, array)", (Node *)n);
        return NULL;
    }
    JscValue *h = eval(n->args.items[0], env, state);
    JscValue *w = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(h); jsc_value_free(w); return NULL; }

    const char *alvo = (h->type == VAL_STRING && h->as.s) ? h->as.s : "";
    if (w->type != VAL_ARRAY) {
        jsc_value_free(h); jsc_value_free(w);
        runtime_error(state, "crypto.crack_md5: 2o argumento deve ser array", (Node *)n);
        return NULL;
    }

    /* Preciso chamar o crypto_md5 internamente. Vou replicar a lógica. */
    extern JscValue *crypto_md5(void *, void *, void *);

    for (size_t i = 0; i < w->as.array.count; i++) {
        JscValue *cand = w->as.array.items[i];
        if (cand->type != VAL_STRING) continue;

        /* Chama crypto_md5 com o candidato */
        /* Melhor: implementar hash direto */
        MD5_CTX ctx;
        md5_init(&ctx);
        md5_update(&ctx, (const unsigned char *)cand->as.s, strlen(cand->as.s));
        unsigned char digest[16];
        md5_final(digest, &ctx);
        char hex[33];
        bytes_to_hex(digest, 16, hex);

        if (strcmp(hex, alvo) == 0) {
            JscValue *r = jsc_string(cand->as.s);
            jsc_value_free(h); jsc_value_free(w);
            return r;
        }
    }

    jsc_value_free(h); jsc_value_free(w);
    return jsc_string("");
}

JscValue *crypto_crack_sha1(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "crypto.crack_sha1() espera 2 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *h = eval(n->args.items[0], env, state);
    JscValue *w = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(h); jsc_value_free(w); return NULL; }

    const char *alvo = (h->type == VAL_STRING && h->as.s) ? h->as.s : "";
    if (w->type != VAL_ARRAY) {
        jsc_value_free(h); jsc_value_free(w);
        runtime_error(state, "crypto.crack_sha1: 2o argumento deve ser array", (Node *)n);
        return NULL;
    }

    for (size_t i = 0; i < w->as.array.count; i++) {
        JscValue *cand = w->as.array.items[i];
        if (cand->type != VAL_STRING) continue;

        SHA1_CTX ctx;
        sha1_init(&ctx);
        sha1_update(&ctx, (const unsigned char *)cand->as.s, strlen(cand->as.s));
        unsigned char digest[20];
        sha1_final(digest, &ctx);
        char hex[41];
        bytes_to_hex(digest, 20, hex);

        if (strcmp(hex, alvo) == 0) {
            JscValue *r = jsc_string(cand->as.s);
            jsc_value_free(h); jsc_value_free(w);
            return r;
        }
    }

    jsc_value_free(h); jsc_value_free(w);
    return jsc_string("");
}

JscValue *crypto_crack_sha256(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "crypto.crack_sha256() espera 2 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *h = eval(n->args.items[0], env, state);
    JscValue *w = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(h); jsc_value_free(w); return NULL; }

    const char *alvo = (h->type == VAL_STRING && h->as.s) ? h->as.s : "";
    if (w->type != VAL_ARRAY) {
        jsc_value_free(h); jsc_value_free(w);
        runtime_error(state, "crypto.crack_sha256: 2o argumento deve ser array", (Node *)n);
        return NULL;
    }

    for (size_t i = 0; i < w->as.array.count; i++) {
        JscValue *cand = w->as.array.items[i];
        if (cand->type != VAL_STRING) continue;

        SHA256_CTX ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, (const unsigned char *)cand->as.s, strlen(cand->as.s));
        unsigned char digest[32];
        sha256_final(digest, &ctx);
        char hex[65];
        bytes_to_hex(digest, 32, hex);

        if (strcmp(hex, alvo) == 0) {
            JscValue *r = jsc_string(cand->as.s);
            jsc_value_free(h); jsc_value_free(w);
            return r;
        }
    }

    jsc_value_free(h); jsc_value_free(w);
    return jsc_string("");
}

/* ---------- NET.IP_PUBLICO ---------- */

JscValue *net_ip_publico(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo("ifconfig.me", NULL, &hints, &res) != 0) {
        return jsc_string("");
    }
    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
    freeaddrinfo(res);

    int sock = net_connect_timeout("ifconfig.me", 80, 5000);
    if (sock < 0) return jsc_string("");

    const char *req = "GET /ip HTTP/1.0\r\nHost: ifconfig.me\r\nUser-Agent: JSC$+\r\n\r\n";
    send(sock, req, strlen(req), 0);

    struct timeval tv;
    tv.tv_sec = 5; tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buf[8192];
    ssize_t r = recv(sock, buf, sizeof(buf) - 1, 0);
    close(sock);
    if (r <= 0) return jsc_string("");
    buf[r] = '\0';

    /* Pega só o corpo (depois de \r\n\r\n) */
    char *body = strstr(buf, "\r\n\r\n");
    if (body) body += 4;
    else body = buf;

    /* Remove \n do final */
    size_t bl = strlen(body);
    while (bl > 0 && (body[bl-1] == '\n' || body[bl-1] == '\r')) body[--bl] = '\0';

    return jsc_string(body);
}

/* ---------- NET.TRACEROUTE (simples, sem raw socket) ---------- */

JscValue *net_traceroute(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count < 1 || n->args.count > 2) {
        runtime_error(state, "net.traceroute() espera 1 ou 2 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *h = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *host = (h->type == VAL_STRING && h->as.s) ? h->as.s : "";
    int max_hops = 30;
    if (n->args.count == 2) {
        JscValue *m = eval(n->args.items[1], env, state);
        if (m) { max_hops = (int)jsc_value_to_int(m); jsc_value_free(m); }
        if (max_hops > 64) max_hops = 64;
    }

    JscValue *hops = jsc_array();

    /* Implementação simples via UDP com TTL crescente */
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    if (getaddrinfo(host, "33434", &hints, &res) != 0) {
        jsc_value_free(h);
        runtime_error(state, "net.traceroute: falha ao resolver", (Node *)n);
        return NULL;
    }

    struct sockaddr_in dest = *(struct sockaddr_in *)res->ai_addr;
    freeaddrinfo(res);

    for (int ttl = 1; ttl <= max_hops; ttl++) {
        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) break;

        setsockopt(sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));

        struct timeval tv;
        tv.tv_sec = 2; tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        sendto(sock, "JSC", 3, 0, (struct sockaddr *)&dest, sizeof(dest));
        close(sock);

        /* Sem raw socket, não conseguimos ler o ICMP de volta.
         * Vou só adicionar o TTL na lista. */
        char entry[64];
        snprintf(entry, sizeof(entry), "ttl %d", ttl);
        jsc_array_push(hops, jsc_string(entry));
    }

    jsc_value_free(h);
    return hops;
}

/* ---------- MÓDULOS ---------- */

static JscValue *make_hack_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, jsc_string("banner"),       jsc_native("banner", hack_banner));
    jsc_map_set(m, jsc_string("headers"),      jsc_native("headers", hack_headers));
    jsc_map_set(m, jsc_string("paths"),        jsc_native("paths", hack_paths));
    jsc_map_set(m, jsc_string("subdomain"),    jsc_native("subdomain", hack_subdomain));
    jsc_map_set(m, jsc_string("cdn_check"),    jsc_native("cdn_check", hack_cdn_check));
    return m;
}


/* fs.walk(path) — lista recursivamente (retorna array de paths) */
static void walk_recursivo(const char *base, JscValue *arr) {
    DIR *d = opendir(base);
    if (!d) return;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", base, entry->d_name);
        jsc_array_push(arr, jsc_string(full));
        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            walk_recursivo(full, arr);
        }
    }
    closedir(d);
}

JscValue *fs_walk(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "fs.walk() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    char *path = strdup((v->type == VAL_STRING && v->as.s) ? v->as.s : "");
    jsc_value_free(v);

    JscValue *arr = jsc_array();
    walk_recursivo(path, arr);
    free(path);
    return arr;
}

static JscValue *make_fs_module(void) {
    JscValue *m = jsc_map();
    #define REG(name) jsc_map_set(m, jsc_string(#name), jsc_native(#name, fs_##name))
    REG(read);
    REG(write);
    REG(append);
    REG(exists);
    REG(is_file);
    REG(is_dir);
    REG(size);
    REG(remove);
    REG(mkdir);
    REG(list);
    REG(copy);
    REG(lines);
    REG(walk);
    #undef REG
    return m;
}

/* ============================================================
 * BLOCO CRYPTO.1 — jsc crypto$+ (base64, hex, random, xor)
 * ============================================================ */

#include <time.h>

/* crypto.base64_encode(s) → string */
JscValue *crypto_base64_encode(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "crypto.base64_encode() espera 1 argumento", (Node *)n);
        return NULL;
    }

    JscValue *s = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *src = (s->type == VAL_STRING && s->as.s) ? s->as.s : "";
    size_t len = strlen(src);

    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len = ((len + 2) / 3) * 4;
    char *out = (char *)malloc(out_len + 1);
    size_t i, j;
    for (i = 0, j = 0; i < len;) {
        unsigned int a = i < len ? (unsigned char)src[i++] : 0;
        unsigned int b = i < len ? (unsigned char)src[i++] : 0;
        unsigned int c = i < len ? (unsigned char)src[i++] : 0;
        unsigned int triple = (a << 16) | (b << 8) | c;
        out[j++] = b64[(triple >> 18) & 0x3F];
        out[j++] = b64[(triple >> 12) & 0x3F];
        out[j++] = b64[(triple >> 6)  & 0x3F];
        out[j++] = b64[triple & 0x3F];
    }
    /* Padding */
    size_t pad = (3 - (len % 3)) % 3;
    for (size_t k = 0; k < pad; k++) {
        out[out_len - 1 - k] = '=';
    }
    out[out_len] = '\0';

    JscValue *result = jsc_string(out);
    free(out);
    jsc_value_free(s);
    return result;
}

/* crypto.base64_decode(s) → string */
JscValue *crypto_base64_decode(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "crypto.base64_decode() espera 1 argumento", (Node *)n);
        return NULL;
    }

    JscValue *s = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *src = (s->type == VAL_STRING && s->as.s) ? s->as.s : "";
    size_t len = strlen(src);

    /* Filtra caracteres válidos */
    int *map = (int *)malloc(256 * sizeof(int));
    for (int i = 0; i < 256; i++) map[i] = -1;
    const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; i++) map[(unsigned char)b64[i]] = i;

    char *out = (char *)malloc(len);
    size_t out_len = 0;
    int buf = 0, bits = 0;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '=') break;
        int v = map[(unsigned char)src[i]];
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[out_len++] = (buf >> bits) & 0xFF;
        }
    }
    out[out_len] = '\0';

    JscValue *result = jsc_string(out);
    free(out);
    free(map);
    jsc_value_free(s);
    return result;
}

/* crypto.hex_encode(s) → string */
JscValue *crypto_hex_encode(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "crypto.hex_encode() espera 1 argumento", (Node *)n);
        return NULL;
    }

    JscValue *s = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *src = (s->type == VAL_STRING && s->as.s) ? s->as.s : "";
    size_t len = strlen(src);

    char *out = (char *)malloc(len * 2 + 1);
    for (size_t i = 0; i < len; i++) {
        sprintf(out + i * 2, "%02x", (unsigned char)src[i]);
    }
    out[len * 2] = '\0';

    JscValue *result = jsc_string(out);
    free(out);
    jsc_value_free(s);
    return result;
}

/* crypto.hex_decode(s) → string */
JscValue *crypto_hex_decode(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "crypto.hex_decode() espera 1 argumento", (Node *)n);
        return NULL;
    }

    JscValue *s = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *src = (s->type == VAL_STRING && s->as.s) ? s->as.s : "";
    size_t len = strlen(src);

    char *out = (char *)malloc(len / 2 + 1);
    size_t out_len = 0;
    for (size_t i = 0; i + 1 < len; i += 2) {
        char buf[3] = { src[i], src[i+1], '\0' };
        out[out_len++] = (char)strtol(buf, NULL, 16);
    }
    out[out_len] = '\0';

    JscValue *result = jsc_string(out);
    free(out);
    jsc_value_free(s);
    return result;
}

/* crypto.random_bytes(n) → string com n bytes aleatórios */
JscValue *crypto_random_bytes(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "crypto.random_bytes() espera 1 argumento", (Node *)n);
        return NULL;
    }

    JscValue *nv = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    long long qty = jsc_value_to_int(nv);
    jsc_value_free(nv);
    if (qty < 0) qty = 0;
    if (qty > 4096) qty = 4096;

    static int seeded = 0;
    if (!seeded) { srand((unsigned)time(NULL) ^ (unsigned)getpid()); seeded = 1; }

    char *buf = (char *)malloc(qty + 1);
    for (long long i = 0; i < qty; i++) {
        buf[i] = (char)(rand() & 0xFF);
    }
    buf[qty] = '\0';

    JscValue *result = jsc_string(buf);
    free(buf);
    return result;
}

/* crypto.random_hex(n) → 2n caracteres hex aleatórios */
JscValue *crypto_random_hex(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "crypto.random_hex() espera 1 argumento", (Node *)n);
        return NULL;
    }

    JscValue *nv = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    long long qty = jsc_value_to_int(nv);
    jsc_value_free(nv);
    if (qty < 0) qty = 0;
    if (qty > 2048) qty = 2048;

    static int seeded = 0;
    if (!seeded) { srand((unsigned)time(NULL) ^ (unsigned)getpid()); seeded = 1; }

    char *buf = (char *)malloc(qty * 2 + 1);
    for (long long i = 0; i < qty; i++) {
        sprintf(buf + i * 2, "%02x", rand() & 0xFF);
    }
    buf[qty * 2] = '\0';

    JscValue *result = jsc_string(buf);
    free(buf);
    return result;
}

/* crypto.random_int(min, max) → int aleatório */
JscValue *crypto_random_int(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "crypto.random_int() espera 2 argumentos", (Node *)n);
        return NULL;
    }

    JscValue *a = eval(n->args.items[0], env, state);
    JscValue *b = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(a); jsc_value_free(b); return NULL; }
    long long lo = jsc_value_to_int(a);
    long long hi = jsc_value_to_int(b);
    jsc_value_free(a); jsc_value_free(b);

    if (lo > hi) { long long t = lo; lo = hi; hi = t; }

    static int seeded = 0;
    if (!seeded) { srand((unsigned)time(NULL) ^ (unsigned)getpid()); seeded = 1; }

    long long range = hi - lo + 1;
    return jsc_int(lo + (rand() % range));
}

/* crypto.xor(s, key) → string XORada */
JscValue *crypto_xor(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "crypto.xor() espera 2 argumentos", (Node *)n);
        return NULL;
    }

    JscValue *s = eval(n->args.items[0], env, state);
    JscValue *k = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(s); jsc_value_free(k); return NULL; }

    const char *src = (s->type == VAL_STRING && s->as.s) ? s->as.s : "";
    const char *key = (k->type == VAL_STRING && k->as.s) ? k->as.s : "";
    size_t s_len = strlen(src);
    size_t k_len = strlen(key);

    if (k_len == 0) {
        runtime_error(state, "crypto.xor: chave vazia", (Node *)n);
        jsc_value_free(s); jsc_value_free(k);
        return NULL;
    }

    char *out = (char *)malloc(s_len + 1);
    for (size_t i = 0; i < s_len; i++) {
        out[i] = src[i] ^ key[i % k_len];
    }
    out[s_len] = '\0';

    JscValue *result = jsc_string(out);
    free(out);
    jsc_value_free(s); jsc_value_free(k);
    return result;
}

/* ============================================================
 * LOTE 1 — time, os, string, crypto.uuid, math extras
 * ============================================================ */

#include <time.h>
#include <sys/time.h>
#include <limits.h>

/* ---------- TIME ---------- */

JscValue *time_now(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;
    return jsc_int((long long)time(NULL));
}

JscValue *time_sleep(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "time.sleep() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *s = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    double seg = jsc_value_to_float(s);
    jsc_value_free(s);

    struct timespec ts;
    ts.tv_sec = (time_t)seg;
    ts.tv_nsec = (long)((seg - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
    return jsc_vazio();
}

JscValue *time_clock(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long long ms = (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    return jsc_int(ms);
}



/* ---------- OS ---------- */

#include <unistd.h>

JscValue *os_env(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "os.env() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *var = (v->type == VAL_STRING && v->as.s) ? v->as.s : "";
    const char *val = getenv(var);
    jsc_value_free(v);
    return jsc_string(val ? val : "");
}

JscValue *os_getcwd(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;
    char buf[4096];
    if (getcwd(buf, sizeof(buf)) == NULL) return jsc_string("");
    return jsc_string(buf);
}

JscValue *os_pid(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;
    return jsc_int((long long)getpid());
}

/* os.home() — diretorio home */
JscValue *os_home(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;
    const char *h = getenv("HOME");
    return jsc_string(h ? h : "/root");
}

/* os.tmpdir() — diretorio temporario */
JscValue *os_tmpdir(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;
    const char *t = getenv("TMPDIR");
    return jsc_string(t ? t : "/tmp");
}

/* os.mkdir_p(path) — cria pastas aninhadas */
JscValue *os_mkdir_p(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "os.mkdir_p() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    char *path = strdup((v->type == VAL_STRING && v->as.s) ? v->as.s : "");
    jsc_value_free(v);

    /* Cria cada nivel */
    char tmp[4096];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) { free(path); return jsc_bool(0); }
    strcpy(tmp, path);

    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0755);
            tmp[i] = '/';
        }
    }
    int r = mkdir(tmp, 0755);
    free(path);
    return jsc_bool(r == 0 || errno == EEXIST);
}

/* os.remove_dir(path) — remove diretorio (vazio) */
JscValue *os_remove_dir(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "os.remove_dir() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *path = (v->type == VAL_STRING && v->as.s) ? v->as.s : "";
    int r = rmdir(path);
    jsc_value_free(v);
    return jsc_bool(r == 0);
}

static JscValue *make_os_module(void) {
    JscValue *m = jsc_map();
    #define REG(name) jsc_map_set(m, jsc_string(#name), jsc_native(#name, os_##name))
    REG(env);
    REG(getcwd);
    REG(pid);
    REG(home);
    REG(tmpdir);
    REG(mkdir_p);
    REG(remove_dir);
    #undef REG
    return m;
}

/* ---------- STRING (modulo novo) ---------- */

JscValue *string_pad_left(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count < 2 || n->args.count > 3) {
        runtime_error(state, "string.pad_left() espera 2 ou 3 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *s = eval(n->args.items[0], env, state);
    JscValue *nv = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(s); jsc_value_free(nv); return NULL; }

    char pad = ' ';
    JscValue *cv = NULL;
    if (n->args.count == 3) {
        cv = eval(n->args.items[2], env, state);
        if (cv && cv->type == VAL_STRING && cv->as.s && cv->as.s[0]) pad = cv->as.s[0];
    }

    const char *src = (s->type == VAL_STRING && s->as.s) ? s->as.s : "";
    long long len = (long long)strlen(src);
    long long want = jsc_value_to_int(nv);

    if (want <= len) {
        JscValue *r = jsc_string(src);
        jsc_value_free(s); jsc_value_free(nv); if (cv) jsc_value_free(cv);
        return r;
    }

    long long total = want;
    char *out = (char *)malloc(total + 1);
    long long fill = total - len;
    for (long long i = 0; i < fill; i++) out[i] = pad;
    memcpy(out + fill, src, len);
    out[total] = '\0';

    JscValue *r = jsc_string(out);
    free(out);
    jsc_value_free(s); jsc_value_free(nv); if (cv) jsc_value_free(cv);
    return r;
}

JscValue *string_starts_with(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "string.starts_with() espera 2 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *a = eval(n->args.items[0], env, state);
    JscValue *b = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(a); jsc_value_free(b); return NULL; }

    const char *s = (a->type == VAL_STRING && a->as.s) ? a->as.s : "";
    const char *p = (b->type == VAL_STRING && b->as.s) ? b->as.s : "";
    size_t pl = strlen(p);
    int r = (strncmp(s, p, pl) == 0);

    jsc_value_free(a); jsc_value_free(b);
    return jsc_bool(r);
}

JscValue *string_ends_with(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "string.ends_with() espera 2 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *a = eval(n->args.items[0], env, state);
    JscValue *b = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(a); jsc_value_free(b); return NULL; }

    const char *s = (a->type == VAL_STRING && a->as.s) ? a->as.s : "";
    const char *sfx = (b->type == VAL_STRING && b->as.s) ? b->as.s : "";
    size_t sl = strlen(s), pl = strlen(sfx);
    int r = (pl <= sl) && (strcmp(s + sl - pl, sfx) == 0);

    jsc_value_free(a); jsc_value_free(b);
    return jsc_bool(r);
}

JscValue *string_reverse(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "string.reverse() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *a = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *s = (a->type == VAL_STRING && a->as.s) ? a->as.s : "";
    size_t len = strlen(s);
    char *out = (char *)malloc(len + 1);
    for (size_t i = 0; i < len; i++) out[i] = s[len - 1 - i];
    out[len] = '\0';

    JscValue *r = jsc_string(out);
    free(out);
    jsc_value_free(a);
    return r;
}

static JscValue *make_string_module(void) {
    JscValue *m = jsc_map();
    #define REG(name) jsc_map_set(m, jsc_string(#name), jsc_native(#name, string_##name))
    REG(pad_left);
    REG(starts_with);
    REG(ends_with);
    REG(reverse);
    #undef REG
    return m;
}

/* ---------- CRYPTO.UUID ---------- */

JscValue *crypto_uuid(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;
    static int seeded = 0;
    if (!seeded) { srand((unsigned)time(NULL) ^ (unsigned)getpid()); seeded = 1; }

    unsigned char bytes[16];
    for (int i = 0; i < 16; i++) bytes[i] = (unsigned char)(rand() & 0xFF);
    bytes[6] = (bytes[6] & 0x0F) | 0x40;   /* versao 4 */
    bytes[8] = (bytes[8] & 0x3F) | 0x80;   /* variant */

    char buf[40];
    sprintf(buf, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            bytes[0], bytes[1], bytes[2], bytes[3],
            bytes[4], bytes[5], bytes[6], bytes[7],
            bytes[8], bytes[9], bytes[10], bytes[11],
            bytes[12], bytes[13], bytes[14], bytes[15]);
    return jsc_string(buf);
}

/* ---------- MATH EXTRAS ---------- */









/* net.port_open(host, porta) → bool */
JscValue *net_port_open(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "net.port_open() espera 2 argumentos", (Node *)n);
        return NULL;
    }

    JscValue *h = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    JscValue *p = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(h); return NULL; }

    char *host = strdup((h->type == VAL_STRING && h->as.s) ? h->as.s : "");
    int porta = (int)jsc_value_to_int(p);
    jsc_value_free(h);
    jsc_value_free(p);

    int s = net_connect_timeout(host, porta, 500);
    free(host);
    if (s >= 0) {
        close(s);
        return jsc_bool(1);
    }
    return jsc_bool(0);
}

/* net.connect(host, porta) → int (socket fd) ou -1 */
JscValue *net_connect(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "net.connect() espera 2 argumentos", (Node *)n);
        return NULL;
    }

    JscValue *h = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    JscValue *p = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(h); return NULL; }

    char *host = strdup((h->type == VAL_STRING && h->as.s) ? h->as.s : "");
    int porta = (int)jsc_value_to_int(p);
    jsc_value_free(h);
    jsc_value_free(p);

    int s = net_connect_timeout(host, porta, 5000);
    free(host);
    if (s < 0) {
        runtime_error(state, "net.connect: falhou", (Node *)n);
        return jsc_int(-1);
    }
    return jsc_int(s);
}

/* net.send(sock, dados) → bool */
JscValue *net_send(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "net.send() espera 2 argumentos", (Node *)n);
        return NULL;
    }

    JscValue *s = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    JscValue *d = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(s); return NULL; }

    int sock = (int)jsc_value_to_int(s);
    char *data = strdup((d->type == VAL_STRING && d->as.s) ? d->as.s : "");
    size_t len = strlen(data);

    ssize_t sent = send(sock, data, len, 0);
    jsc_value_free(s);
    jsc_value_free(d);
    free(data);

    return jsc_bool(sent == (ssize_t)len);
}

/* net.recv(sock) → string (lê até 8192 bytes) */
JscValue *net_recv(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count < 1) {
        runtime_error(state, "net.recv() espera 1 argumento", (Node *)n);
        return NULL;
    }

    JscValue *s = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    int sock = (int)jsc_value_to_int(s);
    jsc_value_free(s);

    /* Timeout no recv */
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buf[8193];
    ssize_t r = recv(sock, buf, 8192, 0);
    if (r < 0) r = 0;
    buf[r] = '\0';

    return jsc_string(buf);
}

/* net.close(sock) */
JscValue *net_close(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "net.close() espera 1 argumento", (Node *)n);
        return NULL;
    }

    JscValue *s = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    int sock = (int)jsc_value_to_int(s);
    jsc_value_free(s);

    close(sock);
    return jsc_vazio();
}

/* net.http_get(url) → string com resposta */
JscValue *net_http_get(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "net.http_get() espera 1 argumento", (Node *)n);
        return NULL;
    }

    JscValue *u = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    char *url = strdup((u->type == VAL_STRING && u->as.s) ? u->as.s : "");
    jsc_value_free(u);

    /* Parse da URL: http://host[:porta]/caminho */
    if (strncmp(url, "http://", 7) != 0) {
        runtime_error(state, "net.http_get: so suporta http://", (Node *)n);
        jsc_value_free(u);
        return NULL;
    }

    const char *host_start = url + 7;
    const char *path = strchr(host_start, '/');
    char host[256];
    int porta = 80;

    if (path) {
        size_t host_len = path - host_start;
        if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
        memcpy(host, host_start, host_len);
        host[host_len] = '\0';
    } else {
        strncpy(host, host_start, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
        path = "/";
    }

    /* Verifica se tem :porta */
    char *colon = strchr(host, ':');
    if (colon) {
        *colon = '\0';
        porta = atoi(colon + 1);
    }

    int sock = net_connect_timeout(host, porta, 5000);
    if (sock < 0) {
        runtime_error(state, "net.http_get: nao consegui conectar", (Node *)n);
        jsc_value_free(u);
        return NULL;
    }

    /* Monta request */
    char req[1024];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: JSC$+/1.0\r\nConnection: close\r\n\r\n",
             path, host);

    send(sock, req, strlen(req), 0);

    /* Recebe tudo */
    size_t cap = 8192;
    size_t total = 0;
    char *buf = (char *)malloc(cap);
    ssize_t r;

    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while ((r = recv(sock, buf + total, cap - total - 1, 0)) > 0) {
        total += r;
        if (total + 1024 >= cap) {
            cap *= 2;
            buf = (char *)realloc(buf, cap);
        }
    }
    buf[total] = '\0';
    close(sock);

    JscValue *result = jsc_string(buf);
    free(buf);
    free(url);
    return result;
}

/* net.scan(host, inicio, fim) → array de portas abertas */
JscValue *net_scan(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 3) {
        runtime_error(state, "net.scan() espera 3 argumentos (host, inicio, fim)", (Node *)n);
        return NULL;
    }

    JscValue *h = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    JscValue *i_v = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(h); return NULL; }
    JscValue *f_v = eval(n->args.items[2], env, state);
    if (state->had_error) { jsc_value_free(h); jsc_value_free(i_v); return NULL; }

    char *host = strdup((h->type == VAL_STRING && h->as.s) ? h->as.s : "");
    int inicio = (int)jsc_value_to_int(i_v);
    int fim = (int)jsc_value_to_int(f_v);
    jsc_value_free(h);
    jsc_value_free(i_v);
    jsc_value_free(f_v);

    if (inicio < 1) inicio = 1;
    if (fim > 65535) fim = 65535;
    if (inicio > fim) { int t = inicio; inicio = fim; fim = t; }

    JscValue *arr = jsc_array();
    for (int porta = inicio; porta <= fim; porta++) {
        int s = net_connect_timeout(host, porta, 300);
        if (s >= 0) {
            close(s);
            jsc_array_push(arr, jsc_int(porta));
        }
    }
    free(host);
    return arr;
}

static JscValue *make_net_module(void) {
    JscValue *m = jsc_map();
    #define REG(name) jsc_map_set(m, jsc_string(#name), jsc_native(#name, net_##name))
    REG(resolve);
    REG(dns_lookup);
    REG(port_open);
    REG(connect);
    REG(send);
    REG(recv);
    REG(close);
    REG(http_get);
    REG(scan);
    REG(ip_publico);
    REG(traceroute);
    #undef REG
    return m;
}


 JscThread;

static JscThread *get_thread(JscValue *v) {
    return (JscThread *)v->as.thread.thread_ptr;
}

/* ============================================================
 * JSC$+ — Evaluator (Camada 4.c)
 * Adiciona if / while / for.
 * ============================================================ */

static void runtime_error(EvalState *state, const char *msg, Node *node) {
    /* Guarda a mensagem pra try/catch poder usar */
    snprintf(state->error_message, sizeof(state->error_message), "%s", msg);

    /* Só imprime se NÃO estiver dentro de um try */
    if (!state->in_try) {
        fprintf(stderr, "JSC$+ erro em [%d:%d]: %s\n",
                node->line, node->column, msg);
    }
    state->had_error = 1;
}

/* Helper: pega o JscValue SEM copiar.
 * Se for IDENT, busca direto no env (pra métodos modificarem o original).
 * Senão, avalia normal. */
static JscValue *eval_no_copy(Node *node, Env *env, EvalState *state) {
    if (node->type == NODE_IDENT) {
        NodeIdent *id = (NodeIdent *)node;
        return env_get(env, id->name);
    }
    return eval(node, env, state);
}

/* Helper: procura campo/metodo na classe (e nos pais) */
static int class_find_member(JscValue *klass, const char *name,
                             JscValue **out_field, NodeFuncDecl **out_method) {
    if (!klass || klass->type != VAL_CLASS) return 0;

    /* Procura nos campos */
    if (klass->as.cls.fields) {
        for (size_t i = 0; i < klass->as.cls.fields->count; i++) {
            Node *f = klass->as.cls.fields->items[i];
            if (f->type == NODE_VAR_DECL) {
                NodeVarDecl *vd = (NodeVarDecl *)f;
                if (strcmp(vd->var_name, name) == 0) {
                    if (out_field)  *out_field  = NULL;
                    if (out_method) *out_method = NULL;
                    return 1;
                }
            }
        }
    }

    /* Procura nos metodos */
    if (klass->as.cls.methods) {
        for (size_t i = 0; i < klass->as.cls.methods->count; i++) {
            Node *m = klass->as.cls.methods->items[i];
            if (m->type == NODE_FUNC_DECL) {
                NodeFuncDecl *fd = (NodeFuncDecl *)m;
                if (strcmp(fd->name, name) == 0) {
                    if (out_method) *out_method = fd;
                    return 1;
                }
            }
        }
    }

    /* Procura no pai */
    if (klass->as.cls.parent) {
        return class_find_member(klass->as.cls.parent, name, out_field, out_method);
    }

    return 0;
}

/* ------------------------------------------------------------
 * Entrada principal
 * ------------------------------------------------------------ */

JscValue *eval(Node *node, Env *env, EvalState *state) {
    if (!node || state->had_error) return NULL;
    if (state->has_return) return NULL;
    if (state->has_break) return NULL;
    if (state->has_continue) return NULL;

    switch (node->type) {
        case NODE_NUMBER:
        case NODE_STRING:
        case NODE_BOOL:
        case NODE_VAZIO:
            return eval_literal(node, env, state);

        case NODE_IDENT:
            return eval_ident(node, env, state);

        case NODE_BINARY:
            return eval_binary(node, env, state);
        case NODE_UNARY:
            return eval_unary(node, env, state);
        case NODE_TERNARY:
            return eval_ternary(node, env, state);

        case NODE_CALL:
            return eval_call(node, env, state);

        case NODE_ARRAY:
            return eval_array(node, env, state);
        case NODE_MAP:
            return eval_map(node, env, state);

        case NODE_INDEX:
            return eval_index(node, env, state);
        case NODE_FIELD:
            return eval_field(node, env, state);
        case NODE_ASSIGN:
            return eval_assign(node, env, state);

        case NODE_PROGRAM:
            return eval_program(node, env, state);
        case NODE_BLOCK:
            return eval_block(node, env, state);
        case NODE_PRINTJ:
            return eval_printj(node, env, state);
        case NODE_VAR_DECL:
            return eval_var_decl(node, env, state);

        /* Camada 4.c — controle de fluxo */
        case NODE_IF:
            return eval_if(node, env, state);
        case NODE_WHILE:
            return eval_while(node, env, state);
        case NODE_FOR:
            return eval_for(node, env, state);
        case NODE_FOR_EACH:
            return eval_for_each(node, env, state);

        case NODE_IMPORT:
            return eval_import(node, env, state);

        case NODE_FUNC_DECL:
            return eval_func_decl(node, env, state);
        case NODE_CLASS_DECL:
            return eval_class_decl(node, env, state);
        case NODE_RETURN:
            return eval_return(node, env, state);
        case NODE_TRY:
            return eval_try(node, env, state);
        case NODE_BREAK:
            return eval_break(node, env, state);
        case NODE_CONTINUE:
            return eval_continue(node, env, state);

        default:
            runtime_error(state, "nó ainda não implementado no evaluator", node);
            return NULL;
    }
}

/* ------------------------------------------------------------
 * Literais
 * ------------------------------------------------------------ */

JscValue *eval_literal(Node *node, Env *env, EvalState *state) {
    (void)env; (void)state;

    switch (node->type) {
        case NODE_NUMBER: {
            NodeNumber *n = (NodeNumber *)node;
            if (n->value == (long long)n->value) {
                return jsc_int((long long)n->value);
            }
            return jsc_float(n->value);
        }
        case NODE_STRING: {
            NodeString *n = (NodeString *)node;
            return jsc_string(n->value);
        }
        case NODE_BOOL: {
            NodeBool *n = (NodeBool *)node;
            return jsc_bool(n->value);
        }
        case NODE_VAZIO:
            return jsc_vazio();
        default:
            return NULL;
    }
}

/* ------------------------------------------------------------
 * Ident
 * ------------------------------------------------------------ */

JscValue *eval_ident(Node *node, Env *env, EvalState *state) {
    NodeIdent *n = (NodeIdent *)node;
    JscValue  *v = env_get(env, n->name);

    if (!v) {
        char buf[256];
        snprintf(buf, sizeof(buf), "variável '%s' não definida", n->name);
        runtime_error(state, buf, node);
        return NULL;
    }
    /* Devolve COPIA — assim quem consome pode liberar sem afetar o env */
    return jsc_value_copy(v);
}

/* ------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------ */

static int is_numeric(JscValue *v) {
    return v && (v->type == VAL_INT || v->type == VAL_FLOAT);
}

static JscValue *make_number(JscValue *l, JscValue *r, double result) {
    if (l->type == VAL_INT && r->type == VAL_INT) {
        if (result == (long long)result) {
            return jsc_int((long long)result);
        }
    }
    return jsc_float(result);
}

/* ------------------------------------------------------------
 * Binários
 * ------------------------------------------------------------ */

JscValue *eval_binary(Node *node, Env *env, EvalState *state) {
    NodeBinary *n = (NodeBinary *)node;

    JscValue *l = eval(n->left,  env, state);
    if (state->had_error) return NULL;

    JscValue *r = eval(n->right, env, state);
    if (state->had_error) {
        jsc_value_free(l);
        return NULL;
    }

    JscValue *result = NULL;

    /* Operadores bit-a-bit e comparações que precisam de int */
    if (n->op_code == OP_BIT_AND || n->op_code == OP_BIT_OR ||
        n->op_code == OP_BIT_XOR || n->op_code == OP_SHL ||
        n->op_code == OP_SHR) {
        if (l->type != VAL_INT || r->type != VAL_INT) {
            runtime_error(state, "operador bit-a-bit espera inteiros", node);
        } else {
            switch (n->op_code) {
                case OP_BIT_AND: result = jsc_int(l->as.i & r->as.i); break;
                case OP_BIT_OR:  result = jsc_int(l->as.i | r->as.i); break;
                case OP_BIT_XOR: result = jsc_int(l->as.i ^ r->as.i); break;
                case OP_SHL:     result = jsc_int(l->as.i << r->as.i); break;
                case OP_SHR:     result = jsc_int(l->as.i >> r->as.i); break;
                default: break;
            }
        }
        jsc_value_free(l);
        jsc_value_free(r);
        return result ? result : jsc_vazio();
    }

    /* Arimética e concatenação de string */
    switch (n->op_code) {
        case OP_ADD: {
            /* String + String = concatena */
            if (l->type == VAL_STRING && r->type == VAL_STRING) {
                size_t len_l = strlen(l->as.s ? l->as.s : "");
                size_t len_r = strlen(r->as.s ? r->as.s : "");
                char  *buf   = (char *)malloc(len_l + len_r + 1);
                memcpy(buf, l->as.s ? l->as.s : "", len_l);
                memcpy(buf + len_l, r->as.s ? r->as.s : "", len_r);
                buf[len_l + len_r] = '\0';
                result = jsc_string(buf);
                free(buf);
            }
            /* Int + Int = int */
            else if (l->type == VAL_INT && r->type == VAL_INT) {
                result = jsc_int(l->as.i + r->as.i);
            }
            /* Numérico misto = float */
            else if ((l->type == VAL_INT || l->type == VAL_FLOAT) &&
                     (r->type == VAL_INT || r->type == VAL_FLOAT)) {
                result = jsc_float(jsc_value_to_float(l) + jsc_value_to_float(r));
            } else {
                runtime_error(state, "'+' espera numeros ou strings", node);
            }
            break;
        }
        case OP_SUB: {
            if (l->type == VAL_INT && r->type == VAL_INT) {
                result = jsc_int(l->as.i - r->as.i);
            } else if ((l->type == VAL_INT || l->type == VAL_FLOAT) &&
                       (r->type == VAL_INT || r->type == VAL_FLOAT)) {
                result = jsc_float(jsc_value_to_float(l) - jsc_value_to_float(r));
            } else {
                runtime_error(state, "'-' espera numeros", node);
            }
            break;
        }
        case OP_MUL: {
            /* String * Int = repete */
            if (l->type == VAL_STRING && r->type == VAL_INT) {
                long long times = r->as.i;
                if (times < 0) times = 0;
                size_t len = strlen(l->as.s ? l->as.s : "");
                char  *buf = (char *)malloc(len * times + 1);
                for (long long k = 0; k < times; k++) {
                    memcpy(buf + k * len, l->as.s ? l->as.s : "", len);
                }
                buf[len * times] = '\0';
                result = jsc_string(buf);
                free(buf);
            } else if (l->type == VAL_INT && r->type == VAL_INT) {
                result = jsc_int(l->as.i * r->as.i);
            } else if ((l->type == VAL_INT || l->type == VAL_FLOAT) &&
                       (r->type == VAL_INT || r->type == VAL_FLOAT)) {
                result = jsc_float(jsc_value_to_float(l) * jsc_value_to_float(r));
            } else {
                runtime_error(state, "'*' espera numeros", node);
            }
            break;
        }
        case OP_DIV: {
            if ((l->type == VAL_INT || l->type == VAL_FLOAT) &&
                (r->type == VAL_INT || r->type == VAL_FLOAT)) {
                double b = jsc_value_to_float(r);
                if (b == 0.0) runtime_error(state, "divisao por zero", node);
                else result = jsc_float(jsc_value_to_float(l) / b);
            } else {
                runtime_error(state, "'/' espera numeros", node);
            }
            break;
        }
        case OP_MOD: {
            if (l->type == VAL_INT && r->type == VAL_INT) {
                if (r->as.i == 0) runtime_error(state, "modulo por zero", node);
                else result = jsc_int(l->as.i % r->as.i);
            } else if ((l->type == VAL_INT || l->type == VAL_FLOAT) &&
                       (r->type == VAL_INT || r->type == VAL_FLOAT)) {
                double b = jsc_value_to_float(r);
                if (b == 0.0) runtime_error(state, "modulo por zero", node);
                else {
                    double a = jsc_value_to_float(l);
                    result = jsc_float(a - (long long)(a / b) * b);
                }
            } else {
                runtime_error(state, "'%' espera numeros", node);
            }
            break;
        }
        case OP_POW: {
            if ((l->type == VAL_INT || l->type == VAL_FLOAT) &&
                (r->type == VAL_INT || r->type == VAL_FLOAT)) {
                if (l->type == VAL_INT && r->type == VAL_INT && r->as.i >= 0) {
                    long long base = l->as.i, exp = r->as.i, acc = 1;
                    for (long long k = 0; k < exp; k++) acc *= base;
                    result = jsc_int(acc);
                } else {
                    extern double pow(double, double);
                    result = jsc_float(pow(jsc_value_to_float(l), jsc_value_to_float(r)));
                }
            } else {
                runtime_error(state, "'**' espera numeros", node);
            }
            break;
        }
        case OP_EQ:  result = jsc_bool(jsc_value_equals(l, r)); break;
        case OP_NEQ: result = jsc_bool(!jsc_value_equals(l, r)); break;
        case OP_LT: case OP_GT: case OP_LTE: case OP_GTE: {
            if ((l->type == VAL_INT || l->type == VAL_FLOAT) &&
                (r->type == VAL_INT || r->type == VAL_FLOAT)) {
                double a = jsc_value_to_float(l);
                double b = jsc_value_to_float(r);
                if      (n->op_code == OP_LT)  result = jsc_bool(a <  b);
                else if (n->op_code == OP_GT)  result = jsc_bool(a >  b);
                else if (n->op_code == OP_LTE) result = jsc_bool(a <= b);
                else                            result = jsc_bool(a >= b);
            } else if (l->type == VAL_STRING && r->type == VAL_STRING) {
                int cmp = strcmp(l->as.s ? l->as.s : "", r->as.s ? r->as.s : "");
                if      (n->op_code == OP_LT)  result = jsc_bool(cmp <  0);
                else if (n->op_code == OP_GT)  result = jsc_bool(cmp >  0);
                else if (n->op_code == OP_LTE) result = jsc_bool(cmp <= 0);
                else                            result = jsc_bool(cmp >= 0);
            } else {
                runtime_error(state, "comparacao espera numeros ou strings", node);
            }
            break;
        }
        case OP_AND: result = jsc_value_is_truthy(l) ? r : l; break;
        case OP_OR:  result = jsc_value_is_truthy(l) ? l : r; break;
        default: {
            char buf[128];
            snprintf(buf, sizeof(buf), "operador binario desconhecido: '%s'", n->op ? n->op : "?");
            runtime_error(state, buf, node);
            break;
        }
    }

    if (result == l) {
        jsc_value_free(r);
    } else if (result == r) {
        jsc_value_free(l);
    } else {
        jsc_value_free(l);
        jsc_value_free(r);
    }

    return result ? result : jsc_vazio();
}

/* ------------------------------------------------------------
 * Unário
 * ------------------------------------------------------------ */

JscValue *eval_unary(Node *node, Env *env, EvalState *state) {
    NodeUnary *n = (NodeUnary *)node;

    JscValue *operand = eval(n->operand, env, state);
    if (state->had_error) return NULL;

    const char *op = n->op;
    JscValue   *result = NULL;

    if (strcmp(op, "-") == 0) {
        if (operand->type == VAL_INT)        result = jsc_int(-operand->as.i);
        else if (operand->type == VAL_FLOAT) result = jsc_float(-operand->as.f);
        else runtime_error(state, "operador unário '-' espera número", node);
    }
    else if (strcmp(op, "n+") == 0) {
        result = jsc_bool(!jsc_value_is_truthy(operand));
    }
    else if (strcmp(op, "~") == 0) {
        if (operand->type == VAL_INT) result = jsc_int(~operand->as.i);
        else runtime_error(state, "operador '~' espera inteiro", node);
    }
    else {
        char buf[128];
        snprintf(buf, sizeof(buf), "operador unário desconhecido: '%s'", op);
        runtime_error(state, buf, node);
    }

    jsc_value_free(operand);
    return result ? result : jsc_vazio();
}

/* ------------------------------------------------------------
 * Ternário
 * ------------------------------------------------------------ */

JscValue *eval_ternary(Node *node, Env *env, EvalState *state) {
    NodeTernary *n = (NodeTernary *)node;

    JscValue *cond = eval(n->cond, env, state);
    if (state->had_error) return NULL;

    int truthy = jsc_value_is_truthy(cond);
    jsc_value_free(cond);

    if (truthy) return eval(n->then_expr, env, state);
    else        return eval(n->else_expr, env, state);
}

/* ------------------------------------------------------------
 * Programa
 * ------------------------------------------------------------ */

JscValue *eval_program(Node *node, Env *env, EvalState *state) {
    NodeProgram *prog = (NodeProgram *)node;

    for (size_t i = 0; i < prog->statements.count; i++) {
        eval(prog->statements.items[i], env, state);
        if (state->had_error) break;
        if (state->has_return) break;
        gc_maybe_collect();
    }
    return jsc_vazio();
}

/* ------------------------------------------------------------
 * Bloco
 * ------------------------------------------------------------ */

JscValue *eval_block(Node *node, Env *env, EvalState *state) {
    NodeBlock *blk = (NodeBlock *)node;

    for (size_t i = 0; i < blk->statements.count; i++) {
        eval(blk->statements.items[i], env, state);
        if (state->had_error) break;
        if (state->has_return) break;
        if (state->has_break) break;
        if (state->has_continue) break;
    }
    return jsc_vazio();
}

/* ------------------------------------------------------------
 * printj
 * ------------------------------------------------------------ */

JscValue *eval_printj(Node *node, Env *env, EvalState *state) {
    NodePrintj *n = (NodePrintj *)node;

    JscValue *v = eval(n->expr, env, state);
    if (state->had_error) return NULL;

    jsc_print_value(v);
    printf("\n");

    jsc_value_free(v);
    return jsc_vazio();
}

/* ------------------------------------------------------------
 * Declaração de variável
 * ------------------------------------------------------------ */

JscValue *eval_var_decl(Node *node, Env *env, EvalState *state) {
    NodeVarDecl *n = (NodeVarDecl *)node;

    JscValue *v = NULL;
    if (n->value) {
        v = eval(n->value, env, state);
        if (state->had_error) return NULL;
    } else {
        v = jsc_vazio();
    }

    /* Verifica tipo declarado (se houver) */
    if (n->type_name && v) {
        const char *tipo = n->type_name;
        int ok = 1;

        if (strcmp(tipo, "int") == 0) {
            ok = (v->type == VAL_INT);
        } else if (strcmp(tipo, "float") == 0) {
            /* int aceito como float */
            if (v->type == VAL_INT) {
                /* Converte pra float */
                JscValue *f = jsc_float((double)v->as.i);
                jsc_value_free(v);
                v = f;
                ok = 1;
            } else {
                ok = (v->type == VAL_FLOAT);
            }
        } else if (strcmp(tipo, "string") == 0) {
            ok = (v->type == VAL_STRING);
        } else if (strcmp(tipo, "bool") == 0) {
            ok = (v->type == VAL_BOOL);
        } else if (strcmp(tipo, "vazio") == 0) {
            ok = (v->type == VAL_VAZIO);
        } else if (strcmp(tipo, "array") == 0) {
            ok = (v->type == VAL_ARRAY);
        } else if (strcmp(tipo, "map") == 0) {
            ok = (v->type == VAL_MAP);
        } else {
            /* Tipo desconhecido — só avisa */
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "tipo desconhecido '%s' (use: int, float, string, bool, vazio, array, map)",
                     tipo);
            runtime_error(state, buf, node);
            jsc_value_free(v);
            return NULL;
        }

        if (!ok) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "variavel '%s' declarada como '%s' recebeu '%s'",
                     n->var_name, tipo, jsc_type_name(v->type));
            runtime_error(state, buf, node);
            jsc_value_free(v);
            return NULL;
        }
    }

    env_define(env, n->var_name, v);
    return jsc_vazio();
}

/* ============================================================
 * CAMADA 4.c — CONTROLE DE FLUXO
 * ============================================================ */

/* ------------------------------------------------------------
 * if cond { } else { }
 * ------------------------------------------------------------ */

JscValue *eval_if(Node *node, Env *env, EvalState *state) {
    NodeIf *n = (NodeIf *)node;

    JscValue *cond = eval(n->cond, env, state);
    if (state->had_error) return NULL;

    int truthy = jsc_value_is_truthy(cond);
    jsc_value_free(cond);

    if (truthy) {
        eval(n->then_block, env, state);
    } else if (n->else_block) {
        eval(n->else_block, env, state);
    }

    return jsc_vazio();
}

/* ------------------------------------------------------------
 * while cond { }
 * ------------------------------------------------------------ */

JscValue *eval_while(Node *node, Env *env, EvalState *state) {
    NodeWhile *n = (NodeWhile *)node;

    while (1) {
        JscValue *cond = eval(n->cond, env, state);
        if (state->had_error) return NULL;

        int truthy = jsc_value_is_truthy(cond);
        jsc_value_free(cond);

        if (!truthy) break;

        state->has_break = 0;
        state->has_continue = 0;

        eval(n->body, env, state);
        if (state->had_error) break;
        if (state->has_return) break;
        if (state->has_break) { state->has_break = 0; break; }
        if (state->has_continue) { state->has_continue = 0; continue; }
    }

    return jsc_vazio();
}

/* ------------------------------------------------------------
 * for i in start..end { }
 * ------------------------------------------------------------ */

JscValue *eval_for(Node *node, Env *env, EvalState *state) {
    NodeFor *n = (NodeFor *)node;

    JscValue *start_v = eval(n->start, env, state);
    if (state->had_error) return NULL;

    JscValue *end_v = eval(n->end, env, state);
    if (state->had_error) {
        jsc_value_free(start_v);
        return NULL;
    }

    long long start = jsc_value_to_int(start_v);
    long long end   = jsc_value_to_int(end_v);
    jsc_value_free(start_v);
    jsc_value_free(end_v);

    /* Cria escopo próprio pro for */
    Env *loop_env = env_new(env);

    for (long long i = start; i <= end; i++) {
        env_define(loop_env, n->var_name, jsc_int(i));

        state->has_break = 0;
        state->has_continue = 0;

        eval(n->body, loop_env, state);
        if (state->had_error) break;
        if (state->has_return) break;
        if (state->has_break) { state->has_break = 0; break; }
        if (state->has_continue) { state->has_continue = 0; continue; }
    }

    env_free(loop_env);
    return jsc_vazio();
}

/* ============================================================
 * CAMADA 4.d - FUNCOES
 * ============================================================ */

JscValue *eval_func_decl(Node *node, Env *env, EvalState *state) {
    (void)state;
    NodeFuncDecl *n = (NodeFuncDecl *)node;

    JscValue *fn = jsc_vazio();
    fn->type = VAL_FUNCTION;
    fn->as.func.name    = strdup(n->name);
    fn->as.func.body    = n->body;
    fn->as.func.params  = &n->params;
    fn->as.func.closure = env;

    env_define(env, n->name, fn);
    return jsc_vazio();
}

JscValue *eval_call(Node *node, Env *env, EvalState *state);

JscValue *eval_call(Node *node, Env *env, EvalState *state) {
    NodeCall *n = (NodeCall *)node;

    /* Se o callee for FIELD (obj.metodo), é chamada de método */
    if (n->callee->type == NODE_FIELD) {
        return eval_method_call(n, env, state);
    }

    JscValue *callee = eval(n->callee, env, state);
    if (state->had_error) return NULL;

    /* Se o callee é uma CLASSE, é instanciação */
    if (callee->type == VAL_CLASS) {
        JscValue *inst = jsc_instance(callee);
        inst->as.inst.fields = (Env *)env_new(NULL);

        /* Procura init() na classe e nos pais */
        NodeFuncDecl *init_fd = NULL;
        JscValue *init_field = NULL;
        class_find_member(callee, "init", &init_field, &init_fd);
        Node *init_method = (Node *)init_fd;

        if (init_method) {
            NodeFuncDecl *fd = (NodeFuncDecl *)init_method;

            size_t n_args = n->args.count;
            JscValue **args = (JscValue **)malloc(sizeof(JscValue *) * (n_args ? n_args : 1));
            for (size_t i = 0; i < n_args; i++) {
                args[i] = eval(n->args.items[i], env, state);
                if (state->had_error) {
                    for (size_t j = 0; j < i; j++) jsc_value_free(args[j]);
                    free(args);
                    jsc_value_free(callee);
                    return NULL;
                }
            }

            size_t n_params = fd->params.count;
            if (n_args != n_params) {
                runtime_error(state, "construtor init() com numero errado de argumentos", node);
                for (size_t i = 0; i < n_args; i++) jsc_value_free(args[i]);
                free(args);
                jsc_value_free(callee);
                return NULL;
            }

            Env *call_env = env_new(callee->as.cls.closure);
            /* Guarda o PONTEIRO ORIGINAL — não cópia.
             * Antes de liberar call_env, a gente remove o `this`. */
            env_define(call_env, "this", inst);

            for (size_t i = 0; i < n_params; i++) {
                Node *param = fd->params.items[i];
                NodeIdent *pi = (NodeIdent *)param;
                env_define(call_env, pi->name, args[i]);
            }
            free(args);

            eval(fd->body, call_env, state);

            /* Remove o `this` do env antes de liberar
             * (senão o env_free libera a instância que vamos devolver) */
            for (size_t i = 0; i < call_env->count; i++) {
                if (strcmp(call_env->entries[i].name, "this") == 0) {
                    call_env->entries[i].value = NULL;  /* não liberar */
                    break;
                }
            }
            env_free(call_env);
        }

        jsc_value_free(callee);
        return inst;
    }

    if (callee->type != VAL_FUNCTION) {
        runtime_error(state, "tentativa de chamar algo que nao e funcao", node);
        jsc_value_free(callee);
        return NULL;
    }

    /* Se é função nativa, chama direto */
    if (callee->as.func.native != NULL) {
        JscValue *result = callee->as.func.native((void *)n, (void *)env, (void *)state);
        jsc_value_free(callee);
        return result ? result : jsc_vazio();
    }

    size_t n_args = n->args.count;
    JscValue **args = (JscValue **)malloc(sizeof(JscValue *) * (n_args ? n_args : 1));
    for (size_t i = 0; i < n_args; i++) {
        args[i] = eval(n->args.items[i], env, state);
        if (state->had_error) {
            for (size_t j = 0; j < i; j++) jsc_value_free(args[j]);
            free(args);
            jsc_value_free(callee);
            return NULL;
        }
    }

    size_t n_params = callee->as.func.params->count;
    if (n_args != n_params) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "funcao '%s' espera %zu argumentos, recebeu %zu",
                 callee->as.func.name, n_params, n_args);
        runtime_error(state, buf, node);
        for (size_t i = 0; i < n_args; i++) jsc_value_free(args[i]);
        free(args);
        jsc_value_free(callee);
        return NULL;
    }

    Env *call_env = env_new(callee->as.func.closure);

    for (size_t i = 0; i < n_params; i++) {
        Node *param = callee->as.func.params->items[i];
        NodeIdent *pi = (NodeIdent *)param;
        env_define(call_env, pi->name, args[i]);
    }
    free(args);

    int saved_has_return = state->has_return;
    JscValue *saved_return_value = state->return_value;
    state->has_return = 0;
    state->return_value = NULL;

    eval(callee->as.func.body, call_env, state);

    JscValue *result = NULL;
    if (state->has_return && state->return_value) {
        result = state->return_value;
    } else {
        result = jsc_vazio();
    }

    state->has_return = saved_has_return;
    state->return_value = saved_return_value;

    env_free(call_env);
    jsc_value_free(callee);

    return result;
}

JscValue *eval_return(Node *node, Env *env, EvalState *state) {
    NodeReturn *n = (NodeReturn *)node;

    JscValue *v = NULL;
    if (n->expr) {
        v = eval(n->expr, env, state);
        if (state->had_error) return NULL;
    } else {
        v = jsc_vazio();
    }

    state->has_return = 1;
    if (state->return_value) {
        jsc_value_free(state->return_value);
    }
    state->return_value = v;

    return jsc_vazio();
}

/* ============================================================
 * BLOCO B.1 — Arrays e Maps (literais)
 * ============================================================ */

JscValue *eval_array(Node *node, Env *env, EvalState *state) {
    NodeArray *n = (NodeArray *)node;
    JscValue  *arr = jsc_array();

    for (size_t i = 0; i < n->items.count; i++) {
        JscValue *item = eval(n->items.items[i], env, state);
        if (state->had_error) {
            jsc_value_free(arr);
            return NULL;
        }
        jsc_array_push(arr, item);
    }
    return arr;
}

JscValue *eval_map(Node *node, Env *env, EvalState *state) {
    NodeMap *n = (NodeMap *)node;
    JscValue *map = jsc_map();

    for (size_t i = 0; i < n->keys.count; i++) {
        JscValue *key   = eval(n->keys.items[i], env, state);
        if (state->had_error) { jsc_value_free(map); return NULL; }

        JscValue *value = eval(n->values.items[i], env, state);
        if (state->had_error) {
            jsc_value_free(key);
            jsc_value_free(map);
            return NULL;
        }
        jsc_map_set(map, key, value);
    }
    return map;
}

/* ============================================================
 * BLOCO B.2 — Acesso e escrita por indice
 * ============================================================ */

JscValue *eval_index(Node *node, Env *env, EvalState *state) {
    NodeIndex *n = (NodeIndex *)node;

    JscValue *obj = eval(n->object, env, state);
    if (state->had_error) return NULL;

    JscValue *idx = eval(n->index, env, state);
    if (state->had_error) {
        jsc_value_free(obj);
        return NULL;
    }

    JscValue *result = NULL;

    if (obj->type == VAL_ARRAY) {
        long long i = jsc_value_to_int(idx);
        if (i < 0 || (size_t)i >= jsc_array_len(obj)) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "indice %lld fora do array (tamanho %zu)",
                     i, jsc_array_len(obj));
            runtime_error(state, buf, node);
        } else {
            result = jsc_value_copy(jsc_array_get(obj, (size_t)i));
        }
    }
    else if (obj->type == VAL_MAP) {
        JscValue *val = jsc_map_get(obj, idx);
        if (val) {
            result = jsc_value_copy(val);
        } else {
            result = jsc_vazio();
        }
    }
    else if (obj->type == VAL_STRING) {
        long long i = jsc_value_to_int(idx);
        const char *s = obj->as.s ? obj->as.s : "";
        long long len = (long long)strlen(s);
        if (i < 0 || i >= len) {
            runtime_error(state, "indice fora da string", node);
        } else {
            char buf[2] = { s[i], '\0' };
            result = jsc_string(buf);
        }
    }
    else {
        runtime_error(state, "so array, map ou string podem ser indexados", node);
    }

    jsc_value_free(obj);
    jsc_value_free(idx);

    return result ? result : jsc_vazio();
}

JscValue *eval_assign(Node *node, Env *env, EvalState *state) {
    NodeAssign *n = (NodeAssign *)node;

    /* Target é IDENT — atribuição simples ou composta */
    if (n->target->type == NODE_IDENT) {
        NodeIdent *id = (NodeIdent *)n->target;

        JscValue *value = eval(n->value, env, state);
        if (state->had_error) return NULL;

        if (strcmp(n->op, "=") != 0) {
            JscValue *atual = env_get(env, id->name);
            if (!atual) {
                runtime_error(state, "variavel nao definida pra atribuicao composta", node);
                jsc_value_free(value);
                return NULL;
            }

            double a = jsc_value_to_float(atual);
            double b = jsc_value_to_float(value);
            double r = 0.0;

            if      (strcmp(n->op, "+=") == 0) r = a + b;
            else if (strcmp(n->op, "-=") == 0) r = a - b;
            else if (strcmp(n->op, "*=") == 0) r = a * b;
            else if (strcmp(n->op, "/=") == 0) {
                if (b == 0) { runtime_error(state, "divisao por zero", node); jsc_value_free(value); return NULL; }
                r = a / b;
            }
            else if (strcmp(n->op, "%=") == 0) {
                if (b == 0) { runtime_error(state, "modulo por zero", node); jsc_value_free(value); return NULL; }
                r = (double)((long long)a % (long long)b);
            }

            jsc_value_free(value);

            if (atual->type == VAL_INT && r == (long long)r) {
                value = jsc_int((long long)r);
            } else {
                value = jsc_float(r);
            }
        }

        env_define(env, id->name, value);
        return jsc_vazio();
    }

    /* Target é INDEX */
    if (n->target->type == NODE_INDEX) {
        NodeIndex *idx_node = (NodeIndex *)n->target;

        /* ATENÇÃO: pega o objeto SEM COPIAR, pra mexer no original */
        JscValue *obj = eval_no_copy(idx_node->object, env, state);
        if (state->had_error) return NULL;

        if (!obj) {
            runtime_error(state, "objeto nao existe pra atribuicao", node);
            return NULL;
        }

        JscValue *idx = eval(idx_node->index, env, state);
        if (state->had_error) return NULL;

        JscValue *value = eval(n->value, env, state);
        if (state->had_error) { jsc_value_free(idx); return NULL; }

        /* obj NÃO é cópia — é o valor original no env.
         * Não pode ser liberado no final. */

        if (obj->type == VAL_ARRAY) {
            long long i = jsc_value_to_int(idx);
            if (i < 0 || (size_t)i >= jsc_array_len(obj)) {
                runtime_error(state, "indice fora do array na atribuicao", node);
                jsc_value_free(value);
            } else {
                jsc_value_free(obj->as.array.items[i]);
                obj->as.array.items[i] = value;
            }
            jsc_value_free(idx);
        }
        else if (obj->type == VAL_MAP) {
            /* jsc_map_set toma posse do idx E do value — nao liberar */
            jsc_map_set(obj, idx, value);
        }
        else {
            runtime_error(state, "so array e map podem receber atribuicao por indice", node);
            jsc_value_free(value);
            jsc_value_free(idx);
        }

        return jsc_vazio();
    }

    /* Target é FIELD: this.nome = valor, pessoa.idade = 19 */
    if (n->target->type == NODE_FIELD) {
        NodeField *fld = (NodeField *)n->target;

        /* IMPORTANTE: usa eval_no_copy pra pegar o objeto ORIGINAL.
         * Se fosse `eval`, o `this` viria copiado e o `fields` seria perdido. */
        JscValue *obj = eval_no_copy(fld->object, env, state);
        if (state->had_error) return NULL;

        if (!obj) {
            runtime_error(state, "objeto nao existe pra atribuicao de campo", node);
            return NULL;
        }

        JscValue *value = eval(n->value, env, state);
        if (state->had_error) { jsc_value_free(obj); return NULL; }

        /* Se for instância, guarda no env de campos */
        if (obj->type == VAL_INSTANCE) {
            Env *fields = (Env *)obj->as.inst.fields;
            if (!fields) {
                obj->as.inst.fields = (Env *)env_new(NULL);
                fields = (Env *)obj->as.inst.fields;
            }
            env_define(fields, fld->field, value);
        }
        /* Se for map, guarda direto */
        else if (obj->type == VAL_MAP) {
            JscValue *key = jsc_string(fld->field);
            jsc_map_set(obj, key, value);
        }
        else {
            runtime_error(state, "atribuicao de campo so funciona em instancia ou map", node);
            jsc_value_free(value);
        }

        /* NÃO libera obj — veio do eval_no_copy (pode ser do env) */
        return jsc_vazio();
    }

    runtime_error(state, "target de atribuicao invalido", node);
    return NULL;
}

/* ============================================================
 * BLOCO B.3.a — Chamada de metodos (obj.metodo())
 * ============================================================ */

JscValue *eval_method_call(NodeCall *n, Env *env, EvalState *state) {
    NodeField *field = (NodeField *)n->callee;

    JscValue *obj = eval_no_copy(field->object, env, state);
    if (state->had_error) return NULL;

    const char *metodo = field->field;
    JscValue   *result = NULL;

    /* ============================================================
     * PRIORIDADE 1: MODULOS (math, fs, etc)
     * obj é um map que contém funções nativas.
     * ============================================================ */
    if (obj->type == VAL_MAP) {
        JscValue key;
        key.type = VAL_STRING;
        key.as.s = (char *)metodo;

        JscValue *fn = jsc_map_get(obj, &key);
        if (fn && fn->type == VAL_FUNCTION && fn->as.func.native != NULL) {
            result = fn->as.func.native((void *)n, (void *)env, (void *)state);
            return result ? result : jsc_vazio();
        }
    }

    /* ============================================================
     * PRIORIDADE 2: INSTANCIA (objetos de classe)
     * ============================================================ */
    if (obj->type == VAL_INSTANCE) {
        JscValue *klass = (JscValue *)obj->as.inst.klass;
        if (!klass || klass->type != VAL_CLASS) {
            runtime_error(state, "instancia sem classe", (Node *)n);
        } else {
            NodeFuncDecl *found = NULL;
            JscValue *found_field = NULL;
            class_find_member(klass, metodo, &found_field, &found);

            if (!found) {
                char buf[128];
                snprintf(buf, sizeof(buf), "metodo '%s' nao existe", metodo);
                runtime_error(state, buf, (Node *)n);
            } else {
                size_t n_args = n->args.count;
                JscValue **args = (JscValue **)malloc(sizeof(JscValue *) * (n_args ? n_args : 1));
                for (size_t i = 0; i < n_args; i++) {
                    args[i] = eval(n->args.items[i], env, state);
                    if (state->had_error) {
                        for (size_t j = 0; j < i; j++) jsc_value_free(args[j]);
                        free(args);
                        return NULL;
                    }
                }

                size_t n_params = found->params.count;
                if (n_args != n_params) {
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                             "metodo '%s' espera %zu args, recebeu %zu",
                             metodo, n_params, n_args);
                    runtime_error(state, buf, (Node *)n);
                    for (size_t i = 0; i < n_args; i++) jsc_value_free(args[i]);
                    free(args);
                    return NULL;
                }

                Env *method_env = env_new(klass->as.cls.closure);
                env_define(method_env, "this", obj);

                for (size_t i = 0; i < n_params; i++) {
                    Node *param = found->params.items[i];
                    NodeIdent *pi = (NodeIdent *)param;
                    env_define(method_env, pi->name, args[i]);
                }
                free(args);

                int saved_has_return = state->has_return;
                JscValue *saved_return_value = state->return_value;
                state->has_return = 0;
                state->return_value = NULL;

                eval(found->body, method_env, state);

                JscValue *method_result = NULL;
                if (state->has_return && state->return_value) {
                    method_result = state->return_value;
                } else {
                    method_result = jsc_vazio();
                }

                state->has_return = saved_has_return;
                state->return_value = saved_return_value;

                /* Remove this antes de liberar */
                for (size_t i = 0; i < method_env->count; i++) {
                    if (strcmp(method_env->entries[i].name, "this") == 0) {
                        method_env->entries[i].value = NULL;
                        break;
                    }
                }
                env_free(method_env);
                result = method_result;
            }
        }
        return result ? result : jsc_vazio();
    }

    /* ============================================================
     * PRIORIDADE 3: METODOS NATIVOS (len, push, pop, etc)
     * ============================================================ */

    if (strcmp(metodo, "len") == 0) {
        if (n->args.count != 0) {
            runtime_error(state, "len() nao aceita argumentos", (Node *)n);
        } else {
            long long tamanho = 0;
            if (obj->type == VAL_ARRAY)      tamanho = (long long)jsc_array_len(obj);
            else if (obj->type == VAL_MAP)   tamanho = (long long)obj->as.map.count;
            else if (obj->type == VAL_STRING) tamanho = (long long)strlen(obj->as.s ? obj->as.s : "");
            else {
                runtime_error(state, "len() so funciona em array, map ou string", (Node *)n);
            }
            if (!state->had_error) result = jsc_int(tamanho);
        }
    }

    else if (strcmp(metodo, "push") == 0 && obj->type == VAL_ARRAY) {
        if (n->args.count != 1) {
            runtime_error(state, "push() espera 1 argumento", (Node *)n);
        } else {
            JscValue *val = eval(n->args.items[0], env, state);
            if (!state->had_error) {
                jsc_array_push(obj, val);
                result = jsc_vazio();
            }
        }
    }

    else if (strcmp(metodo, "pop") == 0 && obj->type == VAL_ARRAY) {
        if (obj->as.array.count == 0) {
            runtime_error(state, "pop() em array vazio", (Node *)n);
        } else {
            size_t last = obj->as.array.count - 1;
            JscValue *val = obj->as.array.items[last];
            obj->as.array.count--;
            result = val;
        }
    }

    else if (strcmp(metodo, "first") == 0 && obj->type == VAL_ARRAY) {
        if (obj->as.array.count == 0) result = jsc_vazio();
        else result = jsc_value_copy(obj->as.array.items[0]);
    }

    else if (strcmp(metodo, "last") == 0 && obj->type == VAL_ARRAY) {
        if (obj->as.array.count == 0) result = jsc_vazio();
        else result = jsc_value_copy(obj->as.array.items[obj->as.array.count - 1]);
    }

    else if (strcmp(metodo, "contains") == 0 && obj->type == VAL_ARRAY) {
        if (n->args.count != 1) {
            runtime_error(state, "contains() espera 1 argumento", (Node *)n);
        } else {
            JscValue *val = eval(n->args.items[0], env, state);
            if (!state->had_error) {
                int achou = 0;
                for (size_t i = 0; i < obj->as.array.count; i++) {
                    if (jsc_value_equals(obj->as.array.items[i], val)) { achou = 1; break; }
                }
                result = jsc_bool(achou);
                jsc_value_free(val);
            }
        }
    }

    else if (strcmp(metodo, "contains") == 0 && obj->type == VAL_STRING) {
        if (n->args.count != 1) {
            runtime_error(state, "contains() espera 1 argumento", (Node *)n);
        } else {
            JscValue *sub = eval(n->args.items[0], env, state);
            if (!state->had_error) {
                const char *s = obj->as.s ? obj->as.s : "";
                const char *p = (sub->type == VAL_STRING && sub->as.s) ? sub->as.s : "";
                result = jsc_bool(strstr(s, p) != NULL);
                jsc_value_free(sub);
            }
        }
    }

    else if (strcmp(metodo, "clear") == 0 && obj->type == VAL_ARRAY) {
        for (size_t i = 0; i < obj->as.array.count; i++) jsc_value_free(obj->as.array.items[i]);
        obj->as.array.count = 0;
        result = jsc_vazio();
    }

    else if (strcmp(metodo, "clear") == 0 && obj->type == VAL_MAP) {
        for (size_t i = 0; i < obj->as.map.count; i++) {
            jsc_value_free(obj->as.map.entries[i].key);
            jsc_value_free(obj->as.map.entries[i].value);
        }
        obj->as.map.count = 0;
        result = jsc_vazio();
    }

    else if (strcmp(metodo, "reverse") == 0 && obj->type == VAL_ARRAY) {
        size_t n_items = obj->as.array.count;
        for (size_t i = 0; i < n_items / 2; i++) {
            JscValue *tmp = obj->as.array.items[i];
            obj->as.array.items[i] = obj->as.array.items[n_items - 1 - i];
            obj->as.array.items[n_items - 1 - i] = tmp;
        }
        result = jsc_vazio();
    }

    else if (strcmp(metodo, "join") == 0 && obj->type == VAL_ARRAY) {
        if (n->args.count != 1) {
            runtime_error(state, "join() espera 1 argumento", (Node *)n);
        } else {
            JscValue *sep_val = eval(n->args.items[0], env, state);
            if (!state->had_error) {
                const char *sep = (sep_val->type == VAL_STRING && sep_val->as.s) ? sep_val->as.s : "";
                size_t sep_len = strlen(sep);
                size_t total = 0;
                for (size_t i = 0; i < obj->as.array.count; i++) {
                    JscValue *item = obj->as.array.items[i];
                    char b[64];
                    if (item->type == VAL_STRING) total += strlen(item->as.s ? item->as.s : "");
                    else if (item->type == VAL_INT) { snprintf(b, sizeof(b), "%lld", item->as.i); total += strlen(b); }
                    else if (item->type == VAL_FLOAT) { snprintf(b, sizeof(b), "%g", item->as.f); total += strlen(b); }
                    else if (item->type == VAL_BOOL) total += item->as.b ? 4 : 5;
                    else total += 8;
                    if (i < obj->as.array.count - 1) total += sep_len;
                }
                char *out = (char *)malloc(total + 1);
                out[0] = '\0';
                for (size_t i = 0; i < obj->as.array.count; i++) {
                    if (i > 0) strcat(out, sep);
                    JscValue *item = obj->as.array.items[i];
                    char b[64];
                    if (item->type == VAL_STRING) strcat(out, item->as.s ? item->as.s : "");
                    else if (item->type == VAL_INT) { snprintf(b, sizeof(b), "%lld", item->as.i); strcat(out, b); }
                    else if (item->type == VAL_FLOAT) { snprintf(b, sizeof(b), "%g", item->as.f); strcat(out, b); }
                    else if (item->type == VAL_BOOL) strcat(out, item->as.b ? "true" : "false");
                    else strcat(out, "<valor>");
                }
                result = jsc_string(out);
                free(out);
                jsc_value_free(sep_val);
            }
        }
    }

    /* MAP: keys */
    else if (strcmp(metodo, "keys") == 0 && obj->type == VAL_MAP) {
        JscValue *arr = jsc_array();
        for (size_t i = 0; i < obj->as.map.count; i++)
            jsc_array_push(arr, jsc_value_copy(obj->as.map.entries[i].key));
        result = arr;
    }

    else if (strcmp(metodo, "values") == 0 && obj->type == VAL_MAP) {
        JscValue *arr = jsc_array();
        for (size_t i = 0; i < obj->as.map.count; i++)
            jsc_array_push(arr, jsc_value_copy(obj->as.map.entries[i].value));
        result = arr;
    }

    else if (strcmp(metodo, "has") == 0 && obj->type == VAL_MAP) {
        if (n->args.count != 1) {
            runtime_error(state, "has() espera 1 argumento", (Node *)n);
        } else {
            JscValue *key = eval(n->args.items[0], env, state);
            if (!state->had_error) {
                JscValue *found = jsc_map_get(obj, key);
                result = jsc_bool(found != NULL);
                jsc_value_free(key);
            }
        }
    }

    else if (strcmp(metodo, "remove") == 0 && obj->type == VAL_MAP) {
        if (n->args.count != 1) {
            runtime_error(state, "remove() espera 1 argumento", (Node *)n);
        } else {
            JscValue *key = eval(n->args.items[0], env, state);
            if (!state->had_error) {
                for (size_t i = 0; i < obj->as.map.count; i++) {
                    if (jsc_value_equals(obj->as.map.entries[i].key, key)) {
                        jsc_value_free(obj->as.map.entries[i].key);
                        jsc_value_free(obj->as.map.entries[i].value);
                        for (size_t j = i; j < obj->as.map.count - 1; j++)
                            obj->as.map.entries[j] = obj->as.map.entries[j + 1];
                        obj->as.map.count--;
                        break;
                    }
                }
                result = jsc_vazio();
                jsc_value_free(key);
            }
        }
    }

    /* STRING */
    else if (strcmp(metodo, "upper") == 0 && obj->type == VAL_STRING) {
        const char *s = obj->as.s ? obj->as.s : "";
        size_t len = strlen(s);
        char *out = (char *)malloc(len + 1);
        for (size_t i = 0; i < len; i++) {
            char ch = s[i];
            out[i] = (ch >= 'a' && ch <= 'z') ? ch - 32 : ch;
        }
        out[len] = '\0';
        result = jsc_string(out);
        free(out);
    }

    else if (strcmp(metodo, "lower") == 0 && obj->type == VAL_STRING) {
        const char *s = obj->as.s ? obj->as.s : "";
        size_t len = strlen(s);
        char *out = (char *)malloc(len + 1);
        for (size_t i = 0; i < len; i++) {
            char ch = s[i];
            out[i] = (ch >= 'A' && ch <= 'Z') ? ch + 32 : ch;
        }
        out[len] = '\0';
        result = jsc_string(out);
        free(out);
    }

    else if (strcmp(metodo, "split") == 0 && obj->type == VAL_STRING) {
        if (n->args.count != 1) {
            runtime_error(state, "split() espera 1 argumento", (Node *)n);
        } else {
            JscValue *sep = eval(n->args.items[0], env, state);
            if (!state->had_error) {
                const char *s = obj->as.s ? obj->as.s : "";
                const char *sp = (sep->type == VAL_STRING && sep->as.s) ? sep->as.s : "";
                size_t sp_len = strlen(sp);
                JscValue *arr = jsc_array();
                if (sp_len == 0) {
                    for (size_t i = 0; s[i]; i++) {
                        char b[2] = { s[i], '\0' };
                        jsc_array_push(arr, jsc_string(b));
                    }
                } else {
                    const char *start = s;
                    const char *p;
                    while ((p = strstr(start, sp)) != NULL) {
                        size_t len = p - start;
                        char *piece = (char *)malloc(len + 1);
                        memcpy(piece, start, len);
                        piece[len] = '\0';
                        jsc_array_push(arr, jsc_string(piece));
                        free(piece);
                        start = p + sp_len;
                    }
                    jsc_array_push(arr, jsc_string(start));
                }
                result = arr;
                jsc_value_free(sep);
            }
        }
    }

    else if (strcmp(metodo, "trim") == 0 && obj->type == VAL_STRING) {
        const char *s = obj->as.s ? obj->as.s : "";
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
        size_t len = strlen(s);
        while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\n' || s[len-1] == '\r')) len--;
        char *out = (char *)malloc(len + 1);
        memcpy(out, s, len);
        out[len] = '\0';
        result = jsc_string(out);
        free(out);
    }

    else if (strcmp(metodo, "replace") == 0 && obj->type == VAL_STRING) {
        if (n->args.count != 2) {
            runtime_error(state, "replace() espera 2 argumentos", (Node *)n);
        } else {
            JscValue *old_v = eval(n->args.items[0], env, state);
            if (!state->had_error) {
                JscValue *new_v = eval(n->args.items[1], env, state);
                if (!state->had_error) {
                    const char *s = obj->as.s ? obj->as.s : "";
                    const char *old = (old_v->type == VAL_STRING && old_v->as.s) ? old_v->as.s : "";
                    const char *nw = (new_v->type == VAL_STRING && new_v->as.s) ? new_v->as.s : "";
                    size_t old_len = strlen(old), new_len = strlen(nw);
                    if (old_len == 0) {
                        result = jsc_string(s);
                    } else {
                        size_t count = 0;
                        const char *p = s;
                        while ((p = strstr(p, old)) != NULL) { count++; p += old_len; }
                        size_t s_len = strlen(s);
                        size_t out_len = s_len + count * (new_len - old_len);
                        char *out = (char *)malloc(out_len + 1);
                        char *dst = out;
                        const char *src = s;
                        while ((p = strstr(src, old)) != NULL) {
                            size_t before = p - src;
                            memcpy(dst, src, before); dst += before;
                            memcpy(dst, nw, new_len); dst += new_len;
                            src = p + old_len;
                        }
                        strcpy(dst, src);
                        result = jsc_string(out);
                        free(out);
                    }
                    jsc_value_free(new_v);
                }
                jsc_value_free(old_v);
            }
        }
    }

    /* CONVERSAO */
    else if (strcmp(metodo, "to_int") == 0) {
        if (obj->type == VAL_INT) result = jsc_int(obj->as.i);
        else if (obj->type == VAL_FLOAT) result = jsc_int((long long)obj->as.f);
        else if (obj->type == VAL_BOOL) result = jsc_int(obj->as.b ? 1 : 0);
        else if (obj->type == VAL_STRING) {
            const char *s = obj->as.s ? obj->as.s : "";
            char *end = NULL;
            long long v = strtoll(s, &end, 10);
            if (end == s) v = (long long)strtod(s, NULL);
            result = jsc_int(v);
        }
        else if (obj->type == VAL_VAZIO) result = jsc_int(0);
        else runtime_error(state, "to_int() nao suporta esse tipo", (Node *)n);
    }

    else if (strcmp(metodo, "to_float") == 0) {
        if (obj->type == VAL_INT) result = jsc_float((double)obj->as.i);
        else if (obj->type == VAL_FLOAT) result = jsc_float(obj->as.f);
        else if (obj->type == VAL_BOOL) result = jsc_float((double)obj->as.b);
        else if (obj->type == VAL_STRING) result = jsc_float(strtod(obj->as.s ? obj->as.s : "", NULL));
        else if (obj->type == VAL_VAZIO) result = jsc_float(0.0);
        else runtime_error(state, "to_float() nao suporta esse tipo", (Node *)n);
    }

    else if (strcmp(metodo, "to_str") == 0 || strcmp(metodo, "to_string") == 0) {
        if (obj->type == VAL_STRING) result = jsc_string(obj->as.s ? obj->as.s : "");
        else if (obj->type == VAL_INT) { char b[32]; snprintf(b, sizeof(b), "%lld", obj->as.i); result = jsc_string(b); }
        else if (obj->type == VAL_FLOAT) {
            char b[32];
            if (obj->as.f == (long long)obj->as.f) snprintf(b, sizeof(b), "%lld", (long long)obj->as.f);
            else snprintf(b, sizeof(b), "%g", obj->as.f);
            result = jsc_string(b);
        }
        else if (obj->type == VAL_BOOL) result = jsc_string(obj->as.b ? "true" : "false");
        else if (obj->type == VAL_VAZIO) result = jsc_string("vazio");
        else if (obj->type == VAL_ARRAY || obj->type == VAL_MAP) {
            /* Usa json_stringify_into pra formatar */
            extern void json_stringify_into(JscValue *v, char **out, size_t *len, size_t *cap, int pretty, int depth);
            size_t cap = 256, len = 0;
            char *out = malloc(cap);
            out[0] = 0;
            json_stringify_into(obj, &out, &len, &cap, 0, 0);
            result = jsc_string(out);
            free(out);
        }
        else result = jsc_string("<valor>");
    }

    else if (strcmp(metodo, "to_bool") == 0) {
        result = jsc_bool(jsc_value_is_truthy(obj));
    }

    /* TIPO */
    else if (strcmp(metodo, "is_int") == 0)     result = jsc_bool(obj->type == VAL_INT);
    else if (strcmp(metodo, "is_float") == 0)   result = jsc_bool(obj->type == VAL_FLOAT);
    else if (strcmp(metodo, "is_string") == 0)  result = jsc_bool(obj->type == VAL_STRING);
    else if (strcmp(metodo, "is_bool") == 0)    result = jsc_bool(obj->type == VAL_BOOL);
    else if (strcmp(metodo, "is_array") == 0)   result = jsc_bool(obj->type == VAL_ARRAY);
    else if (strcmp(metodo, "is_map") == 0)     result = jsc_bool(obj->type == VAL_MAP);
    else if (strcmp(metodo, "is_vazio") == 0)   result = jsc_bool(obj->type == VAL_VAZIO);
    else if (strcmp(metodo, "type") == 0)       result = jsc_string(jsc_type_name(obj->type));

    /* ============================================================
     * METODOS DE THREAD
     * ============================================================ */
    else if (obj->type == VAL_THREAD) {
        JscThread *t = get_thread(obj);

        if (strcmp(metodo, "join") == 0) {
            pthread_join(t->handle, NULL);
            result = t->result ? t->result : jsc_vazio();
            t->result = NULL;
        }
        else if (strcmp(metodo, "id") == 0) {
            result = jsc_int((long long)t->id);
        }
        else if (strcmp(metodo, "is_alive") == 0) {
            result = jsc_bool(t->done == 0);
        }
        else {
            char buf[128];
            snprintf(buf, sizeof(buf), "metodo de thread '%s' nao implementado", metodo);
            runtime_error(state, buf, (Node *)n);
        }
        /* NAO libera obj — veio direto do env (eval_no_copy) */
        return result ? result : jsc_vazio();
    }

    else {
        char buf[128];
        snprintf(buf, sizeof(buf), "metodo '%s' nao implementado", metodo);
        runtime_error(state, buf, (Node *)n);
    }

    /* NAO libera obj — veio do env (eval_no_copy) */
    return result ? result : jsc_vazio();
}

/* ============================================================
 * BLOCO B.4 — for each
 * ============================================================ */

JscValue *eval_for_each(Node *node, Env *env, EvalState *state) {
    NodeForEach *n = (NodeForEach *)node;

    JscValue *iter = eval(n->iterable, env, state);
    if (state->had_error) return NULL;

    Env *loop_env = env_new(env);

    if (iter->type == VAL_ARRAY) {
        for (size_t i = 0; i < iter->as.array.count; i++) {
            JscValue *item = iter->as.array.items[i];
            env_define(loop_env, n->var_name, jsc_value_copy(item));
            eval(n->body, loop_env, state);
            if (state->had_error) break;
            if (state->has_return) break;
        }
    }
    else if (iter->type == VAL_MAP) {
        for (size_t i = 0; i < iter->as.map.count; i++) {
            env_define(loop_env, n->var_name,
                       jsc_value_copy(iter->as.map.entries[i].key));
            eval(n->body, loop_env, state);
            if (state->had_error) break;
            if (state->has_return) break;
        }
    }
    else if (iter->type == VAL_STRING) {
        const char *s = iter->as.s ? iter->as.s : "";
        for (size_t i = 0; s[i]; i++) {
            char buf[2] = { s[i], '\0' };
            env_define(loop_env, n->var_name, jsc_string(buf));
            eval(n->body, loop_env, state);
            if (state->had_error) break;
            if (state->has_return) break;
        }
    }
    else {
        runtime_error(state, "for each espera array, map ou string", node);
    }

    env_free(loop_env);
    jsc_value_free(iter);
    return jsc_vazio();
}

/* ============================================================
 * BLOCO INPUT — Funcoes nativas
 * ============================================================ */

#include "parser.h"   /* pra NodeCall */

JscValue *native_input(void *call_ptr, void *env_ptr, void *state_ptr) {
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;
    NodeCall *n = (NodeCall *)call_ptr;

    /* Se tem argumento, imprime como prompt */
    if (n->args.count >= 1) {
        JscValue *prompt = eval(n->args.items[0], env, state);
        if (state->had_error) return NULL;
        jsc_print_value(prompt);
        fflush(stdout);
        jsc_value_free(prompt);
    }

    /* Lê uma linha do stdin */
    char buf[4096];
    if (fgets(buf, sizeof(buf), stdin) == NULL) {
        return jsc_string("");
    }

    /* Remove \n do final */
    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
        buf[--len] = '\0';
    }

    return jsc_string(buf);
}

void natives_init(Env *global) {
    env_define(global, "input",    jsc_native("input",    native_input));
    env_define(global, "spawn",    jsc_native("spawn",    native_spawn));
    env_define(global, "import_c", jsc_native("import_c", native_import_c));
    env_define(global, "call_c",   jsc_native("call_c",   native_call_c));
    env_define(global, "py", make_py_module());
    env_define(global, "tensor", make_tensor_module());
    env_define(global, "embedding", make_embedding_module());
    env_define(global, "attention", make_attention_module());
    env_define(global, "multihead", make_multihead_module());
    env_define(global, "block", make_block_module());
    env_define(global, "model", make_model_module());
    env_define(global, "transformer", make_transformer_module());
    env_define(global, "loss", make_loss_module());
    env_define(global, "backprop", make_backprop_module());
    env_define(global, "backprop_ff", make_backprop_ff_module());
    env_define(global, "backprop_ln", make_backprop_ln_module());
    env_define(global, "backprop_attn", make_backprop_attn_module());
    env_define(global, "optimizer", make_optimizer_module());
    env_define(global, "forward_cache", make_forward_cache_module());
    env_define(global, "backprop_layer", make_backprop_layer_module());
}

/* ============================================================
 * BLOCO C — jsc math$+
 * ============================================================ */

#include <math.h>

/* Macros pra funções nativas do math */
#define MATH_FN_1(name, expr) \
    JscValue *math_##name(void *call_ptr, void *env_ptr, void *state_ptr) { \
        NodeCall *n = (NodeCall *)call_ptr; \
        Env *env = (Env *)env_ptr; \
        EvalState *state = (EvalState *)state_ptr; \
        if (n->args.count != 1) { \
            runtime_error(state, "math." #name "() espera 1 argumento", (Node *)n); \
            return NULL; \
        } \
        JscValue *arg = eval(n->args.items[0], env, state); \
        if (state->had_error) return NULL; \
        double x = jsc_value_to_float(arg); \
        jsc_value_free(arg); \
        (void)x; \
        return jsc_float(expr); \
    }

MATH_FN_1(sqrt,   sqrt(x))
MATH_FN_1(abs,    x < 0 ? -x : x)
MATH_FN_1(floor,  floor(x))
MATH_FN_1(ceil,   ceil(x))
MATH_FN_1(round,  round(x))
MATH_FN_1(sin,    sin(x))
MATH_FN_1(cos,    cos(x))
MATH_FN_1(tan,    tan(x))
MATH_FN_1(asin,   asin(x))
MATH_FN_1(acos,   acos(x))
MATH_FN_1(atan,   atan(x))
MATH_FN_1(log,    log(x))
MATH_FN_1(log10,  log10(x))
MATH_FN_1(exp,    exp(x))

JscValue *math_pow(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "math.pow() espera 2 argumentos", (Node *)n);
        return NULL;
    }

    JscValue *a = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    JscValue *b = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(a); return NULL; }

    double r = pow(jsc_value_to_float(a), jsc_value_to_float(b));
    jsc_value_free(a);
    jsc_value_free(b);
    return jsc_float(r);
}

JscValue *math_min(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "math.min() espera 2 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *a = eval(n->args.items[0], env, state);
    JscValue *b = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(a); jsc_value_free(b); return NULL; }
    double x = jsc_value_to_float(a);
    double y = jsc_value_to_float(b);
    jsc_value_free(a); jsc_value_free(b);
    return jsc_float(x < y ? x : y);
}

JscValue *math_max(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "math.max() espera 2 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *a = eval(n->args.items[0], env, state);
    JscValue *b = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(a); jsc_value_free(b); return NULL; }
    double x = jsc_value_to_float(a);
    double y = jsc_value_to_float(b);
    jsc_value_free(a); jsc_value_free(b);
    return jsc_float(x > y ? x : y);
}

JscValue *math_random(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;
    return jsc_float((double)rand() / (double)RAND_MAX);
}

JscValue *math_random_int(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "math.random_int() espera 2 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *a = eval(n->args.items[0], env, state);
    JscValue *b = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(a); jsc_value_free(b); return NULL; }
    long long lo = jsc_value_to_int(a);
    long long hi = jsc_value_to_int(b);
    jsc_value_free(a); jsc_value_free(b);

    if (lo > hi) { long long t = lo; lo = hi; hi = t; }
    long long range = hi - lo + 1;
    return jsc_int(lo + (rand() % range));
}

/* Cria o módulo math e devolve */
JscValue *math_gcd(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "math.gcd() espera 2 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *a = eval(n->args.items[0], env, state);
    JscValue *b = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(a); jsc_value_free(b); return NULL; }
    long long x = jsc_value_to_int(a), y = jsc_value_to_int(b);
    jsc_value_free(a); jsc_value_free(b);
    if (x < 0) x = -x;
    if (y < 0) y = -y;
    while (y != 0) { long long t = y; y = x % y; x = t; }
    return jsc_int(x);
}

JscValue *math_factorial(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "math.factorial() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *a = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    long long x = jsc_value_to_int(a);
    jsc_value_free(a);
    if (x < 0) x = 0;
    if (x > 20) x = 20;
    long long r = 1;
    for (long long i = 2; i <= x; i++) r *= i;
    return jsc_int(r);
}

JscValue *math_is_prime(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "math.is_prime() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *a = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    long long x = jsc_value_to_int(a);
    jsc_value_free(a);
    if (x < 2) return jsc_bool(0);
    if (x < 4) return jsc_bool(1);
    if (x % 2 == 0) return jsc_bool(0);
    for (long long i = 3; i * i <= x; i += 2) {
        if (x % i == 0) return jsc_bool(0);
    }
    return jsc_bool(1);
}

static JscValue *make_math_module(void) {
    JscValue *m = jsc_map();

    /* Constantes */
    jsc_map_set(m, jsc_string("pi"), jsc_float(3.14159265358979323846));
    jsc_map_set(m, jsc_string("e"),  jsc_float(2.71828182845904523536));

    /* Funções */
    #define REG(name) jsc_map_set(m, jsc_string(#name), jsc_native(#name, math_##name))
    REG(sqrt);
    REG(abs);
    REG(floor);
    REG(ceil);
    REG(round);
    REG(sin);
    REG(cos);
    REG(tan);
    REG(asin);
    REG(acos);
    REG(atan);
    REG(log);
    REG(log10);
    REG(exp);
    REG(pow);
    REG(min);
    REG(max);
    REG(random);
    REG(random_int);
    REG(gcd);
    REG(factorial);
    REG(is_prime);
    #undef REG

    return m;
}

/* ============================================================
 * BLOCO FS — jsc fs$+
 * ============================================================ */

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

/* fs.read(path) → string */
JscValue *fs_read(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "fs.read() espera 1 argumento", (Node *)n);
        return NULL;
    }

    JscValue *path_v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    char *path = strdup((path_v->type == VAL_STRING && path_v->as.s) ? path_v->as.s : "");
    jsc_value_free(path_v);

    FILE *f = fopen(path, "rb");
    if (!f) {
        char buf[256];
        snprintf(buf, sizeof(buf), "fs.read: nao consegui abrir '%s'", path);
        runtime_error(state, buf, (Node *)n);
        free(path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    JscValue *result = jsc_string(buf);
    free(buf);
    free(path);
    return result;
}

/* fs.write(path, conteudo) */
JscValue *fs_write(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "fs.write() espera 2 argumentos", (Node *)n);
        return NULL;
    }

    JscValue *path_v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    JscValue *content_v = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(path_v); return NULL; }

    char *path = strdup((path_v->type == VAL_STRING && path_v->as.s) ? path_v->as.s : "");
    char *content = strdup((content_v->type == VAL_STRING && content_v->as.s) ? content_v->as.s : "");

    FILE *f = fopen(path, "wb");
    if (!f) {
        char buf[256];
        snprintf(buf, sizeof(buf), "fs.write: nao consegui criar '%s'", path);
        runtime_error(state, buf, (Node *)n);
        jsc_value_free(path_v);
        jsc_value_free(content_v);
        free(path);
        free(content);
        return NULL;
    }
    fwrite(content, 1, strlen(content), f);
    fclose(f);

    jsc_value_free(path_v);
    jsc_value_free(content_v);
    free(path);
    free(content);
    return jsc_vazio();
}

/* fs.append(path, conteudo) */
JscValue *fs_append(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "fs.append() espera 2 argumentos", (Node *)n);
        return NULL;
    }

    JscValue *path_v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    JscValue *content_v = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(path_v); return NULL; }

    char *path = strdup((path_v->type == VAL_STRING && path_v->as.s) ? path_v->as.s : "");
    char *content = strdup((content_v->type == VAL_STRING && content_v->as.s) ? content_v->as.s : "");

    FILE *f = fopen(path, "ab");
    if (!f) {
        runtime_error(state, "fs.append: nao consegui abrir arquivo", (Node *)n);
        jsc_value_free(path_v);
        jsc_value_free(content_v);
        free(path);
        free(content);
        return NULL;
    }
    fwrite(content, 1, strlen(content), f);
    fclose(f);

    jsc_value_free(path_v);
    jsc_value_free(content_v);
    free(path);
    free(content);
    return jsc_vazio();
}

/* fs.exists(path) → bool */
JscValue *fs_exists(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "fs.exists() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *path_v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    char *path = strdup((path_v->type == VAL_STRING && path_v->as.s) ? path_v->as.s : "");
    jsc_value_free(path_v);
    struct stat st;
    int ok = (stat(path, &st) == 0);
    free(path);
    return jsc_bool(ok);
}

/* fs.is_file(path) → bool */
JscValue *fs_is_file(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "fs.is_file() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *path_v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    char *path = strdup((path_v->type == VAL_STRING && path_v->as.s) ? path_v->as.s : "");
    jsc_value_free(path_v);
    struct stat st;
    int ok = (stat(path, &st) == 0) && S_ISREG(st.st_mode);
    free(path);
    return jsc_bool(ok);
}

/* fs.is_dir(path) → bool */
JscValue *fs_is_dir(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "fs.is_dir() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *path_v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    char *path = strdup((path_v->type == VAL_STRING && path_v->as.s) ? path_v->as.s : "");
    jsc_value_free(path_v);
    struct stat st;
    int ok = (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
    free(path);
    return jsc_bool(ok);
}

/* fs.size(path) → int (bytes) */
JscValue *fs_size(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "fs.size() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *path_v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    char *path = strdup((path_v->type == VAL_STRING && path_v->as.s) ? path_v->as.s : "");
    jsc_value_free(path_v);
    struct stat st;
    long long tam = 0;
    if (stat(path, &st) == 0) tam = (long long)st.st_size;
    free(path);
    return jsc_int(tam);
}

/* fs.remove(path) */
JscValue *fs_remove(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "fs.remove() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *path_v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    char *path = strdup((path_v->type == VAL_STRING && path_v->as.s) ? path_v->as.s : "");
    jsc_value_free(path_v);
    
    int r = remove(path);
    
    free(path);
    JscValue *result = jsc_bool(r == 0);
    
    return result;
}

/* fs.mkdir(path) */
JscValue *fs_mkdir(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "fs.mkdir() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *path_v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    char *path = strdup((path_v->type == VAL_STRING && path_v->as.s) ? path_v->as.s : "");
    jsc_value_free(path_v);
    int r = mkdir(path, 0755);
    free(path);
    return jsc_bool(r == 0);
}

/* fs.list(dir) → array de nomes */
JscValue *fs_list(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "fs.list() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *path_v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    char *path = strdup((path_v->type == VAL_STRING && path_v->as.s) ? path_v->as.s : "");
    jsc_value_free(path_v);

    DIR *d = opendir(path);
    if (!d) {
        char buf[256];
        snprintf(buf, sizeof(buf), "fs.list: nao consegui abrir '%s'", path);
        runtime_error(state, buf, (Node *)n);
        free(path);
        return NULL;
    }

    JscValue *arr = jsc_array();
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        jsc_array_push(arr, jsc_string(entry->d_name));
    }
    closedir(d);
    free(path);
    return arr;
}

/* fs.copy(origem, destino) */
JscValue *fs_copy(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "fs.copy() espera 2 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *src_v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    JscValue *dst_v = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(src_v); return NULL; }

    char *src = strdup((src_v->type == VAL_STRING && src_v->as.s) ? src_v->as.s : "");
    char *dst = strdup((dst_v->type == VAL_STRING && dst_v->as.s) ? dst_v->as.s : "");

    FILE *fi = fopen(src, "rb");
    if (!fi) {
        runtime_error(state, "fs.copy: origem nao encontrada", (Node *)n);
        jsc_value_free(src_v);
        jsc_value_free(dst_v);
        free(src);
        free(dst);
        return NULL;
    }
    FILE *fo = fopen(dst, "wb");
    if (!fo) {
        fclose(fi);
        runtime_error(state, "fs.copy: nao consegui criar destino", (Node *)n);
        jsc_value_free(src_v);
        jsc_value_free(dst_v);
        free(src);
        free(dst);
        return NULL;
    }

    char buf[4096];
    size_t n_read;
    while ((n_read = fread(buf, 1, sizeof(buf), fi)) > 0) {
        fwrite(buf, 1, n_read, fo);
    }
    fclose(fi);
    fclose(fo);

    jsc_value_free(src_v);
    jsc_value_free(dst_v);
    free(src);
    free(dst);
    return jsc_vazio();
}

/* fs.lines(path) → array de linhas */
JscValue *fs_lines(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "fs.lines() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *path_v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    char *path = strdup((path_v->type == VAL_STRING && path_v->as.s) ? path_v->as.s : "");
    jsc_value_free(path_v);

    FILE *f = fopen(path, "rb");
    if (!f) {
        runtime_error(state, "fs.lines: nao consegui abrir arquivo", (Node *)n);
        free(path);
        return NULL;
    }

    JscValue *arr = jsc_array();
    char buf[4096];
    while (fgets(buf, sizeof(buf), f) != NULL) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            buf[--len] = '\0';
        }
        jsc_array_push(arr, jsc_string(buf));
    }
    fclose(f);
    free(path);
    return arr;
}

/* ============================================================
 * MODULO PROC — rodar comandos do sistema
 * ============================================================ */

#include <sys/wait.h>

/* proc.run(cmd) — roda comando, devolve codigo de saida */
JscValue *proc_run(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "proc.run() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    char *cmd = strdup((v->type == VAL_STRING && v->as.s) ? v->as.s : "");
    jsc_value_free(v);

    int r = system(cmd);
    free(cmd);
    return jsc_int(WEXITSTATUS(r));
}

/* proc.capture(cmd) — roda comando e captura stdout */
JscValue *proc_capture(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "proc.capture() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *cmd = (v->type == VAL_STRING && v->as.s) ? v->as.s : "";

    FILE *f = popen(cmd, "r");
    if (!f) {
        jsc_value_free(v);
        return jsc_string("");
    }

    size_t cap = 4096, len = 0;
    char *out = (char *)malloc(cap);
    size_t r;
    while ((r = fread(out + len, 1, cap - len - 1, f)) > 0) {
        len += r;
        if (len + 1024 >= cap) { cap *= 2; out = (char *)realloc(out, cap); }
    }
    out[len] = '\0';
    pclose(f);
    jsc_value_free(v);

    JscValue *res = jsc_string(out);
    free(out);
    return res;
}

/* proc.exit(codigo) — sai do programa */
JscValue *proc_exit(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    int code = 0;
    if (n->args.count >= 1) {
        JscValue *v = eval(n->args.items[0], env, state);
        if (v) { code = (int)jsc_value_to_int(v); jsc_value_free(v); }
    }
    exit(code);
    return jsc_vazio();
}

static JscValue *make_proc_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, jsc_string("run"),     jsc_native("run",     proc_run));
    jsc_map_set(m, jsc_string("capture"), jsc_native("capture", proc_capture));
    jsc_map_set(m, jsc_string("exit"),    jsc_native("exit",    proc_exit));
    return m;
}

/* ============================================================
 * BLOCO PY — jsc py$+  (integracao com Python 3 via subprocess)
 * ============================================================ */

#include <unistd.h>

/* Escreve codigo Python num arquivo temp e devolve o path (malloc'd).
 * Retorna NULL em falha. */
static char *py_write_temp(const char *codigo) {
    char tmpl[] = "/tmp/jsc_py_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return NULL;
    size_t len = strlen(codigo);
    ssize_t w = write(fd, codigo, len);
    close(fd);
    if (w < 0 || (size_t)w != len) {
        unlink(tmpl);
        return NULL;
    }
    return strdup(tmpl);
}

/* Helper — pega o unico argumento string, roda python, retorna stdout.
 * captura=1 → retorna string. captura=0 → imprime e retorna vazio. */
static JscValue *py_run_common(void *call_ptr, void *env_ptr, void *state_ptr,
                               int captura, const char *fname) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, fname, (Node *)n);
        return NULL;
    }
    JscValue *v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    if (v->type != VAL_STRING || !v->as.s) {
        jsc_value_free(v);
        runtime_error(state, "py: argumento precisa ser string", (Node *)n);
        return NULL;
    }

    char *path = py_write_temp(v->as.s);
    jsc_value_free(v);
    if (!path) {
        runtime_error(state, "py: falha ao criar arquivo temporario", (Node *)n);
        return NULL;
    }

    size_t cmd_len = strlen(path) + 16;
    char *cmd = (char *)malloc(cmd_len);
    snprintf(cmd, cmd_len, "python3 %s", path);

    FILE *f = popen(cmd, "r");
    free(cmd);
    if (!f) {
        unlink(path);
        free(path);
        return jsc_string("");
    }

    size_t cap = 4096, len = 0;
    char *out = (char *)malloc(cap);
    size_t r;
    while ((r = fread(out + len, 1, cap - len - 1, f)) > 0) {
        len += r;
        if (len + 1024 >= cap) { cap *= 2; out = (char *)realloc(out, cap); }
    }
    out[len] = '\0';
    pclose(f);

    unlink(path);
    free(path);

    if (captura) {
        JscValue *res = jsc_string(out);
        free(out);
        return res;
    }

    fputs(out, stdout);
    free(out);
    return jsc_vazio();
}

/* py.exec(codigo) — roda e deixa stdout passar */
JscValue *py_exec(void *call_ptr, void *env_ptr, void *state_ptr) {
    return py_run_common(call_ptr, env_ptr, state_ptr, 0, "py.exec() espera 1 argumento");
}

/* py.eval(codigo) — roda e retorna stdout como string */
JscValue *py_eval(void *call_ptr, void *env_ptr, void *state_ptr) {
    return py_run_common(call_ptr, env_ptr, state_ptr, 1, "py.eval() espera 1 argumento");
}

/* py.version() — retorna versao do Python */
JscValue *py_version(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;

    FILE *f = popen("python3 --version 2>&1", "r");
    if (!f) return jsc_string("python3 nao encontrado");

    char buf[256] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    pclose(f);
    buf[n] = '\0';

    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) {
        buf[--n] = '\0';
    }
    return jsc_string(buf);
}

static JscValue *make_py_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, jsc_string("exec"),    jsc_native("exec",    py_exec));
    jsc_map_set(m, jsc_string("eval"),    jsc_native("eval",    py_eval));
    jsc_map_set(m, jsc_string("version"), jsc_native("version", py_version));
    return m;
}


/* ============================================================
 * LOTE 4 P1 — regex, zip, csv, yaml, dns
 * ============================================================ */

#include <regex.h>
#include <zlib.h>
#include <resolv.h>
#include <arpa/nameser.h>

/* ---------- REGEX ---------- */

JscValue *regex_match(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "regex.match() espera 2 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *t = eval(n->args.items[0], env, state);
    JscValue *p = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(t); jsc_value_free(p); return NULL; }

    const char *txt = (t->type == VAL_STRING && t->as.s) ? t->as.s : "";
    const char *pat = (p->type == VAL_STRING && p->as.s) ? p->as.s : "";

    regex_t re;
    int r = regcomp(&re, pat, REG_EXTENDED | REG_NOSUB);
    if (r != 0) {
        runtime_error(state, "regex invalido", (Node *)n);
        jsc_value_free(t); jsc_value_free(p);
        return NULL;
    }
    int ok = (regexec(&re, txt, 0, NULL, 0) == 0);
    regfree(&re);

    jsc_value_free(t); jsc_value_free(p);
    return jsc_bool(ok);
}

JscValue *regex_replace(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 3) {
        runtime_error(state, "regex.replace() espera 3 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *t = eval(n->args.items[0], env, state);
    JscValue *p = eval(n->args.items[1], env, state);
    JscValue *r = eval(n->args.items[2], env, state);
    if (state->had_error) { jsc_value_free(t); jsc_value_free(p); jsc_value_free(r); return NULL; }

    const char *txt = (t->type == VAL_STRING && t->as.s) ? t->as.s : "";
    const char *pat = (p->type == VAL_STRING && p->as.s) ? p->as.s : "";
    const char *rep = (r->type == VAL_STRING && r->as.s) ? r->as.s : "";

    regex_t re;
    if (regcomp(&re, pat, REG_EXTENDED) != 0) {
        runtime_error(state, "regex invalido", (Node *)n);
        jsc_value_free(t); jsc_value_free(p); jsc_value_free(r);
        return NULL;
    }

    size_t cap = strlen(txt) * 2 + 64;
    char *out = (char *)malloc(cap);
    size_t out_len = 0;
    const char *cursor = txt;
    regmatch_t m;

    while (regexec(&re, cursor, 1, &m, 0) == 0) {
        size_t before = m.rm_so;
        size_t rep_len = strlen(rep);
        size_t need = out_len + before + rep_len + 1;
        if (need > cap) { cap = need * 2; out = (char *)realloc(out, cap); }
        memcpy(out + out_len, cursor, before);
        out_len += before;
        memcpy(out + out_len, rep, rep_len);
        out_len += rep_len;
        cursor += m.rm_eo;
        if (m.rm_eo == m.rm_so) break;
    }
    out[out_len] = '\0';

    JscValue *result = jsc_string(out);
    free(out);
    regfree(&re);
    jsc_value_free(t); jsc_value_free(p); jsc_value_free(r);
    return result;
}

JscValue *regex_find_all(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "regex.find_all() espera 2 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *t = eval(n->args.items[0], env, state);
    JscValue *p = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(t); jsc_value_free(p); return NULL; }

    const char *txt = (t->type == VAL_STRING && t->as.s) ? t->as.s : "";
    const char *pat = (p->type == VAL_STRING && p->as.s) ? p->as.s : "";

    regex_t re;
    if (regcomp(&re, pat, REG_EXTENDED) != 0) {
        runtime_error(state, "regex invalido", (Node *)n);
        jsc_value_free(t); jsc_value_free(p);
        return NULL;
    }

    JscValue *arr = jsc_array();
    const char *cursor = txt;
    regmatch_t m;
    while (regexec(&re, cursor, 1, &m, 0) == 0) {
        size_t len = m.rm_eo - m.rm_so;
        char *buf = (char *)malloc(len + 1);
        memcpy(buf, cursor + m.rm_so, len);
        buf[len] = '\0';
        jsc_array_push(arr, jsc_string(buf));
        free(buf);
        cursor += m.rm_eo;
        if (m.rm_eo == m.rm_so) break;
    }
    regfree(&re);
    jsc_value_free(t); jsc_value_free(p);
    return arr;
}

static JscValue *make_regex_module(void) {
    JscValue *m = jsc_map();
    #define REG(name) jsc_map_set(m, jsc_string(#name), jsc_native(#name, regex_##name))
    REG(match);
    REG(replace);
    REG(find_all);
    #undef REG
    return m;
}

/* ---------- ZIP (gzip) ---------- */

JscValue *zip_compress(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "zip.compress() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *a = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *src = (a->type == VAL_STRING && a->as.s) ? a->as.s : "";
    size_t slen = strlen(src);

    uLongf dest_len = compressBound(slen);
    unsigned char *dest = (unsigned char *)malloc(dest_len);
    if (compress(dest, &dest_len, (const Bytef *)src, slen) != Z_OK) {
        runtime_error(state, "zip.compress: falhou", (Node *)n);
        free(dest); jsc_value_free(a);
        return NULL;
    }

    /* Codifica em base64 pra ser seguro em string C */
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len = ((dest_len + 2) / 3) * 4;
    char *out = (char *)malloc(out_len + 1);
    size_t i, j;
    for (i = 0, j = 0; i < dest_len;) {
        unsigned int x = i < dest_len ? dest[i++] : 0;
        unsigned int y = i < dest_len ? dest[i++] : 0;
        unsigned int z = i < dest_len ? dest[i++] : 0;
        unsigned int triple = (x << 16) | (y << 8) | z;
        out[j++] = b64[(triple >> 18) & 0x3F];
        out[j++] = b64[(triple >> 12) & 0x3F];
        out[j++] = b64[(triple >> 6)  & 0x3F];
        out[j++] = b64[triple & 0x3F];
    }
    size_t pad = (3 - (dest_len % 3)) % 3;
    for (size_t k = 0; k < pad; k++) out[out_len - 1 - k] = '=';
    out[out_len] = '\0';

    JscValue *r = jsc_string(out);
    free(out);
    free(dest);
    jsc_value_free(a);
    return r;
}

JscValue *zip_decompress(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count < 1 || n->args.count > 2) {
        runtime_error(state, "zip.decompress() espera 1 ou 2 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *a = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *src = (a->type == VAL_STRING && a->as.s) ? a->as.s : "";
    size_t slen = strlen(src);

    /* Decodifica base64 */
    int map[256];
    for (int i = 0; i < 256; i++) map[i] = -1;
    const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; i++) map[(unsigned char)b64[i]] = i;

    unsigned char *compressed = (unsigned char *)malloc(slen + 1);
    size_t clen = 0;
    int buf = 0, bits = 0;
    for (size_t i = 0; i < slen; i++) {
        if (src[i] == '=') break;
        int v = map[(unsigned char)src[i]];
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) { bits -= 8; compressed[clen++] = (buf >> bits) & 0xFF; }
    }

    uLongf cap = 65536;
    if (n->args.count == 2) {
        JscValue *sz = eval(n->args.items[1], env, state);
        if (sz) { cap = (uLongf)jsc_value_to_int(sz); jsc_value_free(sz); }
    }

    unsigned char *dest = (unsigned char *)malloc(cap + 1);
    if (uncompress(dest, &cap, compressed, clen) != Z_OK) {
        runtime_error(state, "zip.decompress: falhou", (Node *)n);
        free(compressed); free(dest); jsc_value_free(a);
        return NULL;
    }
    dest[cap] = '\0';

    JscValue *r = jsc_string((const char *)dest);
    free(compressed);
    free(dest);
    jsc_value_free(a);
    return r;
}

static JscValue *make_zip_module(void) {
    JscValue *m = jsc_map();
    #define REG(name) jsc_map_set(m, jsc_string(#name), jsc_native(#name, zip_##name))
    REG(compress);
    REG(decompress);
    #undef REG
    return m;
}

/* ---------- CSV ---------- */

JscValue *csv_parse(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "csv.parse() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *a = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *src = (a->type == VAL_STRING && a->as.s) ? a->as.s : "";

    JscValue *rows = jsc_array();
    char *buf = strdup(src);
    char *line = strtok(buf, "\n");
    while (line) {
        JscValue *row = jsc_array();
        /* Remove \r */
        size_t ll = strlen(line);
        while (ll > 0 && (line[ll-1] == '\r')) line[--ll] = '\0';

        char *cell = line;
        char *next;
        while (1) {
            next = strchr(cell, ',');
            if (next) { *next = '\0'; next++; }
            jsc_array_push(row, jsc_string(cell));
            if (!next) break;
            cell = next;
        }
        jsc_array_push(rows, row);
        line = strtok(NULL, "\n");
    }
    free(buf);
    jsc_value_free(a);
    return rows;
}

static JscValue *make_csv_module(void) {
    JscValue *m = jsc_map();
    #define REG(name) jsc_map_set(m, jsc_string(#name), jsc_native(#name, csv_##name))
    REG(parse);
    #undef REG
    return m;
}

/* ---------- NET.DNS_LOOKUP ---------- */

JscValue *net_dns_lookup(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "net.dns_lookup() espera 2 argumentos (host, tipo)", (Node *)n);
        return NULL;
    }
    JscValue *h = eval(n->args.items[0], env, state);
    JscValue *t = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(h); jsc_value_free(t); return NULL; }

    const char *host = (h->type == VAL_STRING && h->as.s) ? h->as.s : "";
    const char *tipo = (t->type == VAL_STRING && t->as.s) ? t->as.s : "A";

    JscValue *arr = jsc_array();

    /* Usa getaddrinfo pra A e AAAA */
    if (strcmp(tipo, "A") == 0 || strcmp(tipo, "AAAA") == 0) {
        struct addrinfo hints, *res, *p;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = (strcmp(tipo, "A") == 0) ? AF_INET : AF_INET6;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host, NULL, &hints, &res) == 0) {
            for (p = res; p != NULL; p = p->ai_next) {
                char buf[INET6_ADDRSTRLEN];
                void *addr;
                if (p->ai_family == AF_INET) addr = &((struct sockaddr_in *)p->ai_addr)->sin_addr;
                else addr = &((struct sockaddr_in6 *)p->ai_addr)->sin6_addr;
                inet_ntop(p->ai_family, addr, buf, sizeof(buf));
                jsc_array_push(arr, jsc_string(buf));
            }
            freeaddrinfo(res);
        }
    }

    jsc_value_free(h); jsc_value_free(t);
    return arr;
}

static JscValue *make_crypto_module(void) {
    JscValue *m = jsc_map();
    #define REG(name) jsc_map_set(m, jsc_string(#name), jsc_native(#name, crypto_##name))
    REG(base64_encode);
    REG(base64_decode);
    REG(hex_encode);
    REG(hex_decode);
    REG(random_bytes);
    REG(random_hex);
    REG(random_int);
    REG(xor);
    REG(md5);
    REG(sha1);
    REG(sha256);
    REG(sha512);
    REG(hmac_sha256);
    REG(uuid);
    REG(crack_md5);
    REG(crack_sha1);
    REG(crack_sha256);
    #undef REG
    return m;
}

/* ============================================================
 * BLOCO B — json + time completo
 * ============================================================ */

#include <ctype.h>

/* ---------- JSON ---------- */

void json_stringify_into(JscValue *v, char **out, size_t *len, size_t *cap, int pretty, int depth) {
    #define APPEND(s) do { \
        size_t _l = strlen(s); \
        if (*len + _l + 1 > *cap) { *cap = (*cap) * 2 + _l + 64; *out = realloc(*out, *cap); } \
        memcpy(*out + *len, s, _l); *len += _l; (*out)[*len] = 0; \
    } while(0)

    if (!v) { APPEND("null"); return; }

    char buf[512];

    switch (v->type) {
        case VAL_VAZIO:
            APPEND("null");
            break;
        case VAL_BOOL:
            APPEND(v->as.b ? "true" : "false");
            break;
        case VAL_INT:
            snprintf(buf, sizeof(buf), "%lld", v->as.i);
            APPEND(buf);
            break;
        case VAL_FLOAT:
            if (v->as.f == (long long)v->as.f) {
                snprintf(buf, sizeof(buf), "%lld", (long long)v->as.f);
            } else {
                snprintf(buf, sizeof(buf), "%g", v->as.f);
            }
            APPEND(buf);
            break;
        case VAL_STRING:
            APPEND("\"");
            for (const char *s = v->as.s ? v->as.s : ""; *s; s++) {
                if (*s == '"') APPEND("\\\"");
                else if (*s == '\\') APPEND("\\\\");
                else if (*s == '\n') APPEND("\\n");
                else if (*s == '\t') APPEND("\\t");
                else if (*s == '\r') APPEND("\\r");
                else { buf[0] = *s; buf[1] = 0; APPEND(buf); }
            }
            APPEND("\"");
            break;
        case VAL_ARRAY: {
            APPEND("[");
            for (size_t i = 0; i < v->as.array.count; i++) {
                if (i > 0) APPEND(",");
                if (pretty) { APPEND("\n"); for (int k = 0; k <= depth; k++) APPEND("  "); }
                json_stringify_into(v->as.array.items[i], out, len, cap, pretty, depth + 1);
            }
            if (pretty && v->as.array.count > 0) { APPEND("\n"); for (int k = 0; k < depth; k++) APPEND("  "); }
            APPEND("]");
            break;
        }
        case VAL_MAP: {
            APPEND("{");
            for (size_t i = 0; i < v->as.map.count; i++) {
                if (i > 0) APPEND(",");
                if (pretty) { APPEND("\n"); for (int k = 0; k <= depth; k++) APPEND("  "); }
                json_stringify_into(v->as.map.entries[i].key, out, len, cap, pretty, depth + 1);
                APPEND(":");
                if (pretty) APPEND(" ");
                json_stringify_into(v->as.map.entries[i].value, out, len, cap, pretty, depth + 1);
            }
            if (pretty && v->as.map.count > 0) { APPEND("\n"); for (int k = 0; k < depth; k++) APPEND("  "); }
            APPEND("}");
            break;
        }
        default:
            APPEND("null");
            break;
    }
    #undef APPEND
}

JscValue *json_stringify(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "json.stringify() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    size_t cap = 256, len = 0;
    char *out = malloc(cap);
    out[0] = 0;

    json_stringify_into(v, &out, &len, &cap, 0, 0);

    JscValue *r = jsc_string(out);
    free(out);
    jsc_value_free(v);
    return r;
}

JscValue *json_pretty(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "json.pretty() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    size_t cap = 256, len = 0;
    char *out = malloc(cap);
    out[0] = 0;

    json_stringify_into(v, &out, &len, &cap, 1, 0);

    JscValue *r = jsc_string(out);
    free(out);
    jsc_value_free(v);
    return r;
}

typedef struct {
    const char *src;
    size_t pos;
} JsonParser;

static void json_skip_ws(JsonParser *p) {
    while (p->src[p->pos] && isspace((unsigned char)p->src[p->pos])) p->pos++;
}

static JscValue *json_parse_value(JsonParser *p);

static JscValue *json_parse_string(JsonParser *p) {
    if (p->src[p->pos] != '"') return NULL;
    p->pos++;
    size_t cap = 64, len = 0;
    char *buf = malloc(cap);
    while (p->src[p->pos] && p->src[p->pos] != '"') {
        char c = p->src[p->pos++];
        if (c == '\\') {
            char e = p->src[p->pos++];
            switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                default: c = e; break;
            }
        }
        if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = c;
    }
    if (p->src[p->pos] == '"') p->pos++;
    buf[len] = 0;
    JscValue *r = jsc_string(buf);
    free(buf);
    return r;
}

static JscValue *json_parse_value(JsonParser *p) {
    json_skip_ws(p);
    char c = p->src[p->pos];
    if (c == '"') return json_parse_string(p);
    if (c == 't') { p->pos += 4; return jsc_bool(1); }
    if (c == 'f') { p->pos += 5; return jsc_bool(0); }
    if (c == 'n') { p->pos += 4; return jsc_vazio(); }
    if (c == '[') {
        p->pos++;
        JscValue *arr = jsc_array();
        json_skip_ws(p);
        if (p->src[p->pos] == ']') { p->pos++; return arr; }
        while (1) {
            JscValue *item = json_parse_value(p);
            jsc_array_push(arr, item);
            json_skip_ws(p);
            if (p->src[p->pos] == ',') { p->pos++; continue; }
            if (p->src[p->pos] == ']') { p->pos++; break; }
            break;
        }
        return arr;
    }
    if (c == '{') {
        p->pos++;
        JscValue *map = jsc_map();
        json_skip_ws(p);
        if (p->src[p->pos] == '}') { p->pos++; return map; }
        while (1) {
            json_skip_ws(p);
            JscValue *key = json_parse_string(p);
            json_skip_ws(p);
            if (p->src[p->pos] == ':') p->pos++;
            JscValue *val = json_parse_value(p);
            jsc_map_set(map, key, val);
            json_skip_ws(p);
            if (p->src[p->pos] == ',') { p->pos++; continue; }
            if (p->src[p->pos] == '}') { p->pos++; break; }
            break;
        }
        return map;
    }
    size_t start = p->pos;
    if (c == '-') p->pos++;
    while (isdigit((unsigned char)p->src[p->pos])) p->pos++;
    int is_float = 0;
    if (p->src[p->pos] == '.') { is_float = 1; p->pos++; while (isdigit((unsigned char)p->src[p->pos])) p->pos++; }
    if (p->src[p->pos] == 'e' || p->src[p->pos] == 'E') { is_float = 1; p->pos++; if (p->src[p->pos]=='+'||p->src[p->pos]=='-') p->pos++; while (isdigit((unsigned char)p->src[p->pos])) p->pos++; }
    size_t len = p->pos - start;
    char *buf = malloc(len + 1);
    memcpy(buf, p->src + start, len);
    buf[len] = 0;
    JscValue *r;
    if (is_float) r = jsc_float(atof(buf));
    else r = jsc_int(atoll(buf));
    free(buf);
    return r;
}

JscValue *json_parse(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "json.parse() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *s = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *src = (s->type == VAL_STRING && s->as.s) ? s->as.s : "";

    JsonParser p = { src, 0 };
    JscValue *r = json_parse_value(&p);
    jsc_value_free(s);
    return r ? r : jsc_vazio();
}

static JscValue *make_json_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, jsc_string("parse"),     jsc_native("parse",     json_parse));
    jsc_map_set(m, jsc_string("stringify"), jsc_native("stringify", json_stringify));
    jsc_map_set(m, jsc_string("pretty"),    jsc_native("pretty",    json_pretty));
    return m;
}

/* ---------- TIME COMPLETO ---------- */

JscValue *time_format(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "time.format() espera 2 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *t = eval(n->args.items[0], env, state);
    JscValue *f = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(t); jsc_value_free(f); return NULL; }

    time_t ts = (time_t)jsc_value_to_int(t);
    const char *fmt = (f->type == VAL_STRING && f->as.s) ? f->as.s : "";
    struct tm *tm = localtime(&ts);

    char out[256];
    strftime(out, sizeof(out), fmt, tm);

    jsc_value_free(t); jsc_value_free(f);
    return jsc_string(out);
}

JscValue *time_parse(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "time.parse() espera 2 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *s = eval(n->args.items[0], env, state);
    JscValue *f = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(s); jsc_value_free(f); return NULL; }

    const char *txt = (s->type == VAL_STRING && s->as.s) ? s->as.s : "";
    const char *fmt = (f->type == VAL_STRING && f->as.s) ? f->as.s : "";

    struct tm tm = {0};
    if (strptime(txt, fmt, &tm) == NULL) {
        runtime_error(state, "time.parse: formato nao bate", (Node *)n);
        jsc_value_free(s); jsc_value_free(f);
        return NULL;
    }
    time_t ts = mktime(&tm);

    jsc_value_free(s); jsc_value_free(f);
    return jsc_int((long long)ts);
}

JscValue *time_diff(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "time.diff() espera 2 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *a = eval(n->args.items[0], env, state);
    JscValue *b = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(a); jsc_value_free(b); return NULL; }
    long long r = jsc_value_to_int(a) - jsc_value_to_int(b);
    jsc_value_free(a); jsc_value_free(b);
    return jsc_int(r);
}

JscValue *time_add_days(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        runtime_error(state, "time.add_days() espera 2 argumentos", (Node *)n);
        return NULL;
    }
    JscValue *t = eval(n->args.items[0], env, state);
    JscValue *d = eval(n->args.items[1], env, state);
    if (state->had_error) { jsc_value_free(t); jsc_value_free(d); return NULL; }
    long long ts = jsc_value_to_int(t) + jsc_value_to_int(d) * 86400;
    jsc_value_free(t); jsc_value_free(d);
    return jsc_int(ts);
}

JscValue *time_day_of_week(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "time.day_of_week() espera 1 argumento", (Node *)n);
        return NULL;
    }
    JscValue *t = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    time_t ts = (time_t)jsc_value_to_int(t);
    jsc_value_free(t);

    static const char *dias[] = {"domingo", "segunda", "terca", "quarta", "quinta", "sexta", "sabado"};
    struct tm *tm = localtime(&ts);
    return jsc_string(dias[tm->tm_wday]);
}

JscValue *time_timestamp(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;
    return jsc_int((long long)time(NULL));
}

static JscValue *make_time_module(void) {
    JscValue *m = jsc_map();
    #define REG(name) jsc_map_set(m, jsc_string(#name), jsc_native(#name, time_##name))
    REG(now);
    REG(sleep);
    REG(clock);
    REG(format);
    REG(parse);
    REG(diff);
    REG(add_days);
    REG(day_of_week);
    REG(timestamp);
    #undef REG
    return m;
}


/* ============================================================
 * IMPORT_C — carrega biblioteca C dinamica
 * ============================================================ */

/* Estrutura que guarda a lib carregada */
typedef struct {
    void *handle;       /* dlopen handle */
    char *path;         /* caminho da lib */
} JscLibrary;

/* import_c(path) — carrega lib, devolve map com handle */
JscValue *native_import_c(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        runtime_error(state, "import_c() espera 1 argumento (caminho da lib)", (Node *)n);
        return NULL;
    }

    JscValue *v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    const char *path = (v->type == VAL_STRING && v->as.s) ? v->as.s : "";

    /* Carrega a lib */
    void *handle = dlopen(path, RTLD_LAZY);
    if (!handle) {
        char buf[512];
        snprintf(buf, sizeof(buf), "import_c: nao consegui carregar '%s': %s", path, dlerror());
        runtime_error(state, buf, (Node *)n);
        jsc_value_free(v);
        return NULL;
    }

    /* Cria a struct JscLibrary */
    JscLibrary *lib = (JscLibrary *)calloc(1, sizeof(JscLibrary));
    lib->handle = handle;
    lib->path   = strdup(path);

    /* Devolve um map com o handle (como int) e o path */
    JscValue *map = jsc_map();
    jsc_map_set(map, jsc_string("handle"), jsc_int((long long)(intptr_t)handle));
    jsc_map_set(map, jsc_string("path"),   jsc_string(path));
    jsc_map_set(map, jsc_string("__lib_ptr__"), jsc_int((long long)(intptr_t)lib));

    jsc_value_free(v);
    return map;
}

/* ============================================================
 * CALL_C — chama funcao C de uma lib carregada
 * ============================================================ */

/* Estrutura que descreve como chamar uma funcao C */
typedef struct {
    char *nome;         /* nome da funcao */
    char *tipo_ret;     /* "int", "float", "string", "void" */
    char *tipos_args;   /* "i", "f", "s" — string de tipos */
    int   n_args;
} JscCFunc;

/* ============================================================
 * CALL_C — chama funcao C manualmente
 * Suporta 3 tipos de assinatura (ABI):
 *   1) int  func(int)
 *   2) size_t func(char*)
 *   3) double func(double)
 * ============================================================ */

/* Assinaturas suportadas */
typedef int    (*fn_i_i)(int);
typedef size_t (*fn_i_p)(const char *);
typedef double (*fn_d_d)(double);
typedef int    (*fn_i_v)(void);

/* Chama funcao C a partir do nome + tipo + argumentos */
JscValue *native_call_c(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    /* Assinatura: call_c(lib, "nome", "tiporet", [args...])
     * tiporet:
     *   "v"   -> int func(void)
     *   "i"   -> int func(int)        ou  int func(int, int)
     *   "d"   -> double func(double)  ou  double func(double, double)
     *   "p"   -> size_t func(char*)
     *   "vp"  -> void func(char*)
     */
    if (n->args.count < 3) {
        runtime_error(state, "call_c() espera ao menos 3 args: lib, nome, tiporet, [args]", (Node *)n);
        return NULL;
    }

    /* 1. Handle da lib */
    JscValue *lib_val = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;
    if (lib_val->type != VAL_MAP) {
        runtime_error(state, "call_c: primeiro arg deve ser uma lib de import_c()", (Node *)n);
        jsc_value_free(lib_val);
        return NULL;
    }
    JscValue *handle_val = jsc_map_get(lib_val, jsc_string("handle"));
    if (!handle_val) {
        runtime_error(state, "call_c: lib sem handle", (Node *)n);
        jsc_value_free(lib_val);
        return NULL;
    }
    void *handle = (void *)(intptr_t)jsc_value_to_int(handle_val);
    jsc_value_free(lib_val);

    /* 2. Nome da funcao */
    JscValue *nome_val = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;
    const char *nome = (nome_val->type == VAL_STRING && nome_val->as.s) ? nome_val->as.s : "";
    char *nome_copy = strdup(nome);
    jsc_value_free(nome_val);

    /* 3. Tipo de retorno */
    JscValue *tipo_val = eval(n->args.items[2], env, state);
    if (state->had_error) { free(nome_copy); return NULL; }
    char *tipo = strdup((tipo_val->type == VAL_STRING && tipo_val->as.s) ? tipo_val->as.s : "");
    jsc_value_free(tipo_val);
    char t0 = tipo[0];
    char t1 = tipo[1];

    /* 4. Busca o simbolo */
    void *sym = dlsym(handle, nome_copy);
    if (!sym) {
        char buf[512];
        snprintf(buf, sizeof(buf), "call_c: '%s' nao encontrada: %s", nome_copy, dlerror());
        runtime_error(state, buf, (Node *)n);
        free(nome_copy);
        return NULL;
    }

    int n_args_reais = (int)n->args.count - 3;
    JscValue *result = NULL;


    /* ============================================================
     * Casos de chamada
     * ============================================================ */

    /* "Vs" — char* func(int) — ex: OpenSSL_version */
    if (t0 == 'V' && t1 == 's') {
        if (n_args_reais != 1) {
            runtime_error(state, "call_c 'Vs' espera 1 arg int", (Node *)n);
        } else {
            JscValue *a = eval(n->args.items[3], env, state);
            int x = (int)jsc_value_to_int(a);
            jsc_value_free(a);
            typedef const char *(*f_t)(int);
            f_t f = (f_t)sym;
            const char *r = f(x);
            result = jsc_string(r ? r : "");
        }
    }
    /* "V" — char* func(void) — ex: sqlite3_libversion */
    else if (t0 == 'V' && t1 == '\0') {
        typedef const char *(*f_t)(void);
        f_t f = (f_t)sym;
        const char *r = f();
        result = jsc_string(r ? r : "");
    }
    /* "v" — int func(void) */
    else if (t0 == 'v' && t1 == '\0') {
        typedef int (*f_t)(void);
        f_t f = (f_t)sym;
        result = jsc_int((long long)f());
    }
    /* "vp" — void func(char*) */
    else if (t0 == 'v' && t1 == 'p') {
        if (n_args_reais != 1) {
            runtime_error(state, "call_c 'vp' espera 1 arg string", (Node *)n);
        } else {
            JscValue *a = eval(n->args.items[3], env, state);
            char *s = strdup((a->type == VAL_STRING && a->as.s) ? a->as.s : "");
            jsc_value_free(a);
            typedef void (*f_t)(char *);
            f_t f = (f_t)sym;
            f(s);
            free(s);
            result = jsc_vazio();
        }
    }
    /* "p" — size_t func(char*) */
    else if (t0 == 'p' && t1 == '\0') {
        if (n_args_reais != 1) {
            runtime_error(state, "call_c 'p' espera 1 arg string", (Node *)n);
        } else {
            JscValue *a = eval(n->args.items[3], env, state);
            char *s = strdup((a->type == VAL_STRING && a->as.s) ? a->as.s : "");
            jsc_value_free(a);
            typedef size_t (*f_t)(const char *);
            f_t f = (f_t)sym;
            result = jsc_int((long long)f(s));
            free(s);
        }
    }
    /* "ss" — int func(char*, char*) — ex: strcmp */
    else if (t0 == 's' && t1 == 's' && strlen(tipo) == 2) {
        if (n_args_reais != 2) {
            runtime_error(state, "call_c 'ss' espera 2 args string", (Node *)n);
        } else {
            JscValue *a = eval(n->args.items[3], env, state);
            JscValue *b = eval(n->args.items[4], env, state);
            char *s1 = strdup((a->type == VAL_STRING && a->as.s) ? a->as.s : "");
            char *s2 = strdup((b->type == VAL_STRING && b->as.s) ? b->as.s : "");
            jsc_value_free(a);
            jsc_value_free(b);
            typedef int (*f_t)(const char *, const char *);
            f_t f = (f_t)sym;
            result = jsc_int((long long)f(s1, s2));
            free(s1);
            free(s2);
        }
    }
    /* "s" — int func(char*) — alias de "p" mas mais legivel */
    else if (t0 == 's' && t1 == '\0') {
        if (n_args_reais != 1) {
            runtime_error(state, "call_c 's' espera 1 arg string", (Node *)n);
        } else {
            JscValue *a = eval(n->args.items[3], env, state);
            char *s = strdup((a->type == VAL_STRING && a->as.s) ? a->as.s : "");
            jsc_value_free(a);
            typedef int (*f_t)(const char *);
            f_t f = (f_t)sym;
            result = jsc_int((long long)f(s));
            free(s);
        }
    }
    /* "ssi" — int func(char*, char*, int) — ex: strncmp */
    else if (t0 == 's' && t1 == 's' && strlen(tipo) == 3 && tipo[2] == 'i') {
        if (n_args_reais != 3) {
            runtime_error(state, "call_c 'ssi' espera 3 args: string, string, int", (Node *)n);
        } else {
            JscValue *a = eval(n->args.items[3], env, state);
            JscValue *b = eval(n->args.items[4], env, state);
            JscValue *d = eval(n->args.items[5], env, state);
            char *s1 = strdup((a->type == VAL_STRING && a->as.s) ? a->as.s : "");
            char *s2 = strdup((b->type == VAL_STRING && b->as.s) ? b->as.s : "");
            int   nn = (int)jsc_value_to_int(d);
            jsc_value_free(a);
            jsc_value_free(b);
            jsc_value_free(d);
            typedef int (*f_t)(const char *, const char *, size_t);
            f_t f = (f_t)sym;
            result = jsc_int((long long)f(s1, s2, (size_t)nn));
            free(s1);
            free(s2);
        }
    }
    /* "i" — int func(int)  ou  int func(int, int) */
    else if (t0 == 'i' && t1 == '\0') {
        if (n_args_reais == 1) {
            JscValue *a = eval(n->args.items[3], env, state);
            int x = (int)jsc_value_to_int(a);
            jsc_value_free(a);
            typedef int (*f_t)(int);
            f_t f = (f_t)sym;
            result = jsc_int((long long)f(x));
        } else if (n_args_reais == 2) {
            JscValue *a = eval(n->args.items[3], env, state);
            JscValue *b = eval(n->args.items[4], env, state);
            int x = (int)jsc_value_to_int(a);
            int y = (int)jsc_value_to_int(b);
            jsc_value_free(a);
            jsc_value_free(b);
            typedef int (*f_t)(int, int);
            f_t f = (f_t)sym;
            result = jsc_int((long long)f(x, y));
        } else {
            runtime_error(state, "call_c 'i' espera 1 ou 2 args int", (Node *)n);
        }
    }
    /* "d" — double func(double)  ou  double func(double, double) */
    else if (t0 == 'd' && t1 == '\0') {
        if (n_args_reais == 1) {
            JscValue *a = eval(n->args.items[3], env, state);
            double x = jsc_value_to_float(a);
            jsc_value_free(a);
            typedef double (*f_t)(double);
            f_t f = (f_t)sym;
            result = jsc_float(f(x));
        } else if (n_args_reais == 2) {
            JscValue *a = eval(n->args.items[3], env, state);
            JscValue *b = eval(n->args.items[4], env, state);
            double x = jsc_value_to_float(a);
            double y = jsc_value_to_float(b);
            jsc_value_free(a);
            jsc_value_free(b);
            typedef double (*f_t)(double, double);
            f_t f = (f_t)sym;
            result = jsc_float(f(x, y));
        } else {
            runtime_error(state, "call_c 'd' espera 1 ou 2 args float", (Node *)n);
        }
    }
    else {
        char buf[128];
        snprintf(buf, sizeof(buf), "call_c: tipo '%s' nao suportado (use v, vp, p, i, d)", tipo);
        runtime_error(state, buf, (Node *)n);
    }

    free(nome_copy);
    free(tipo);
    return result ? result : jsc_vazio();
}

JscValue *eval_import(Node *node, Env *env, EvalState *state) {
    NodeImport *n = (NodeImport *)node;
    (void)state;

    if (strcmp(n->module, "math") == 0) {
        if (env_get(env, "math")) return jsc_vazio();
        env_define(env, "math", make_math_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "fs") == 0) {
        if (env_get(env, "fs")) return jsc_vazio();
        env_define(env, "fs", make_fs_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "crypto") == 0) {
        if (env_get(env, "crypto")) return jsc_vazio();
        env_define(env, "crypto", make_crypto_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "net") == 0) {
        if (env_get(env, "net")) return jsc_vazio();
        env_define(env, "net", make_net_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "time") == 0) {
        if (env_get(env, "time")) return jsc_vazio();
        env_define(env, "time", make_time_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "os") == 0) {
        if (env_get(env, "os")) return jsc_vazio();
        env_define(env, "os", make_os_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "string") == 0) {
        if (env_get(env, "string")) return jsc_vazio();
        env_define(env, "string", make_string_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "regex") == 0) {
        if (env_get(env, "regex")) return jsc_vazio();
        env_define(env, "regex", make_regex_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "zip") == 0) {
        if (env_get(env, "zip")) return jsc_vazio();
        env_define(env, "zip", make_zip_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "csv") == 0) {
        if (env_get(env, "csv")) return jsc_vazio();
        env_define(env, "csv", make_csv_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "hack") == 0) {
        if (env_get(env, "hack")) return jsc_vazio();
        env_define(env, "hack", make_hack_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "json") == 0) {
        if (env_get(env, "json")) return jsc_vazio();
        env_define(env, "json", make_json_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "proc") == 0) {
        if (env_get(env, "proc")) return jsc_vazio();
        env_define(env, "proc", make_proc_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "py") == 0) {
        if (env_get(env, "py")) return jsc_vazio();
        env_define(env, "py", make_py_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "tensor") == 0) {
        if (env_get(env, "tensor")) return jsc_vazio();
        env_define(env, "tensor", make_tensor_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "embedding") == 0) {
        if (env_get(env, "embedding")) return jsc_vazio();
        env_define(env, "embedding", make_embedding_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "attention") == 0) {
        if (env_get(env, "attention")) return jsc_vazio();
        env_define(env, "attention", make_attention_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "multihead") == 0) {
        if (env_get(env, "multihead")) return jsc_vazio();
        env_define(env, "multihead", make_multihead_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "block") == 0) {
        if (env_get(env, "block")) return jsc_vazio();
        env_define(env, "block", make_block_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "model") == 0) {
        if (env_get(env, "model")) return jsc_vazio();
        env_define(env, "model", make_model_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "transformer") == 0) {
        if (env_get(env, "transformer")) return jsc_vazio();
        env_define(env, "transformer", make_transformer_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "loss") == 0) {
        if (env_get(env, "loss")) return jsc_vazio();
        env_define(env, "loss", make_loss_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "backprop") == 0) {
        if (env_get(env, "backprop")) return jsc_vazio();
        env_define(env, "backprop", make_backprop_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "backprop_ff") == 0) {
        if (env_get(env, "backprop_ff")) return jsc_vazio();
        env_define(env, "backprop_ff", make_backprop_ff_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "backprop_ln") == 0) {
        if (env_get(env, "backprop_ln")) return jsc_vazio();
        env_define(env, "backprop_ln", make_backprop_ln_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "backprop_attn") == 0) {
        if (env_get(env, "backprop_attn")) return jsc_vazio();
        env_define(env, "backprop_attn", make_backprop_attn_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "optimizer") == 0) {
        if (env_get(env, "optimizer")) return jsc_vazio();
        env_define(env, "optimizer", make_optimizer_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "forward_cache") == 0) {
        if (env_get(env, "forward_cache")) return jsc_vazio();
        env_define(env, "forward_cache", make_forward_cache_module());
        return jsc_vazio();
    }

    if (strcmp(n->module, "backprop_layer") == 0) {
        if (env_get(env, "backprop_layer")) return jsc_vazio();
        env_define(env, "backprop_layer", make_backprop_layer_module());
        return jsc_vazio();
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "modulo '%s' nao encontrado", n->module);
    runtime_error(state, buf, node);
    return NULL;
}

/* ============================================================
 * Acesso de campo: math.pi, pessoa.nome
 * ============================================================ */

JscValue *eval_field(Node *node, Env *env, EvalState *state) {
    NodeField *n = (NodeField *)node;

    JscValue *obj = eval(n->object, env, state);
    if (state->had_error) return NULL;

    JscValue *result = NULL;

    if (obj->type == VAL_MAP) {
        JscValue key;
        key.type = VAL_STRING;
        key.as.s = (char *)n->field;

        JscValue *found = jsc_map_get(obj, &key);
        if (found) {
            result = jsc_value_copy(found);
        } else {
            char buf[128];
            snprintf(buf, sizeof(buf), "campo '%s' nao existe no map", n->field);
            runtime_error(state, buf, node);
        }
    }
    else if (obj->type == VAL_INSTANCE) {
        /* 1. Procura nos campos da instancia */
        Env *fields = (Env *)obj->as.inst.fields;
        JscValue *campo = NULL;
        if (fields) {
            campo = env_get(fields, n->field);
        }

        if (campo) {
            result = jsc_value_copy(campo);
        } else {
            /* 2. Procura como metodo na classe (e nos pais) */
            JscValue *klass = (JscValue *)obj->as.inst.klass;
            NodeFuncDecl *found_method = NULL;
            JscValue *found_field = NULL;

            if (class_find_member(klass, n->field, &found_field, &found_method)) {
                if (found_method) {
                    Env *bind_env = env_new(klass->as.cls.closure);
                    env_define(bind_env, "this", jsc_value_copy(obj));

                    JscValue *fn = jsc_vazio();
                    fn->type = VAL_FUNCTION;
                    fn->as.func.name    = strdup(found_method->name);
                    fn->as.func.body    = found_method->body;
                    fn->as.func.params  = &found_method->params;
                    fn->as.func.closure = bind_env;
                    fn->as.func.native  = NULL;
                    result = fn;
                } else {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "campo '%s' existe mas nao foi inicializado", n->field);
                    runtime_error(state, buf, node);
                }
            } else {
                char buf[128];
                snprintf(buf, sizeof(buf), "campo ou metodo '%s' nao existe", n->field);
                runtime_error(state, buf, node);
            }
        }
    }
    else {
        runtime_error(state, "acesso de campo so funciona em map ou instancia", node);
    }

    jsc_value_free(obj);
    return result ? result : jsc_vazio();
}

/* ============================================================
 * BLOCO D.1 — Classes
 * ============================================================ */

JscValue *eval_class_decl(Node *node, Env *env, EvalState *state) {
    NodeClassDecl *n = (NodeClassDecl *)node;

    JscValue *cls = jsc_class(n->name, &n->fields, &n->methods, env);

    /* Se tem extends, resolve a classe pai */
    if (n->parent) {
        JscValue *parent_cls = env_get(env, n->parent);
        if (!parent_cls || parent_cls->type != VAL_CLASS) {
            char buf[128];
            snprintf(buf, sizeof(buf), "classe pai '%s' nao encontrada", n->parent);
            runtime_error(state, buf, node);
            jsc_value_free(cls);
            return NULL;
        }
        cls->as.cls.parent = jsc_value_copy(parent_cls);
    }

    env_define(env, n->name, cls);
    return jsc_vazio();
}

/* ============================================================
 * TRY/CATCH/FINALLY
 * ============================================================ */

JscValue *eval_try(Node *node, Env *env, EvalState *state) {
    NodeTry *n = (NodeTry *)node;

    /* Salva o estado atual */
    int saved_in_try = state->in_try;

    /* Entra no modo try — erros não imprimem */
    state->in_try = 1;
    state->had_error = 0;

    /* Executa o try */
    if (n->try_block) {
        eval(n->try_block, env, state);
    }

    int try_falhou = state->had_error;

    /* Se o try falhou, executa o catch */
    if (try_falhou && n->catch_block) {
        Env *catch_env = env_new(env);

        /* Define a variável do catch com a mensagem de erro */
        if (n->catch_var) {
            char msg[256];
            snprintf(msg, sizeof(msg), "%s", state->error_message);
            env_define(catch_env, n->catch_var, jsc_string(msg));
        }

        /* Reseta o had_error pra que o catch possa rodar */
        state->had_error = 0;

        eval(n->catch_block, catch_env, state);

        env_free(catch_env);
    }

    /* Sai do modo try ANTES do finally, pra finally poder imprimir erros */
    state->in_try = saved_in_try;

    /* finally SEMPRE roda */
    if (n->finally_block) {
        int before_finally = state->had_error;
        state->had_error = 0;
        eval(n->finally_block, env, state);
        if (!state->had_error) {
            state->had_error = before_finally;
        }
    }

    /* Se try falhou, mas NÃO tem catch, propaga o erro */
    if (try_falhou && !n->catch_block) {
        state->had_error = 1;
        /* Se não tava em try antes, imprime agora */
        if (!saved_in_try) {
            fprintf(stderr, "JSC$+ erro: %s\n", state->error_message);
        }
    }

    return jsc_vazio();
}

/* ============================================================
 * THREAD.1 — Infraestrutura de threads
 * ============================================================ */

static int thread_counter = 0;

/* Chamado pelo GC pra marcar o resultado da thread */
void gc_mark_thread(void *thread_ptr) {
    JscThread *t = (JscThread *)thread_ptr;
    if (!t) return;
    if (t->result) {
        gc_mark(t->result);
    }
    if (t->closure) {
        gc_mark_env(t->closure);
    }
    for (size_t i = 0; i < t->n_args; i++) {
        if (t->args[i]) {
            gc_mark(t->args[i]);
        }
    }
}

/* Rotina executada pela thread */
static void *thread_entry(void *arg) {
    JscThread *t = (JscThread *)arg;

    gc_in_thread = 1;

    /* Cria um state próprio pra essa thread */
    EvalState state = {0};
    state.return_value = NULL;
    state.error_message[0] = '\0';
    state.in_try = 0;

    /* Cria env do método */
    Env *call_env = env_new(t->closure);

    /* Bota parâmetros */
    for (size_t i = 0; i < t->n_args; i++) {
        Node *param = t->body->params.items[i];
        NodeIdent *pi = (NodeIdent *)param;
        env_define(call_env, pi->name, t->args[i]);
    }

    /* Executa o body */
    eval(t->body->body, call_env, &state);

    /* Captura o resultado — COPIA pra nao depender do call_env */
    if (state.has_return && state.return_value) {
        t->result = jsc_value_copy(state.return_value);
    } else {
        t->result = jsc_vazio();
    }

    t->had_error = state.had_error;
    if (state.had_error) {
        snprintf(t->error_msg, sizeof(t->error_msg), "%s", state.error_message);
    }

    t->done = 1;

    env_free(call_env);

    __sync_sub_and_fetch(&gc_active_threads, 1);

    return NULL;
}

/* spawn(func, arg1, arg2, ...) → thread handle */
JscValue *native_spawn(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count < 1) {
        runtime_error(state, "spawn() espera ao menos 1 argumento (funcao)", (Node *)n);
        return NULL;
    }

    /* Avalia o primeiro argumento (a função) */
    JscValue *fn = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    if (fn->type != VAL_FUNCTION) {
        runtime_error(state, "spawn(): primeiro argumento nao e funcao", (Node *)n);
        jsc_value_free(fn);
        return NULL;
    }

    if (fn->as.func.native != NULL) {
        runtime_error(state, "spawn(): nao suporta funcoes nativas ainda", (Node *)n);
        jsc_value_free(fn);
        return NULL;
    }

    /* Prepara args (do segundo em diante) */
    size_t n_args = n->args.count - 1;
    JscValue **args = (JscValue **)malloc(sizeof(JscValue *) * (n_args ? n_args : 1));
    for (size_t i = 0; i < n_args; i++) {
        args[i] = eval(n->args.items[i + 1], env, state);
        if (state->had_error) {
            for (size_t j = 0; j < i; j++) jsc_value_free(args[j]);
            free(args);
            jsc_value_free(fn);
            return NULL;
        }
    }

    /* Cria struct thread */
    JscThread *t = (JscThread *)calloc(1, sizeof(JscThread));
    t->id        = __sync_add_and_fetch(&thread_counter, 1);
    t->body      = NULL;  /* vamos pegar a "funcao" do JscValue */
    t->args      = args;
    t->n_args    = n_args;
    t->closure   = fn->as.func.closure;
    t->done      = 0;
    t->result    = NULL;

    /* Preciso do NodeFuncDecl pra pegar body e params.
     * Mas o JscValue só guarda "body" (NodeBlock) e "params" (NodeList).
     * Vou construir uma struct temporária. */
    NodeFuncDecl *fake_fd = (NodeFuncDecl *)calloc(1, sizeof(NodeFuncDecl));
    fake_fd->name = strdup(fn->as.func.name ? fn->as.func.name : "?");
    fake_fd->body = fn->as.func.body;
    fake_fd->params = *fn->as.func.params;
    t->body = fake_fd;

    /* Incrementa contador de threads ativas */
    __sync_add_and_fetch(&gc_active_threads, 1);

    /* Cria a thread */
    if (pthread_create(&t->handle, NULL, thread_entry, t) != 0) {
        __sync_sub_and_fetch(&gc_active_threads, 1);
        runtime_error(state, "spawn(): falha ao criar thread", (Node *)n);
        free(args);
        free(t);
        jsc_value_free(fn);
        return NULL;
    }

    jsc_value_free(fn);

    /* Devolve handle */
    JscValue *handle = jsc_thread(t);
    return handle;
}

/* Helper: cast do void* pra JscThread* */

/* ============================================================
 * BREAK / CONTINUE
 * ============================================================ */

JscValue *eval_break(Node *node, Env *env, EvalState *state) {
    (void)node; (void)env;
    state->has_break = 1;
    return jsc_vazio();
}

JscValue *eval_continue(Node *node, Env *env, EvalState *state) {
    (void)node; (void)env;
    state->has_continue = 1;
    return jsc_vazio();
}


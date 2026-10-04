/* SHA-256, HMAC-SHA256 and PBKDF2, because a password must never be
 * stored and must never be cheap to guess.
 *
 * Written out rather than linked because this program is C99 with no
 * dependencies and is built on three operating systems; adding OpenSSL
 * to get one hash would cost more than the hash.  Everything here is
 * checked against published vectors in court_pw_selftest(), which runs
 * before the server will accept a single login -- untested crypto is
 * worse than none, because it looks like something.
 */
#include "court.h"
#include <string.h>
#include <stdio.h>

typedef struct { unsigned int h[8]; unsigned long long n; unsigned char b[64]; size_t k; } Sha;

static const unsigned int K[64] = {
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

#define ROR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))

static void blk(Sha *s, const unsigned char *p)
{
    unsigned int w[64], a,b,c,d,e,f,g,h,t1,t2;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((unsigned int)p[i*4]<<24)|((unsigned int)p[i*4+1]<<16)
             | ((unsigned int)p[i*4+2]<<8)|((unsigned int)p[i*4+3]);
    for (i = 16; i < 64; i++) {
        unsigned int s0 = ROR(w[i-15],7) ^ ROR(w[i-15],18) ^ (w[i-15] >> 3);
        unsigned int s1 = ROR(w[i-2],17) ^ ROR(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a=s->h[0];b=s->h[1];c=s->h[2];d=s->h[3];
    e=s->h[4];f=s->h[5];g=s->h[6];h=s->h[7];
    for (i = 0; i < 64; i++) {
        unsigned int S1 = ROR(e,6) ^ ROR(e,11) ^ ROR(e,25);
        unsigned int ch = (e & f) ^ ((~e) & g);
        unsigned int S0 = ROR(a,2) ^ ROR(a,13) ^ ROR(a,22);
        unsigned int mj = (a & b) ^ (a & c) ^ (b & c);
        t1 = h + S1 + ch + K[i] + w[i];
        t2 = S0 + mj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;
    s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}

static void sha_init(Sha *s)
{
    s->h[0]=0x6a09e667;s->h[1]=0xbb67ae85;s->h[2]=0x3c6ef372;s->h[3]=0xa54ff53a;
    s->h[4]=0x510e527f;s->h[5]=0x9b05688c;s->h[6]=0x1f83d9ab;s->h[7]=0x5be0cd19;
    s->n = 0; s->k = 0;
}

static void sha_add(Sha *s, const unsigned char *p, size_t n)
{
    s->n += (unsigned long long)n * 8;
    while (n--) {
        s->b[s->k++] = *p++;
        if (s->k == 64) { blk(s, s->b); s->k = 0; }
    }
}

static void sha_end(Sha *s, unsigned char out[32])
{
    unsigned long long n = s->n;
    int i;
    unsigned char pad = 0x80;
    sha_add(s, &pad, 1);
    pad = 0;
    while (s->k != 56) sha_add(s, &pad, 1);
    for (i = 7; i >= 0; i--) {
        unsigned char c = (unsigned char)(n >> (i * 8));
        s->b[s->k++] = c;
        if (s->k == 64) { blk(s, s->b); s->k = 0; }
    }
    for (i = 0; i < 8; i++) {
        out[i*4]   = (unsigned char)(s->h[i] >> 24);
        out[i*4+1] = (unsigned char)(s->h[i] >> 16);
        out[i*4+2] = (unsigned char)(s->h[i] >> 8);
        out[i*4+3] = (unsigned char)(s->h[i]);
    }
}

void court_sha256(const void *p, size_t n, unsigned char out[32])
{
    Sha s; sha_init(&s); sha_add(&s, (const unsigned char *)p, n); sha_end(&s, out);
}

static void hmac(const unsigned char *key, size_t klen,
                 const unsigned char *msg, size_t mlen, unsigned char out[32])
{
    unsigned char k[64], ip[64], op[64], in[32];
    Sha s;
    size_t i;
    memset(k, 0, sizeof k);
    if (klen > 64) court_sha256(key, klen, k);
    else memcpy(k, key, klen);
    for (i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
    sha_init(&s); sha_add(&s, ip, 64); sha_add(&s, msg, mlen); sha_end(&s, in);
    sha_init(&s); sha_add(&s, op, 64); sha_add(&s, in, 32); sha_end(&s, out);
}

/* PBKDF2-HMAC-SHA256, one block, which is all a 32-byte key needs. */
void court_pbkdf2(const char *pw, const unsigned char *salt, size_t slen,
                  unsigned long rounds, unsigned char out[32])
{
    unsigned char t[32], u[32], blk1[64];
    unsigned long i;
    size_t j;
    if (slen > 60) slen = 60;
    memcpy(blk1, salt, slen);
    blk1[slen] = 0; blk1[slen+1] = 0; blk1[slen+2] = 0; blk1[slen+3] = 1;
    hmac((const unsigned char *)pw, strlen(pw), blk1, slen + 4, u);
    memcpy(t, u, 32);
    for (i = 1; i < rounds; i++) {
        hmac((const unsigned char *)pw, strlen(pw), u, 32, u);
        for (j = 0; j < 32; j++) t[j] ^= u[j];
    }
    memcpy(out, t, 32);
}

/* --- the check that has to pass before anyone can log in ----------- */

static int same(const unsigned char *a, const char *hex)
{
    char got[65];
    int i;
    for (i = 0; i < 32; i++) sprintf(got + i*2, "%02x", a[i]);
    got[64] = 0;
    return strcmp(got, hex) == 0;
}

int court_pw_selftest(void)
{
    unsigned char o[32];
    unsigned char key[20];
    int i, ok = 1;

    court_sha256("abc", 3, o);
    ok &= same(o, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    court_sha256("", 0, o);
    ok &= same(o, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    court_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, o);
    ok &= same(o, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    for (i = 0; i < 20; i++) key[i] = 0x0b;
    hmac(key, 20, (const unsigned char *)"Hi There", 8, o);
    ok &= same(o, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");

    court_pbkdf2("password", (const unsigned char *)"salt", 4, 1, o);
    ok &= same(o, "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    court_pbkdf2("password", (const unsigned char *)"salt", 4, 2, o);
    ok &= same(o, "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    return ok;
}

#include <stdint.h>
#include <quanta/account.h>
#include <quanta/arch.h>

#define ACCOUNT_LBA 256U
#define ACCOUNT_SIZE 512U
#define FLAG_TEMPORARY 1U
#define ACCOUNT_VERSION 1U

static uint32_t rotr(uint32_t value, uint32_t bits) { return (value >> bits) | (value << (32U - bits)); }
static const uint32_t constants[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
static void hash(const uint8_t *input, uint32_t length, uint8_t *output) {
    uint8_t block[128]; uint32_t words[64], state[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    uint32_t offset, i, a, b, c, d, e, f, g, h, t1, t2; uint64_t bits = (uint64_t)length * 8U;
    uint32_t total = ((length + 9U + 63U) / 64U) * 64U;
    for (offset = 0; offset < total; offset += 64U) {
        for (i = 0; i < 128U; ++i) block[i] = 0;
        for (i = 0; i < 64U; ++i) block[i] = offset + i < length ? input[offset + i] : 0;
        if (offset <= length && length - offset < 64U) block[length - offset] = 0x80;
        if (offset + 64U == total) for (i = 0; i < 8U; ++i) block[56U + i] = (uint8_t)(bits >> (56U - i * 8U));
        for (i = 0; i < 16U; ++i) words[i] = ((uint32_t)block[i*4]<<24)|((uint32_t)block[i*4+1]<<16)|((uint32_t)block[i*4+2]<<8)|block[i*4+3];
        for (; i < 64U; ++i) { uint32_t s0=rotr(words[i-15],7)^rotr(words[i-15],18)^(words[i-15]>>3); uint32_t s1=rotr(words[i-2],17)^rotr(words[i-2],19)^(words[i-2]>>10); words[i]=words[i-16]+s0+words[i-7]+s1; }
        a=state[0];b=state[1];c=state[2];d=state[3];e=state[4];f=state[5];g=state[6];h=state[7];
        for (i=0;i<64;++i) { uint32_t s1=rotr(e,6)^rotr(e,11)^rotr(e,25); uint32_t choose=(e&f)^((~e)&g); t1=h+s1+choose+constants[i]+words[i]; uint32_t s0=rotr(a,2)^rotr(a,13)^rotr(a,22); uint32_t majority=(a&b)^(a&c)^(b&c); t2=s0+majority; h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2; }
        state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    }
    for (i=0;i<8;++i) { output[i*4]=(uint8_t)(state[i]>>24); output[i*4+1]=(uint8_t)(state[i]>>16); output[i*4+2]=(uint8_t)(state[i]>>8); output[i*4+3]=(uint8_t)state[i]; }
}
static int equal(const uint8_t *a, const uint8_t *b, uint32_t length) { uint8_t difference=0; uint32_t i; for(i=0;i<length;++i) difference |= a[i]^b[i]; return difference==0; }
static uint32_t crc32(const uint8_t *data, uint32_t length) { uint32_t crc=0xffffffffU,index,bit; for(index=0;index<length;++index){crc^=data[index];for(bit=0;bit<8U;++bit)crc=(crc>>1)^(0xedb88320U&(0U-(crc&1U)));}return ~crc; }
static int record_valid(const uint8_t *record) {
    return record[0]=='Q' && record[1]=='A' && record[2]=='C' && record[3]=='C' &&
        record[4]=='O' && record[5]=='U' && record[6]=='N' && record[7]=='T' &&
        *(uint16_t *)(record + 8) == ACCOUNT_VERSION &&
        *(uint32_t *)(record + 68) == crc32(record, 68U);
}
int quanta_account_verify(const char *password, uint64_t length, uint32_t *temporary) {
    uint8_t first[ACCOUNT_SIZE], second[ACCOUNT_SIZE], digest[32], input[80]; uint8_t *record; uint64_t generation, other_generation; uint32_t i;
    if (password == 0 || temporary == 0 || length > 64U) return -1;
    if (quanta_arch_disk_read(ACCOUNT_LBA, first) != 0 || quanta_arch_disk_read(ACCOUNT_LBA + 1U, second) != 0) return -1;
    record = first; generation = 0; other_generation = 0;
    if (first[0]=='Q' && first[1]=='A' && first[2]=='C' && first[3]=='C' && first[4]=='O' && first[5]=='U' && first[6]=='N' && first[7]=='T') generation = *(uint64_t *)(first+12);
    if (second[0]=='Q' && second[1]=='A' && second[2]=='C' && second[3]=='C' && second[4]=='O' && second[5]=='U' && second[6]=='N' && second[7]=='T') other_generation = *(uint64_t *)(second+12);
    if (other_generation > generation) { record=second; generation=other_generation; }
    if (generation == 0) return -1;
    for (i=0;i<16;++i) input[i]=record[20+i];
    for (i=0;i<length;++i) input[16+i]=(uint8_t)password[i];
    hash(input, 16U + (uint32_t)length, digest);
    *temporary = record[10] & FLAG_TEMPORARY;
    return equal(digest, record+36, 32U) ? 0 : -1;
}
int quanta_account_change(const char *password, uint64_t length) {
    uint8_t first[ACCOUNT_SIZE], second[ACCOUNT_SIZE], record[ACCOUNT_SIZE];
    uint8_t input[80], digest[32], *active, *target; uint64_t generation;
    uint32_t active_lba, target_lba, index, temporary;
    if (password == 0 || length == 0U || length > 64U) return -1;
    if (quanta_arch_disk_read(ACCOUNT_LBA, first) != 0 ||
        quanta_arch_disk_read(ACCOUNT_LBA + 1U, second) != 0) return -1;
    if (!record_valid(first) && !record_valid(second)) return -1;
    active = record_valid(first) ? first : second;
    active_lba = active == first ? ACCOUNT_LBA : ACCOUNT_LBA + 1U;
    if (record_valid(first) && record_valid(second) && *(uint64_t *)(second + 12) > *(uint64_t *)(first + 12)) {
        active = second; active_lba = ACCOUNT_LBA + 1U;
    }
    target = active_lba == ACCOUNT_LBA ? second : first;
    target_lba = active_lba == ACCOUNT_LBA ? ACCOUNT_LBA + 1U : ACCOUNT_LBA;
    generation = *(uint64_t *)(active + 12) + 1U;
    for (index = 0; index < 16U; ++index) input[index] = active[20U + index] ^ (uint8_t)(generation >> ((index % 8U) * 8U));
    for (index = 0; index < length; ++index) input[16U + index] = (uint8_t)password[index];
    hash(input, 16U + (uint32_t)length, digest);
    for (index = 0; index < ACCOUNT_SIZE; ++index) record[index] = 0;
    record[0]='Q'; record[1]='A'; record[2]='C'; record[3]='C'; record[4]='O'; record[5]='U'; record[6]='N'; record[7]='T';
    *(uint16_t *)(record + 8) = ACCOUNT_VERSION; *(uint16_t *)(record + 10) = 0;
    *(uint64_t *)(record + 12) = generation;
    for (index = 0; index < 16U; ++index) record[20U + index] = input[index];
    for (index = 0; index < 32U; ++index) record[36U + index] = digest[index];
    *(uint32_t *)(record + 68) = crc32(record, 68U);
    if (quanta_arch_disk_write(target_lba, record) != 0 ||
        quanta_arch_disk_read(target_lba, target) != 0 || !record_valid(target) ||
        *(uint64_t *)(target + 12) != generation || !equal(target + 36, digest, 32U)) return -1;
    temporary = 0;
    return quanta_account_verify(password, length, &temporary);
}

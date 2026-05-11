// Central registry for pikachu chime clips. At each hour, one is picked at random.
// To add another clip: generate pikachuN.h with `xxd -i pikachuN.wav > pikachuN.h`,
// then #include it below and add its array + length to the two registry arrays.

#include "pikachu1.h"
#include "pikachu2.h"
#include "pikachu3.h"

const unsigned char* const pikachu_clips[] = {
    pikachu1_wav,
    pikachu2_wav,
    pikachu3_wav,
};
const unsigned int pikachu_clip_lens[] = {
    pikachu1_wav_len,
    pikachu2_wav_len,
    pikachu3_wav_len,
};
#define PIKACHU_CLIP_COUNT (sizeof(pikachu_clips) / sizeof(pikachu_clips[0]))

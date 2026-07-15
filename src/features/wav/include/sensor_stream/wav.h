#ifndef SENSOR_STREAM_WAV_H
#define SENSOR_STREAM_WAV_H

/*
 * SNS-04 (WAV): wraps already-captured PCM16 mono 8kHz sample bytes in a
 * canonical 44-byte RIFF/WAVE header.
 *
 * Port of the pinned Android app's WAV-wrapping logic (`savvy_sensor` repo,
 * commit 48e2d1442cd867cc60f8ff3186d53fce1c08f308, MainActivity.java lines
 * ~908-925, function sendIfComm_VoiceData), cross-referenced against the
 * fixed capture parameters in AudioMic/AutoVoiceReconizer.java (mono,
 * 16-bit, 8000 Hz).
 *
 * This module does NOT capture audio (no AudioRecord/ALSA/microphone
 * driver, no decibel calculation) - it only wraps already-captured PCM
 * bytes into a valid WAV byte stream.
 */

#include <stddef.h>
#include <stdint.h>

#include "savvy/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed format parameters (never configurable - the pinned Android app
 * hardcodes these for its voice-capture path). */
#define SENSOR_WAV_AUDIO_FORMAT_PCM 1
#define SENSOR_WAV_NUM_CHANNELS     1u      /* mono */
#define SENSOR_WAV_SAMPLE_RATE      8000u   /* Hz */
#define SENSOR_WAV_BITS_PER_SAMPLE  16u

/* Derived fixed values (also constant, since every input above is fixed):
 *   byteRate   = sampleRate * bitsPerSample * numChannels / 8 = 8000*16*1/8
 *   blockAlign = numChannels * bitsPerSample / 8               = 1*16/8
 */
#define SENSOR_WAV_BYTE_RATE   16000u
#define SENSOR_WAV_BLOCK_ALIGN 2u

/* Total size of the RIFF/WAVE/fmt /data header this module emits, in
 * bytes, always exactly this size regardless of payload length. */
#define SENSOR_WAV_HEADER_LEN 44u

/*
 * Wraps `pcm_len` bytes at `pcm` in a 44-byte little-endian RIFF/WAVE
 * header (PCM=1, mono, 8000 Hz, 16-bit) followed immediately by the PCM
 * bytes themselves, verbatim.
 *
 * On SAVVY_OK, *out_wav is a newly malloc'd buffer of *out_wav_len ==
 * (44 + pcm_len) bytes that the caller must release with free(); the
 * first 44 bytes are the header, the remainder is `pcm` copied verbatim.
 *
 * Argument/error contract:
 *   - out_wav == NULL || out_wav_len == NULL -> SAVVY_ERR_INVALID_ARGUMENT.
 *   - pcm == NULL && pcm_len > 0             -> SAVVY_ERR_INVALID_ARGUMENT.
 *   - pcm_len == 0 is NOT an error: it succeeds and produces a valid
 *     44-byte-only WAV file with an empty `data` chunk.
 *   - If pcm_len (or 36 + pcm_len) does not fit in the WAV format's
 *     32-bit unsigned header fields, or 44 + pcm_len would overflow
 *     size_t, this returns SAVVY_ERR_OVERFLOW without allocating or
 *     writing anything through the output parameters. Nothing is ever
 *     silently truncated or wrapped.
 *   - malloc failure -> SAVVY_ERR_OUT_OF_MEMORY.
 *
 * On any non-SAVVY_OK return, *out_wav and *out_wav_len are left untouched.
 */
savvy_status_t sensor_wav_wrap(const uint8_t *pcm, size_t pcm_len,
                                uint8_t **out_wav, size_t *out_wav_len);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_STREAM_WAV_H */

/**
 * QminiDoctor Mobile - Audio configuration
 * Shared constants for Opus codec and audio pipeline
 */
#ifndef QMINI_AUDIO_CONFIG_H
#define QMINI_AUDIO_CONFIG_H

#define QMINI_SAMPLE_RATE       16000   /* 16 kHz, same as desktop */
#define QMINI_CHANNELS          1       /* Mono */
#define QMINI_FRAME_MS          20      /* 20 ms frames */
#define QMINI_FRAME_SAMPLES     (QMINI_SAMPLE_RATE * QMINI_FRAME_MS / 1000)  /* 320 */
#define QMINI_FRAME_BYTES       (QMINI_FRAME_SAMPLES * 2)  /* 16-bit = 640 bytes */

#define QMINI_OPUS_BITRATE      32000   /* 32 kbps default */
#define QMINI_OPUS_BITRATE_MIN  16000
#define QMINI_OPUS_BITRATE_MAX  64000

/* Audio processing */
#define QMINI_AEC_TAIL_MS       64
#define QMINI_AEC_TAPS          1024
#define QMINI_NS_FFT_SIZE       256
#define QMINI_AGC_TARGET_DBFS   -20.0f

/* Jitter buffer */
#define QMINI_JITTER_CAPACITY   128
#define QMINI_JITTER_MIN_MS     20
#define QMINI_JITTER_MAX_MS     240

/* Network */
#define QMINI_KEEPALIVE_SEC     3
#define QMINI_P2P_MAX_PEERS     6
#define QMINI_PACKET_HEADER_LEN 32      /* Peer ID header */

#endif /* QMINI_AUDIO_CONFIG_H */

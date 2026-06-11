/**
 * JNI bridge - connects Java QminiNative to shared C core.
 */
#include <jni.h>
#include <string.h>
#include <stdlib.h>

#include "protocol/qmini_protocol.h"
#include "crypto/crypto.h"
#include "jitter/jitter_buffer.h"
#include "codec/codec.h"

/* ---- Signaling Protocol ---- */

/*
 * Returns message type as int, fills outFields array.
 * outFields layout: [peerId, nickname, ip, port, payload]
 * Returns: 0=OK, 1=PEER_JOIN, 2=PEER_LEAVE, 3=ICE, 4=RELAY, -1=error
 */
JNIEXPORT jint JNICALL
Java_com_qmini_voice_QminiNative_parseMessage(JNIEnv *env, jclass cls,
                                                jstring raw, jobjectArray outFields) {
    const char *str = (*env)->GetStringUTFChars(env, raw, NULL);
    qmini_msg_t msg;
    int rc = qmini_parse_message(str, &msg);
    (*env)->ReleaseStringUTFChars(env, raw, str);

    if (rc != 0) return -1;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", msg.port);

    switch (msg.type) {
        case MSG_OK:
            (*env)->SetObjectArrayElement(env, outFields, 0, (*env)->NewStringUTF(env, msg.peer_id));
            return 0;
        case MSG_PEER_JOIN:
            (*env)->SetObjectArrayElement(env, outFields, 0, (*env)->NewStringUTF(env, msg.peer_id));
            (*env)->SetObjectArrayElement(env, outFields, 1, (*env)->NewStringUTF(env, msg.nickname));
            (*env)->SetObjectArrayElement(env, outFields, 2, (*env)->NewStringUTF(env, msg.ip));
            (*env)->SetObjectArrayElement(env, outFields, 3, (*env)->NewStringUTF(env, port_str));
            return 1;
        case MSG_PEER_LEAVE:
            (*env)->SetObjectArrayElement(env, outFields, 0, (*env)->NewStringUTF(env, msg.peer_id));
            return 2;
        case MSG_ICE:
            (*env)->SetObjectArrayElement(env, outFields, 0, (*env)->NewStringUTF(env, msg.peer_id));
            (*env)->SetObjectArrayElement(env, outFields, 4, (*env)->NewStringUTF(env, msg.payload));
            return 3;
        case MSG_RELAY:
            (*env)->SetObjectArrayElement(env, outFields, 0, (*env)->NewStringUTF(env, msg.peer_id));
            (*env)->SetObjectArrayElement(env, outFields, 4, (*env)->NewStringUTF(env, msg.payload));
            return 4;
        default:
            return -1;
    }
}

JNIEXPORT jstring JNICALL
Java_com_qmini_voice_QminiNative_buildRegister(JNIEnv *env, jclass cls,
                                                 jstring room, jstring nick, jint localPort) {
    const char *r = (*env)->GetStringUTFChars(env, room, NULL);
    const char *n = (*env)->GetStringUTFChars(env, nick, NULL);
    char buf[256];
    qmini_build_register(buf, sizeof(buf), r, n, (uint16_t)localPort);
    (*env)->ReleaseStringUTFChars(env, room, r);
    (*env)->ReleaseStringUTFChars(env, nick, n);
    return (*env)->NewStringUTF(env, buf);
}

JNIEXPORT jstring JNICALL
Java_com_qmini_voice_QminiNative_buildIce(JNIEnv *env, jclass cls,
                                            jstring targetId, jstring sdp) {
    const char *t = (*env)->GetStringUTFChars(env, targetId, NULL);
    const char *s = (*env)->GetStringUTFChars(env, sdp, NULL);
    char buf[1024];
    qmini_build_ice(buf, sizeof(buf), t, s);
    (*env)->ReleaseStringUTFChars(env, targetId, t);
    (*env)->ReleaseStringUTFChars(env, sdp, s);
    return (*env)->NewStringUTF(env, buf);
}

JNIEXPORT jstring JNICALL
Java_com_qmini_voice_QminiNative_buildRelay(JNIEnv *env, jclass cls,
                                              jstring targetId, jstring base64Data) {
    const char *t = (*env)->GetStringUTFChars(env, targetId, NULL);
    const char *b = (*env)->GetStringUTFChars(env, base64Data, NULL);
    char buf[1500];
    qmini_build_relay(buf, sizeof(buf), t, b);
    (*env)->ReleaseStringUTFChars(env, targetId, t);
    (*env)->ReleaseStringUTFChars(env, base64Data, b);
    return (*env)->NewStringUTF(env, buf);
}

JNIEXPORT jstring JNICALL
Java_com_qmini_voice_QminiNative_buildHello(JNIEnv *env, jclass cls, jstring peerId) {
    const char *p = (*env)->GetStringUTFChars(env, peerId, NULL);
    char buf[64];
    qmini_build_hello(buf, sizeof(buf), p);
    (*env)->ReleaseStringUTFChars(env, peerId, p);
    return (*env)->NewStringUTF(env, buf);
}

/* ---- Crypto ---- */

/*
 * Thread-safe: each call creates a local crypto_ctx_t instead of using
 * a global. The key is derived per-call from the key parameter.
 */

JNIEXPORT jbyteArray JNICALL
Java_com_qmini_voice_QminiNative_aesEncrypt(JNIEnv *env, jclass cls,
                                             jbyteArray key, jbyteArray plaintext, jlong seqNum) {
    jbyte *k = (*env)->GetByteArrayElements(env, key, NULL);
    jbyte *p = (*env)->GetByteArrayElements(env, plaintext, NULL);
    int len = (*env)->GetArrayLength(env, plaintext);

    crypto_ctx_t crypto;
    memcpy(crypto.key, k, CRYPTO_KEY_SIZE);
    crypto.initialized = 1;

    uint8_t *out = malloc(len + CRYPTO_HMAC_SIZE);
    crypto_encrypt(&crypto, (uint16_t)seqNum, (uint8_t *)p, out, len);

    jbyteArray result = (*env)->NewByteArray(env, len + CRYPTO_HMAC_SIZE);
    (*env)->SetByteArrayRegion(env, result, 0, len + CRYPTO_HMAC_SIZE, (jbyte *)out);

    free(out);
    (*env)->ReleaseByteArrayElements(env, key, k, 0);
    (*env)->ReleaseByteArrayElements(env, plaintext, p, 0);
    return result;
}

JNIEXPORT jbyteArray JNICALL
Java_com_qmini_voice_QminiNative_aesDecrypt(JNIEnv *env, jclass cls,
                                             jbyteArray key, jbyteArray ciphertext, jlong seqNum) {
    jbyte *k = (*env)->GetByteArrayElements(env, key, NULL);
    jbyte *c = (*env)->GetByteArrayElements(env, ciphertext, NULL);
    int len = (*env)->GetArrayLength(env, ciphertext);

    crypto_ctx_t crypto;
    memcpy(crypto.key, k, CRYPTO_KEY_SIZE);
    crypto.initialized = 1;

    uint8_t *out = malloc(len);
    int dec_len = crypto_decrypt(&crypto, (uint16_t)seqNum, (uint8_t *)c, out, len);

    jbyteArray result = NULL;
    if (dec_len > 0) {
        result = (*env)->NewByteArray(env, dec_len);
        (*env)->SetByteArrayRegion(env, result, 0, dec_len, (jbyte *)out);
    }

    free(out);
    (*env)->ReleaseByteArrayElements(env, key, k, 0);
    (*env)->ReleaseByteArrayElements(env, ciphertext, c, 0);
    return result;
}

JNIEXPORT jbyteArray JNICALL
Java_com_qmini_voice_QminiNative_deriveKey(JNIEnv *env, jclass cls,
                                             jstring password, jbyteArray salt) {
    const char *pw = (*env)->GetStringUTFChars(env, password, NULL);
    crypto_ctx_t ctx;
    crypto_init_from_password(&ctx, pw);
    (*env)->ReleaseStringUTFChars(env, password, pw);

    jbyteArray result = (*env)->NewByteArray(env, CRYPTO_KEY_SIZE);
    (*env)->SetByteArrayRegion(env, result, 0, CRYPTO_KEY_SIZE, (jbyte *)ctx.key);
    return result;
}

/* ---- Jitter Buffer ---- */

JNIEXPORT jlong JNICALL
Java_com_qmini_voice_QminiNative_jitterCreate(JNIEnv *env, jclass cls, jint capacity) {
    jitter_buffer_t *jb = malloc(sizeof(jitter_buffer_t));
    jitter_buffer_init(jb);
    return (jlong)(intptr_t)jb;
}

JNIEXPORT jint JNICALL
Java_com_qmini_voice_QminiNative_jitterPush(JNIEnv *env, jclass cls,
                                              jlong handle, jbyteArray data, jlong timestamp) {
    jitter_buffer_t *jb = (jitter_buffer_t *)(intptr_t)handle;
    jbyte *d = (*env)->GetByteArrayElements(env, data, NULL);
    int len = (*env)->GetArrayLength(env, data);
    jitter_buffer_push(jb, (uint8_t *)d, len, (uint16_t)(timestamp & 0xFFFF));
    (*env)->ReleaseByteArrayElements(env, data, d, 0);
    return 0;
}

JNIEXPORT jbyteArray JNICALL
Java_com_qmini_voice_QminiNative_jitterPop(JNIEnv *env, jclass cls, jlong handle) {
    jitter_buffer_t *jb = (jitter_buffer_t *)(intptr_t)handle;
    uint8_t buf[JB_MAX_PACKET];
    uint16_t seq;
    int size = jitter_buffer_pop(jb, buf, &seq);
    if (size <= 0) return NULL;

    jbyteArray result = (*env)->NewByteArray(env, size);
    (*env)->SetByteArrayRegion(env, result, 0, size, (jbyte *)buf);
    return result;
}

JNIEXPORT void JNICALL
Java_com_qmini_voice_QminiNative_jitterDestroy(JNIEnv *env, jclass cls, jlong handle) {
    jitter_buffer_t *jb = (jitter_buffer_t *)(intptr_t)handle;
    jitter_buffer_destroy(jb);
    free(jb);
}

/* ---- Opus Codec ---- */

JNIEXPORT jlong JNICALL
Java_com_qmini_voice_QminiNative_opusEncoderCreate(JNIEnv *env, jclass cls,
                                                     jint sampleRate, jint channels, jint bitrate) {
    codec_enc_t *enc = codec_enc_create(sampleRate, channels);
    if (enc && bitrate > 0) codec_enc_set_bitrate(enc, bitrate);
    return (jlong)(intptr_t)enc;
}

JNIEXPORT jbyteArray JNICALL
Java_com_qmini_voice_QminiNative_opusEncode(JNIEnv *env, jclass cls,
                                              jlong encoder, jshortArray pcm, jint frameSamples) {
    codec_enc_t *enc = (codec_enc_t *)(intptr_t)encoder;
    if (!enc) return NULL;

    jshort *p = (*env)->GetShortArrayElements(env, pcm, NULL);
    uint8_t out[400]; /* max Opus frame size */
    int written = codec_enc_encode(enc, p, frameSamples, out, sizeof(out));
    (*env)->ReleaseShortArrayElements(env, pcm, p, 0);

    if (written <= 0) return NULL;
    jbyteArray result = (*env)->NewByteArray(env, written);
    (*env)->SetByteArrayRegion(env, result, 0, written, (jbyte *)out);
    return result;
}

JNIEXPORT void JNICALL
Java_com_qmini_voice_QminiNative_opusEncoderDestroy(JNIEnv *env, jclass cls, jlong encoder) {
    codec_enc_t *enc = (codec_enc_t *)(intptr_t)encoder;
    if (enc) codec_enc_destroy(enc);
}

JNIEXPORT jlong JNICALL
Java_com_qmini_voice_QminiNative_opusDecoderCreate(JNIEnv *env, jclass cls,
                                                     jint sampleRate, jint channels) {
    codec_dec_t *dec = codec_dec_create(sampleRate, channels);
    return (jlong)(intptr_t)dec;
}

JNIEXPORT jshortArray JNICALL
Java_com_qmini_voice_QminiNative_opusDecode(JNIEnv *env, jclass cls,
                                              jlong decoder, jbyteArray data, jint frameSamples) {
    codec_dec_t *dec = (codec_dec_t *)(intptr_t)decoder;
    if (!dec) return NULL;

    jbyte *d = data ? (*env)->GetByteArrayElements(env, data, NULL) : NULL;
    int len = data ? (*env)->GetArrayLength(env, data) : 0;

    short pcm[960]; /* max frame: 48kHz * 20ms */
    int decoded = codec_dec_decode(dec, (uint8_t *)d, len, pcm, frameSamples, 0);

    if (data) (*env)->ReleaseByteArrayElements(env, data, d, 0);

    if (decoded <= 0) return NULL;
    jshortArray result = (*env)->NewShortArray(env, decoded);
    (*env)->SetShortArrayRegion(env, result, 0, decoded, pcm);
    return result;
}

JNIEXPORT void JNICALL
Java_com_qmini_voice_QminiNative_opusDecoderDestroy(JNIEnv *env, jclass cls, jlong decoder) {
    codec_dec_t *dec = (codec_dec_t *)(intptr_t)decoder;
    if (dec) codec_dec_destroy(dec);
}

package com.qmini.voice;

/**
 * JNI bridge to shared C library (protocol, codec, crypto, jitter)
 */
public class QminiNative {

    static {
        System.loadLibrary("qmini_core");
    }

    /* Signaling protocol - returns msg type: 0=OK, 1=JOIN, 2=LEAVE, 3=ICE, 4=RELAY, -1=error */
    public static native int parseMessage(String raw, String[] outFields);
    public static native String buildRegister(String room, String nick, int localPort);
    public static native String buildIce(String targetId, String sdp);
    public static native String buildRelay(String targetId, String base64Data);
    public static native String buildHello(String peerId);

    /* Opus codec */
    public static native long opusEncoderCreate(int sampleRate, int channels, int bitrate);
    public static native byte[] opusEncode(long encoder, short[] pcm, int frameSamples);
    public static native void opusEncoderDestroy(long encoder);

    public static native long opusDecoderCreate(int sampleRate, int channels);
    public static native short[] opusDecode(long decoder, byte[] data, int frameSamples);
    public static native void opusDecoderDestroy(long decoder);

    /* Crypto */
    public static native byte[] aesEncrypt(byte[] key, byte[] plaintext, long seqNum);
    public static native byte[] aesDecrypt(byte[] key, byte[] ciphertext, long seqNum);
    public static native byte[] deriveKey(String password, byte[] salt);

    /* Jitter buffer */
    public static native long jitterCreate(int capacity);
    public static native int jitterPush(long handle, byte[] data, long timestamp);
    public static native byte[] jitterPop(long handle);
    public static native void jitterDestroy(long handle);
}

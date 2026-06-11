package com.qmini.voice;

import android.content.Context;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.AudioTrack;
import android.media.MediaRecorder;
import android.util.Log;

/**
 * Manages audio capture and playback using Android AudioRecord/AudioTrack.
 * v2: Added mute/unmute, speakerphone toggle, VU meter.
 */
public class QminiAudioManager {
    private static final String TAG = "QminiAudio";

    private static final int SAMPLE_RATE = 16000;
    private static final int CHANNEL_IN = AudioFormat.CHANNEL_IN_MONO;
    private static final int CHANNEL_OUT = AudioFormat.CHANNEL_OUT_MONO;
    private static final int FORMAT = AudioFormat.ENCODING_PCM_16BIT;
    private static final int FRAME_SAMPLES = 320; /* 20ms at 16kHz */

    private AudioRecord recorder;
    private AudioTrack player;
    private volatile boolean running = false;
    private volatile boolean muted = false;
    private volatile boolean speakerOn = false;

    private Thread captureThread;
    private AudioCaptureCallback captureCallback;
    private VuMeterCallback vuMeterCallback;
    private Context appContext;

    public interface AudioCaptureCallback {
        void onAudioCaptured(short[] pcm, int samples);
    }

    public interface VuMeterCallback {
        void onLevel(int levelPercent);
    }

    public void setCaptureCallback(AudioCaptureCallback callback) {
        this.captureCallback = callback;
    }

    public void setVuMeterCallback(VuMeterCallback callback) {
        this.vuMeterCallback = callback;
    }

    public void setContext(Context ctx) {
        this.appContext = ctx.getApplicationContext();
    }

    public boolean start() {
        int bufSize = Math.max(
            AudioRecord.getMinBufferSize(SAMPLE_RATE, CHANNEL_IN, FORMAT),
            FRAME_SAMPLES * 2
        );
        recorder = new AudioRecord(
            MediaRecorder.AudioSource.VOICE_COMMUNICATION,
            SAMPLE_RATE, CHANNEL_IN, FORMAT, bufSize
        );

        if (recorder.getState() != AudioRecord.STATE_INITIALIZED) {
            Log.e(TAG, "AudioRecord init failed");
            recorder.release();
            recorder = null;
            return false;
        }

        int outBufSize = Math.max(
            AudioTrack.getMinBufferSize(SAMPLE_RATE, CHANNEL_OUT, FORMAT),
            FRAME_SAMPLES * 2
        );
        player = new AudioTrack(
            AudioManager.STREAM_VOICE_CALL,
            SAMPLE_RATE, CHANNEL_OUT, FORMAT, outBufSize,
            AudioTrack.MODE_STREAM
        );

        if (player.getState() != AudioTrack.STATE_INITIALIZED) {
            Log.e(TAG, "AudioTrack init failed");
            recorder.stop();
            recorder.release();
            recorder = null;
            player.release();
            player = null;
            return false;
        }

        running = true;
        recorder.startRecording();
        player.play();

        /* Apply initial audio routing */
        applySpeakerMode();

        captureThread = new Thread(this::captureLoop, "QminiCapture");
        captureThread.start();

        Log.i(TAG, "Audio started: " + SAMPLE_RATE + "Hz, frame=" + FRAME_SAMPLES);
        return true;
    }

    public void stop() {
        running = false;
        if (captureThread != null) {
            try { captureThread.join(1000); } catch (InterruptedException ignored) {}
        }
        if (recorder != null) {
            try { recorder.stop(); } catch (IllegalStateException ignored) {}
            recorder.release();
            recorder = null;
        }
        if (player != null) {
            try { player.stop(); } catch (IllegalStateException ignored) {}
            player.release();
            player = null;
        }
        Log.i(TAG, "Audio stopped");
    }

    /**
     * Set mute state. When muted, capture still runs but no data is sent.
     */
    public void setMuted(boolean mute) {
        this.muted = mute;
        Log.i(TAG, "Mute: " + mute);
    }

    public boolean isMuted() { return muted; }

    /**
     * Toggle speakerphone on/off.
     */
    public void setSpeakerphoneOn(boolean on) {
        this.speakerOn = on;
        applySpeakerMode();
        Log.i(TAG, "Speaker: " + on);
    }

    public boolean isSpeakerOn() { return speakerOn; }

    private void applySpeakerMode() {
        if (appContext != null) {
            AudioManager am = (AudioManager) appContext.getSystemService(Context.AUDIO_SERVICE);
            if (am != null) {
                am.setSpeakerphoneOn(speakerOn);
                am.setMode(AudioManager.MODE_IN_COMMUNICATION);
            }
        }
    }

    public void playAudio(short[] pcm) {
        if (player != null && running) {
            try {
                player.write(pcm, 0, pcm.length);
            } catch (IllegalStateException e) {
                Log.e(TAG, "AudioTrack write error", e);
            }
        }
    }

    private void captureLoop() {
        short[] buf = new short[FRAME_SAMPLES];
        while (running) {
            int read = recorder.read(buf, 0, FRAME_SAMPLES);
            if (read == FRAME_SAMPLES) {
                /* Compute VU meter level */
                if (vuMeterCallback != null) {
                    long sumSq = 0;
                    for (int i = 0; i < read; i++) {
                        sumSq += (long) buf[i] * buf[i];
                    }
                    int rms = (int) Math.sqrt(sumSq / read);
                    /* Convert to percentage (0-100), 32767 = 100% */
                    int level = Math.min(100, rms * 100 / 32767);
                    vuMeterCallback.onLevel(level);
                }

                /* Only send audio if not muted */
                if (!muted && captureCallback != null) {
                    captureCallback.onAudioCaptured(buf, read);
                }
            }
        }
    }

    public boolean isRunning() { return running; }
}

package com.qmini.voice;

import android.Manifest;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.widget.*;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

/**
 * Main Activity - v2 with multi-peer mixing, mute, speakerphone, VU meter.
 */
public class MainActivity extends AppCompatActivity {

    private static final int REQ_AUDIO = 100;
    private static final String PREFS = "qmini_config";

    /* UI */
    private EditText etServer, etRoom, etNick, etPassword;
    private Button btnJoin, btnLeave;
    private ImageButton btnMute, btnSpeaker;
    private TextView tvStatus;
    private ProgressBar vuMeter;
    private ListView lvMembers;
    private LinearLayout panelJoin, panelRoom;

    /* Core */
    private QminiAudioManager audioManager;
    private QminiNetworkManager networkManager;
    private long opusEncoder;
    private ArrayAdapter<String> memberAdapter;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        initViews();
        loadConfig();
        requestAudioPermission();
    }

    private void initViews() {
        etServer = findViewById(R.id.et_server);
        etRoom = findViewById(R.id.et_room);
        etNick = findViewById(R.id.et_nick);
        etPassword = findViewById(R.id.et_password);
        btnJoin = findViewById(R.id.btn_join);
        btnLeave = findViewById(R.id.btn_leave);
        btnMute = findViewById(R.id.btn_mute);
        btnSpeaker = findViewById(R.id.btn_speaker);
        tvStatus = findViewById(R.id.tv_status);
        vuMeter = findViewById(R.id.vu_meter);
        lvMembers = findViewById(R.id.lv_members);
        panelJoin = findViewById(R.id.panel_join);
        panelRoom = findViewById(R.id.panel_room);

        memberAdapter = new ArrayAdapter<>(this, android.R.layout.simple_list_item_1);
        lvMembers.setAdapter(memberAdapter);

        btnJoin.setOnClickListener(v -> onJoin());
        btnLeave.setOnClickListener(v -> onLeave());
        btnMute.setOnClickListener(v -> toggleMute());
        btnSpeaker.setOnClickListener(v -> toggleSpeaker());

        showPanel(true);
    }

    private void onJoin() {
        String server = etServer.getText().toString().trim();
        String room = etRoom.getText().toString().trim();
        String nick = etNick.getText().toString().trim();
        String password = etPassword.getText().toString().trim();

        if (server.isEmpty() || room.isEmpty() || nick.isEmpty()) {
            Toast.makeText(this, "请填写所有必填项", Toast.LENGTH_SHORT).show();
            return;
        }

        saveConfig(server, room, nick);
        setStatus("连接中...");

        /* Init audio */
        audioManager = new QminiAudioManager();
        audioManager.setContext(this);
        audioManager.setCaptureCallback((pcm, samples) -> {
            if (opusEncoder != 0) {
                byte[] encoded = QminiNative.opusEncode(opusEncoder, pcm, samples);
                if (encoded != null && networkManager != null) {
                    networkManager.sendAudio(encoded);
                }
            }
        });

        /* VU meter callback */
        audioManager.setVuMeterCallback(level -> {
            runOnUiThread(() -> {
                if (vuMeter != null) vuMeter.setProgress(level);
            });
        });

        if (!audioManager.start()) {
            setStatus("音频初始化失败");
            return;
        }

        /* Init codec (encoder only — decoders are per-peer in network manager) */
        opusEncoder = QminiNative.opusEncoderCreate(16000, 1, 32000);

        /* Init network */
        networkManager = new QminiNetworkManager();
        networkManager.setSignalingCallback(new QminiNetworkManager.SignalingCallback() {
            @Override
            public void onRegistered(String peerId) {
                runOnUiThread(() -> {
                    setStatus("已连接: " + peerId);
                    showPanel(false);
                });
            }

            @Override
            public void onPeerJoin(QminiNetworkManager.PeerInfo peer) {
                runOnUiThread(() -> {
                    memberAdapter.add(peer.nickname + " (" + peer.id + ")");
                    memberAdapter.notifyDataSetChanged();
                });
            }

            @Override
            public void onPeerLeave(String peerId) {
                runOnUiThread(() -> {
                    for (int i = 0; i < memberAdapter.getCount(); i++) {
                        if (memberAdapter.getItem(i).contains(peerId)) {
                            memberAdapter.remove(memberAdapter.getItem(i));
                            break;
                        }
                    }
                    memberAdapter.notifyDataSetChanged();
                });
            }

            @Override
            public void onError(String error) {
                runOnUiThread(() -> setStatus("错误: " + error));
            }
        });

        /* Mixed audio callback — receives decoded+mixed PCM from all peers */
        networkManager.setAudioCallback((pcm, samples) -> {
            if (audioManager != null) {
                audioManager.playAudio(pcm);
            }
        });

        networkManager.joinRoom(server, room, nick, password);

        /* Start foreground service to keep audio alive in background */
        startVoiceService(room, nick);
    }

    private void onLeave() {
        stopVoiceService();

        if (networkManager != null) networkManager.leaveRoom();
        if (audioManager != null) audioManager.stop();
        if (opusEncoder != 0) { QminiNative.opusEncoderDestroy(opusEncoder); opusEncoder = 0; }

        memberAdapter.clear();
        memberAdapter.notifyDataSetChanged();
        setStatus("未连接");
        showPanel(true);

        /* Reset button states */
        if (btnMute != null) btnMute.setAlpha(1.0f);
        if (btnSpeaker != null) btnSpeaker.setAlpha(0.5f);
        if (vuMeter != null) vuMeter.setProgress(0);
    }

    private void toggleMute() {
        if (audioManager == null) return;
        boolean newMuted = !audioManager.isMuted();
        audioManager.setMuted(newMuted);
        btnMute.setAlpha(newMuted ? 0.4f : 1.0f);
        Toast.makeText(this, newMuted ? "已静音" : "已取消静音", Toast.LENGTH_SHORT).show();
    }

    private void toggleSpeaker() {
        if (audioManager == null) return;
        boolean newSpeaker = !audioManager.isSpeakerOn();
        audioManager.setSpeakerphoneOn(newSpeaker);
        btnSpeaker.setAlpha(newSpeaker ? 1.0f : 0.5f);
        Toast.makeText(this, newSpeaker ? "扬声器已开启" : "扬声器已关闭", Toast.LENGTH_SHORT).show();
    }

    private void startVoiceService(String room, String nick) {
        Intent intent = new Intent(this, VoiceService.class);
        intent.setAction(VoiceService.ACTION_START);
        intent.putExtra(VoiceService.EXTRA_ROOM, room);
        intent.putExtra(VoiceService.EXTRA_NICK, nick);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent);
        } else {
            startService(intent);
        }
    }

    private void stopVoiceService() {
        Intent intent = new Intent(this, VoiceService.class);
        intent.setAction(VoiceService.ACTION_STOP);
        startService(intent);
    }

    private void showPanel(boolean showJoin) {
        panelJoin.setVisibility(showJoin ? View.VISIBLE : View.GONE);
        panelRoom.setVisibility(showJoin ? View.GONE : View.VISIBLE);
    }

    private void setStatus(String text) {
        tvStatus.setText(text);
    }

    private void saveConfig(String server, String room, String nick) {
        getSharedPreferences(PREFS, MODE_PRIVATE).edit()
            .putString("server", server)
            .putString("room", room)
            .putString("nick", nick)
            .apply();
    }

    private void loadConfig() {
        SharedPreferences sp = getSharedPreferences(PREFS, MODE_PRIVATE);
        etServer.setText(sp.getString("server", "127.0.0.1"));
        etRoom.setText(sp.getString("room", ""));
        etNick.setText(sp.getString("nick", ""));
    }

    private void requestAudioPermission() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
                != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this,
                new String[]{Manifest.permission.RECORD_AUDIO}, REQ_AUDIO);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
                    != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(this,
                    new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQ_AUDIO + 1);
            }
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        onLeave();
    }
}

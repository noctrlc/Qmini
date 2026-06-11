package com.qmini.voice;

import android.util.Log;
import java.io.*;
import java.net.*;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Network layer: TCP signaling + UDP audio transport.
 * Supports P2P mode (<=6 peers) and SFU mode (>6 peers).
 * Integrates: AES encryption, per-peer jitter buffer, keepalive, RELAY fallback.
 *
 * v2: Per-peer jitter buffers + audio mixing (fixes multi-peer audio interleaving)
 */
public class QminiNetworkManager {
    private static final String TAG = "QminiNet";
    private static final int SIGNAL_PORT = 9088;
    private static final int SFU_PORT = 9089;
    private static final int HEADER_LEN = 32;
    private static final int P2P_MAX_PEERS = 6;
    private static final long KEEPALIVE_INTERVAL_MS = 3000;
    private static final int FRAME_SAMPLES = 320;  /* 20ms at 16kHz */
    private static final long STALE_PEER_TIMEOUT_MS = 10000;  /* 10s no packet = stale */

    private Socket tcpSocket;
    private DatagramSocket udpSocket;
    private PrintWriter tcpWriter;
    private BufferedReader tcpReader;
    private String serverAddress;

    private volatile boolean connected = false;
    private volatile boolean sfuMode = false;
    private String localPeerId;
    private int localUdpPort;

    private SignalingCallback signalingCallback;
    private AudioReceiveCallback audioCallback;

    /* Encryption */
    private byte[] cryptoKey;  /* 16 bytes, null = no encryption */
    private long seqSend = 0;

    /* Per-peer state (jitter buffer + decoder managed in PeerInfo) */
    private final ConcurrentHashMap<String, PeerInfo> peers = new ConcurrentHashMap<>();

    /* Mixing thread */
    private Thread mixThread;
    private volatile boolean mixing = false;

    /* Congestion control */
    private long ccLastCheck = 0;
    private int ccBitrate = 32000;  /* current target bitrate */
    private long ccPacketsSent = 0;
    private long ccPacketsAcked = 0;
    private float ccSmoothedLoss = 0f;

    public static class PeerInfo {
        public String id;
        public String nickname;
        public String ip;
        public int port;
        public volatile long lastSeen;
        public volatile boolean useRelay;
        /* Per-peer audio state */
        public long jitterHandle;   /* native jitter buffer */
        public long decoderHandle;  /* native Opus decoder */
        public volatile boolean active;
    }

    public interface SignalingCallback {
        void onPeerJoin(PeerInfo peer);
        void onPeerLeave(String peerId);
        void onRegistered(String peerId);
        void onError(String error);
    }

    /**
     * Audio callback receives MIXED PCM from all peers (already decoded and mixed).
     * This replaces the old per-packet callback.
     */
    public interface AudioReceiveCallback {
        void onMixedAudio(short[] pcm, int samples);
    }

    public void setSignalingCallback(SignalingCallback cb) { this.signalingCallback = cb; }
    public void setAudioCallback(AudioReceiveCallback cb) { this.audioCallback = cb; }

    /**
     * Join a room. Runs on background thread.
     */
    public void joinRoom(String server, String room, String nickname, String password) {
        this.serverAddress = server;

        /* Derive encryption key from password */
        if (password != null && !password.isEmpty()) {
            this.cryptoKey = QminiNative.deriveKey(password, null);
        } else {
            this.cryptoKey = null;
        }

        new Thread(() -> {
            try {
                /* TCP signaling */
                tcpSocket = new Socket();
                tcpSocket.connect(new InetSocketAddress(server, SIGNAL_PORT), 5000);
                tcpSocket.setSoTimeout(10000);
                tcpWriter = new PrintWriter(new OutputStreamWriter(tcpSocket.getOutputStream(), "UTF-8"), true);
                tcpReader = new BufferedReader(new InputStreamReader(tcpSocket.getInputStream(), "UTF-8"));

                /* UDP audio */
                udpSocket = new DatagramSocket();
                localUdpPort = udpSocket.getLocalPort();

                /* Send REGISTER */
                String regMsg = QminiNative.buildRegister(room, nickname, localUdpPort);
                tcpWriter.print(regMsg);
                tcpWriter.flush();

                /* Wait for OK */
                String okLine = tcpReader.readLine();
                if (okLine == null || !okLine.startsWith("OK")) {
                    notifyError("注册失败: " + okLine);
                    return;
                }
                localPeerId = okLine.substring(3).trim();
                connected = true;
                sfuMode = false;
                notifyRegistered(localPeerId);

                /* Send UDP HELLO for NAT traversal (3 times for reliability) */
                String hello = QminiNative.buildHello(localPeerId);
                byte[] helloBytes = hello.getBytes();
                InetAddress serverAddr = InetAddress.getByName(server);
                for (int i = 0; i < 3; i++) {
                    DatagramPacket helloPkt = new DatagramPacket(
                        helloBytes, helloBytes.length, serverAddr, SFU_PORT
                    );
                    udpSocket.send(helloPkt);
                    Thread.sleep(50);
                }

                /* Start threads */
                startTcpReceiveLoop();
                startUdpReceiveLoop();
                startKeepaliveLoop();
                startMixLoop();
                startStalePeerChecker();

            } catch (Exception e) {
                Log.e(TAG, "Join failed", e);
                notifyError("连接失败: " + e.getMessage());
            }
        }, "QminiJoin").start();
    }

    public void leaveRoom() {
        connected = false;
        sfuMode = false;
        mixing = false;
        if (tcpWriter != null) {
            try {
                tcpWriter.print("UNREGISTER\n");
                tcpWriter.flush();
            } catch (Exception ignored) {}
        }
        closeQuietly(tcpSocket);
        if (udpSocket != null && !udpSocket.isClosed()) udpSocket.close();

        /* Destroy all per-peer resources */
        for (PeerInfo peer : peers.values()) {
            destroyPeerResources(peer);
        }
        peers.clear();

        cryptoKey = null;
        seqSend = 0;
        ccBitrate = 32000;
    }

    /**
     * Send opus audio frame with encryption.
     */
    public void sendAudio(byte[] opusFrame) {
        if (!connected || udpSocket == null || udpSocket.isClosed()) return;
        try {
            /* Encrypt if key is set */
            byte[] data = opusFrame;
            if (cryptoKey != null) {
                byte[] encrypted = QminiNative.aesEncrypt(cryptoKey, opusFrame, seqSend);
                if (encrypted != null) {
                    data = encrypted;
                    seqSend++;
                }
            }

            /* Build packet: 32-byte peer ID header + audio data */
            byte[] packet = new byte[HEADER_LEN + data.length];
            byte[] idBytes = localPeerId.getBytes();
            System.arraycopy(idBytes, 0, packet, 0, Math.min(idBytes.length, HEADER_LEN));
            System.arraycopy(data, 0, packet, HEADER_LEN, data.length);

            ccPacketsSent++;

            if (sfuMode) {
                /* SFU mode: send to server relay port */
                InetAddress serverAddr = InetAddress.getByName(serverAddress);
                DatagramPacket pkt = new DatagramPacket(packet, packet.length, serverAddr, SFU_PORT);
                udpSocket.send(pkt);
            } else {
                /* P2P mode: send to each peer directly, RELAY fallback for unreachable peers */
                for (PeerInfo peer : peers.values()) {
                    if (peer.useRelay) {
                        sendRelay(peer.id, data);
                    } else {
                        try {
                            InetAddress addr = InetAddress.getByName(peer.ip);
                            DatagramPacket pkt = new DatagramPacket(packet, packet.length, addr, peer.port);
                            udpSocket.send(pkt);
                        } catch (Exception e) {
                            peer.useRelay = true;
                            Log.w(TAG, "UDP send failed to " + peer.id + ", switching to RELAY");
                            sendRelay(peer.id, data);
                        }
                    }
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Send audio failed", e);
        }
    }

    /**
     * Get current encoder bitrate (from congestion control).
     */
    public int getCurrentBitrate() { return ccBitrate; }

    /**
     * Send audio via TCP RELAY when UDP is unreachable.
     */
    private void sendRelay(String peerId, byte[] data) {
        if (tcpWriter == null) return;
        try {
            String b64 = android.util.Base64.encodeToString(data, android.util.Base64.NO_WRAP);
            String msg = QminiNative.buildRelay(peerId, b64);
            tcpWriter.print(msg);
            tcpWriter.flush();
        } catch (Exception e) {
            Log.e(TAG, "RELAY send failed", e);
        }
    }

    private void checkSfuMode() {
        if (!sfuMode && peers.size() > P2P_MAX_PEERS) {
            sfuMode = true;
            Log.i(TAG, "切换到 SFU 模式 (" + peers.size() + " 人)");
        }
    }

    /**
     * Create per-peer audio resources (jitter buffer + decoder).
     */
    private PeerInfo createPeer(String id, String nickname, String ip, int port) {
        PeerInfo peer = new PeerInfo();
        peer.id = id;
        peer.nickname = nickname;
        peer.ip = ip;
        peer.port = port;
        peer.lastSeen = System.currentTimeMillis();
        peer.useRelay = false;
        peer.jitterHandle = QminiNative.jitterCreate(128);
        peer.decoderHandle = QminiNative.opusDecoderCreate(16000, 1);
        peer.active = true;
        return peer;
    }

    /**
     * Destroy per-peer audio resources.
     */
    private void destroyPeerResources(PeerInfo peer) {
        peer.active = false;
        if (peer.jitterHandle != 0) {
            QminiNative.jitterDestroy(peer.jitterHandle);
            peer.jitterHandle = 0;
        }
        if (peer.decoderHandle != 0) {
            QminiNative.opusDecoderDestroy(peer.decoderHandle);
            peer.decoderHandle = 0;
        }
    }

    /**
     * Mixing thread: pulls from all peer jitter buffers, decodes, mixes, outputs.
     * Runs every 20ms to match audio frame size.
     */
    private void startMixLoop() {
        mixing = true;
        mixThread = new Thread(() -> {
            short[] mixBuf = new short[FRAME_SAMPLES];
            while (mixing && connected) {
                try {
                    Thread.sleep(20);  /* 20ms frame interval */

                    if (!connected || audioCallback == null) continue;

                    /* Clear mix buffer */
                    java.util.Arrays.fill(mixBuf, (short) 0);
                    boolean hasAudio = false;

                    /* Pull from each peer's jitter buffer */
                    for (PeerInfo peer : peers.values()) {
                        if (!peer.active || peer.jitterHandle == 0 || peer.decoderHandle == 0)
                            continue;

                        byte[] opusData = QminiNative.jitterPop(peer.jitterHandle);
                        if (opusData == null) continue;

                        short[] pcm = QminiNative.opusDecode(peer.decoderHandle, opusData, FRAME_SAMPLES);
                        if (pcm == null) continue;

                        /* Mix into output buffer (with clipping protection) */
                        for (int i = 0; i < FRAME_SAMPLES && i < pcm.length; i++) {
                            int mixed = mixBuf[i] + pcm[i];
                            /* Clamp to 16-bit range */
                            if (mixed > 32767) mixed = 32767;
                            else if (mixed < -32768) mixed = -32768;
                            mixBuf[i] = (short) mixed;
                        }
                        hasAudio = true;
                    }

                    /* Always output (silence when no peers) to keep AudioTrack fed */
                    audioCallback.onMixedAudio(mixBuf, FRAME_SAMPLES);
                } catch (InterruptedException e) {
                    break;
                } catch (Exception e) {
                    Log.e(TAG, "Mix loop error", e);
                }
            }
        }, "QminiMix");
        mixThread.setPriority(Thread.MAX_PRIORITY);
        mixThread.start();
    }

    /**
     * Keepalive loop: sends UDP HELLO every 3 seconds to maintain NAT mapping.
     */
    private void startKeepaliveLoop() {
        new Thread(() -> {
            try {
                while (connected) {
                    Thread.sleep(KEEPALIVE_INTERVAL_MS);
                    if (!connected) break;
                    try {
                        String hello = QminiNative.buildHello(localPeerId);
                        byte[] helloBytes = hello.getBytes();
                        InetAddress serverAddr = InetAddress.getByName(serverAddress);
                        DatagramPacket pkt = new DatagramPacket(
                            helloBytes, helloBytes.length, serverAddr, SFU_PORT
                        );
                        udpSocket.send(pkt);
                    } catch (Exception e) {
                        Log.w(TAG, "Keepalive failed", e);
                    }
                }
            } catch (InterruptedException ignored) {}
        }, "QminiKeepalive").start();
    }

    /**
     * Stale peer checker: removes peers that haven't sent packets in 10 seconds.
     */
    private void startStalePeerChecker() {
        new Thread(() -> {
            try {
                while (connected) {
                    Thread.sleep(5000);
                    if (!connected) break;
                    long now = System.currentTimeMillis();
                    for (PeerInfo peer : peers.values()) {
                        if (now - peer.lastSeen > STALE_PEER_TIMEOUT_MS) {
                            Log.w(TAG, "Stale peer detected: " + peer.id);
                            peers.remove(peer.id);
                            destroyPeerResources(peer);
                            if (signalingCallback != null) {
                                signalingCallback.onPeerLeave(peer.id);
                            }
                            /* Check SFU mode downgrade */
                            if (peers.size() <= P2P_MAX_PEERS && sfuMode) {
                                sfuMode = false;
                                Log.i(TAG, "切回 P2P 模式 (" + peers.size() + " 人)");
                            }
                        }
                    }
                }
            } catch (InterruptedException ignored) {}
        }, "QminiStaleCheck").start();
    }

    /**
     * Congestion control: adjust bitrate based on packet loss.
     * Called periodically from the keepalive loop context.
     */
    private void updateCongestionControl() {
        long now = System.currentTimeMillis();
        if (now - ccLastCheck < 1000) return;  /* Check every 1 second */
        ccLastCheck = now;

        if (ccPacketsSent == 0) return;

        /* Simple loss-based CC (same algorithm as desktop) */
        float lossRate = (float)(ccPacketsSent - ccPacketsAcked) / ccPacketsSent;
        ccSmoothedLoss = ccSmoothedLoss * 0.7f + lossRate * 0.3f;

        int newBitrate;
        if (ccSmoothedLoss < 0.02f) {
            newBitrate = 64000;
        } else if (ccSmoothedLoss < 0.05f) {
            newBitrate = 48000;
        } else if (ccSmoothedLoss < 0.10f) {
            newBitrate = 32000;
        } else {
            newBitrate = 16000;
        }

        if (newBitrate != ccBitrate) {
            ccBitrate = newBitrate;
            Log.i(TAG, "CC: bitrate=" + ccBitrate + " loss=" + (int)(ccSmoothedLoss * 100) + "%");
        }

        /* Reset counters */
        ccPacketsSent = 0;
        ccPacketsAcked = 0;
    }

    private void startTcpReceiveLoop() {
        new Thread(() -> {
            try {
                while (connected) {
                    String line = tcpReader.readLine();
                    if (line == null) break;
                    handleSignalingMessage(line);
                }
            } catch (Exception e) {
                if (connected) Log.e(TAG, "TCP recv error", e);
            }
        }, "QminiTcpRecv").start();
    }

    private void startUdpReceiveLoop() {
        new Thread(() -> {
            byte[] buf = new byte[1500];
            while (connected) {
                try {
                    DatagramPacket pkt = new DatagramPacket(buf, buf.length);
                    udpSocket.receive(pkt);
                    if (pkt.getLength() > HEADER_LEN) {
                        String peerId = new String(buf, 0, Math.min(HEADER_LEN, pkt.getLength())).trim();
                        byte[] rawData = new byte[pkt.getLength() - HEADER_LEN];
                        System.arraycopy(buf, HEADER_LEN, rawData, 0, rawData.length);

                        /* Decrypt if key is set */
                        byte[] opusData = rawData;
                        if (cryptoKey != null) {
                            byte[] decrypted = QminiNative.aesDecrypt(cryptoKey, rawData, 0);
                            if (decrypted != null) {
                                opusData = decrypted;
                            } else {
                                continue;  /* Decryption failed, skip */
                            }
                        }

                        /* Find peer and push to their jitter buffer */
                        PeerInfo peer = peers.get(peerId);
                        if (peer != null && peer.active && peer.jitterHandle != 0) {
                            peer.lastSeen = System.currentTimeMillis();
                            peer.useRelay = false;  /* UDP received, disable relay */
                            QminiNative.jitterPush(peer.jitterHandle, opusData, System.currentTimeMillis());
                            ccPacketsAcked++;
                        }

                        updateCongestionControl();
                    }
                } catch (SocketException e) {
                    if (connected) Log.w(TAG, "UDP socket closed");
                    break;
                } catch (Exception e) {
                    if (connected) Log.e(TAG, "UDP recv error", e);
                }
            }
        }, "QminiUdpRecv").start();
    }

    private void handleSignalingMessage(String line) {
        String[] fields = new String[5];
        int msgType = QminiNative.parseMessage(line, fields);

        switch (msgType) {
            case 0: /* OK */
                Log.i(TAG, "收到 OK: " + fields[0]);
                break;
            case 1: /* PEER_JOIN */
                PeerInfo peer = createPeer(fields[0], fields[1], fields[2],
                    Integer.parseInt(fields[3]));
                peers.put(peer.id, peer);
                checkSfuMode();
                if (signalingCallback != null) signalingCallback.onPeerJoin(peer);
                break;
            case 2: /* PEER_LEAVE */
                PeerInfo removed = peers.remove(fields[0]);
                if (removed != null) destroyPeerResources(removed);
                if (peers.size() <= P2P_MAX_PEERS && sfuMode) {
                    sfuMode = false;
                    Log.i(TAG, "切回 P2P 模式 (" + peers.size() + " 人)");
                }
                if (signalingCallback != null) signalingCallback.onPeerLeave(fields[0]);
                break;
            case 3: /* ICE - ignored */
                break;
            case 4: /* RELAY - receive relayed audio */
                if (fields[0] != null && fields[4] != null) {
                    try {
                        byte[] data = android.util.Base64.decode(fields[4], android.util.Base64.NO_WRAP);
                        if (data != null) {
                            byte[] opusData = data;
                            if (cryptoKey != null) {
                                byte[] decrypted = QminiNative.aesDecrypt(cryptoKey, data, 0);
                                if (decrypted != null) opusData = decrypted;
                            }
                            /* Push to peer's jitter buffer */
                            PeerInfo relayPeer = peers.get(fields[0]);
                            if (relayPeer != null && relayPeer.active && relayPeer.jitterHandle != 0) {
                                relayPeer.lastSeen = System.currentTimeMillis();
                                QminiNative.jitterPush(relayPeer.jitterHandle, opusData, System.currentTimeMillis());
                            }
                        }
                    } catch (Exception e) {
                        Log.e(TAG, "RELAY decode failed", e);
                    }
                }
                break;
            default:
                Log.w(TAG, "未知消息: " + line);
                break;
        }
    }

    private void notifyRegistered(String peerId) {
        if (signalingCallback != null) signalingCallback.onRegistered(peerId);
    }

    private void notifyError(String error) {
        if (signalingCallback != null) signalingCallback.onError(error);
    }

    private void closeQuietly(Socket s) {
        try { if (s != null) s.close(); } catch (Exception ignored) {}
    }

    public boolean isConnected() { return connected; }
    public String getLocalPeerId() { return localPeerId; }
    public boolean isSfuMode() { return sfuMode; }
    public int getPeerCount() { return peers.size(); }
}

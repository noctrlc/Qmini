# QminiDoctor Mobile - Architecture

## Overview
Mobile client for QminiDoctor VoIP system. Compatible with existing signaling server (no server changes needed).

## Platform Support
- Android (Java/Kotlin)
- iOS (Swift)

## Shared Core (C library, cross-platform)
The following modules are platform-independent C code that can be compiled for both Android (NDK) and iOS:

| Module | Source | Description |
|--------|--------|-------------|
| codec | shared/codec/ | Opus encode/decode wrapper |
| crypto | shared/crypto/ | AES-128-CTR + HMAC |
| jitter | shared/jitter/ | Adaptive jitter buffer |
| network | shared/network/ | UDP packet format, keepalive |
| protocol | shared/protocol/ | Signaling message parser/builder |

## Platform-Specific
| Layer | Android | iOS |
|-------|---------|-----|
| Audio Capture | AudioRecord (OpenSL ES) | AVAudioEngine |
| Audio Playback | AudioTrack | AVAudioEngine |
| UI | Activity + Fragment | UIKit / SwiftUI |
| Config Storage | SharedPreferences | UserDefaults |
| Socket | java.net.DatagramSocket | NWConnection (Network.framework) |
| Background | Foreground Service | Background Modes (Audio) |

## Audio Pipeline (same as desktop)
```
Mic -> [Platform AEC] -> [NS] -> [AGC] -> Opus Encode -> UDP Send
UDP Recv -> Jitter Buffer -> Opus Decode -> Mix -> Speaker
```

## Signaling Protocol (unchanged)
- TCP port 9088, text-based, newline-delimited
- REGISTER / OK / PEER_JOIN / PEER_LEAVE / ICE / RELAY / UNREGISTER

## Audio Transport (unchanged)
- UDP, 32-byte ID header + AES-encrypted Opus frame
- P2P mode (<=6 peers), SFU mode (>6 peers)
- Keepalive every 3 seconds

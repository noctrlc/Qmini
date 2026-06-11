import Foundation

/**
 * Signaling protocol client for iOS.
 * Compatible with QminiDoctor desktop signaling server.
 */
class QminiProtocol {

    static let signalPort: UInt16 = 9088
    static let sfuPort: UInt16 = 9089
    static let headerLen = 32

    enum MessageType {
        case ok(peerId: String)
        case peerJoin(id: String, nickname: String, ip: String, port: UInt16)
        case peerLeave(id: String)
        case ice(fromId: String, payload: String)
        case relay(fromId: String, data: String)
        case unknown
    }

    static func parseMessage(_ line: String) -> MessageType {
        let parts = line.split(separator: " ", maxSplits: 4).map(String.init)
        guard let cmd = parts.first else { return .unknown }

        switch cmd {
        case "OK":
            guard parts.count >= 2 else { return .unknown }
            return .ok(peerId: parts[1])

        case "PEER_JOIN":
            guard parts.count >= 5,
                  let port = UInt16(parts[4]) else { return .unknown }
            return .peerJoin(id: parts[1], nickname: parts[2], ip: parts[3], port: port)

        case "PEER_LEAVE":
            guard parts.count >= 2 else { return .unknown }
            return .peerLeave(id: parts[1])

        case "ICE":
            guard parts.count >= 3 else { return .unknown }
            return .ice(fromId: parts[1], payload: parts[2...].joined(separator: " "))

        case "RELAY":
            guard parts.count >= 3 else { return .unknown }
            return .relay(fromId: parts[1], data: parts[2...].joined(separator: " "))

        default:
            return .unknown
        }
    }

    static func buildRegister(room: String, nickname: String, localPort: UInt16) -> String {
        return "REGISTER \(room) \(nickname) \(localPort) 0 0\n"
    }

    static func buildIce(targetId: String, sdp: String) -> String {
        return "ICE \(targetId) \(sdp)\n"
    }

    static func buildRelay(targetId: String, base64Data: String) -> String {
        return "RELAY \(targetId) \(base64Data)\n"
    }

    static func buildHello(peerId: String) -> Data {
        return "HELLO \(peerId)".data(using: .utf8) ?? Data()
    }
}

/**
 * Peer info
 */
struct QminiPeer {
    let id: String
    let nickname: String
    let ip: String
    let port: UInt16
    var lastSeen: Date
}

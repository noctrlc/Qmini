import Foundation

/**
 * Swift wrapper for C core functions.
 * Uses the bridging header to access C APIs.
 */
class QminiCore {

    // MARK: - Crypto

    private var cryptoCtx = crypto_ctx_t()

    func initCrypto(password: String) {
        password.withCString { ptr in
            crypto_init_from_password(&cryptoCtx, ptr)
        }
    }

    func encrypt(seq: UInt16, plaintext: Data) -> Data? {
        guard crypto_is_ready(&cryptoCtx) != 0, !plaintext.isEmpty else { return nil }

        return plaintext.withUnsafeBytes { src in
            let len = plaintext.count
            var output = Data(count: len + Int(CRYPTO_HMAC_SIZE))
            return output.withUnsafeMutableBytes { dst in
                let written = crypto_encrypt(&cryptoCtx, seq,
                    src.bindMemory(to: UInt8.self).baseAddress!,
                    dst.bindMemory(to: UInt8.self).baseAddress!, Int32(len))
                return written > 0 ? Data(dst.prefix(Int(written) + Int(CRYPTO_HMAC_SIZE))) : nil
            }
        }
    }

    func decrypt(seq: UInt16, ciphertext: Data) -> Data? {
        guard crypto_is_ready(&cryptoCtx) != 0, ciphertext.count >= Int(CRYPTO_HMAC_SIZE) else { return nil }

        return ciphertext.withUnsafeBytes { src in
            var output = Data(count: ciphertext.count)
            return output.withUnsafeMutableBytes { dst in
                let written = crypto_decrypt(&cryptoCtx, seq,
                    src.bindMemory(to: UInt8.self).baseAddress!,
                    dst.bindMemory(to: UInt8.self).baseAddress!, Int32(ciphertext.count))
                return written > 0 ? Data(dst.prefix(Int(written))) : nil
            }
        }
    }

    // MARK: - Jitter Buffer

    private var jitterHandle: UnsafeMutablePointer<jitter_buffer_t>?

    func createJitterBuffer() {
        jitterHandle = UnsafeMutablePointer<jitter_buffer_t>.allocate(capacity: 1)
        jitter_buffer_init(jitterHandle)
    }

    func pushToJitter(data: Data, seq: UInt16) {
        guard let jb = jitterHandle else { return }
        data.withUnsafeBytes { src in
            jitter_buffer_push(jb, src.bindMemory(to: UInt8.self).baseAddress!,
                               Int32(data.count), seq)
        }
    }

    func popFromJitter() -> Data? {
        guard let jb = jitterHandle else { return nil }
        var buf = [UInt8](repeating: 0, count: Int(JB_MAX_PACKET))
        var seq: UInt16 = 0
        let size = jitter_buffer_pop(jb, &buf, &seq)
        guard size > 0 else { return nil }
        return Data(buf.prefix(Int(size)))
    }

    func destroyJitterBuffer() {
        guard let jb = jitterHandle else { return }
        jitter_buffer_destroy(jb)
        jb.deallocate()
        jitterHandle = nil
    }

    // MARK: - Protocol

    static func buildRegister(room: String, nickname: String, localPort: UInt16) -> String {
        var buf = [CChar](repeating: 0, count: 256)
        qmini_build_register(&buf, 256, room, nickname, localPort)
        return String(cString: buf)
    }

    static func buildHello(peerId: String) -> Data {
        var buf = [CChar](repeating: 0, count: 64)
        let len = qmini_build_hello(&buf, 64, peerId)
        return Data(bytes: buf, count: Int(len))
    }
}

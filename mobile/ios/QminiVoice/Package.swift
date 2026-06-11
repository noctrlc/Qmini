// swift-tools-version:5.7
import PackageDescription

let sharedDir = "../../shared"

let package = Package(
    name: "QminiCore",
    platforms: [.iOS(.v14)],
    products: [
        .library(name: "QminiCore", targets: ["QminiCore"])
    ],
    targets: [
        .target(
            name: "QminiCore",
            path: sharedDir,
            sources: [
                "protocol/qmini_protocol.c",
                "crypto/crypto.c",
                "crypto/sha256.c",
                "crypto/tiny_aes.c",
                "jitter/jitter_buffer.c"
            ],
            publicHeadersPath: ".",
            cSettings: [
                .headerSearchPath("protocol"),
                .headerSearchPath("crypto"),
                .headerSearchPath("jitter"),
                .headerSearchPath("audio")
            ]
        )
    ]
)

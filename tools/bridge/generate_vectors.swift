import BitFoundation
import CryptoKit
import Foundation

let seed = Data((0..<32).map { UInt8($0) })
let signingKey = try Curve25519.Signing.PrivateKey(rawRepresentation: seed)
let noiseKey = try Curve25519.KeyAgreement.PrivateKey(rawRepresentation: Data(repeating: 0x42, count: 32))
let noisePublic = noiseKey.publicKey.rawRepresentation
let publicKey = signingKey.publicKey.rawRepresentation
let peerID = Data(SHA256.hash(data: noisePublic).prefix(8))
let timestamp: UInt64 = 1_795_000_000_000
func hex(_ value: Data) -> String { value.map { String(format: "%02x", $0) }.joined() }
var cases: [[String: String]] = []

func append(_ name: String, _ packet: BitchatPacket, signed: Bool, padded: Bool) throws {
    var packet = packet
    let canonical = packet.toBinaryDataForSigning()!
    if signed { packet.signature = try signingKey.signature(for: canonical) }
    cases.append([
        "name": name, "wire": hex(packet.toBinaryData(padding: padded)!),
        "canonical": hex(canonical), "public_key": hex(publicKey)
    ])
}

var announce = Data([1, 7])
announce.append(Data("fixture".utf8))
announce.append(contentsOf: [2, 32])
announce.append(noisePublic)
announce.append(contentsOf: [3, 32])
announce.append(publicKey)
try append("announce", BitchatPacket(type: 1, senderID: peerID, recipientID: nil, timestamp: timestamp,
    payload: announce, signature: nil, ttl: 7), signed: true, padded: false)

for (name, text) in [("public", "Olá, mundo 🌎"), ("lora", "!lora Olá 🌎"), ("tunnel", "!bctun teste")] {
    try append(name, BitchatPacket(type: 2, senderID: peerID, recipientID: nil, timestamp: timestamp,
        payload: Data(text.utf8), signature: nil, ttl: 7), signed: true, padded: false)
}
for length in [217, 218, 219, 233, 234, 235, 240, 255, 256, 1024] {
    let payload = Data((0..<length).map { UInt8(($0 * 37 + $0 / 7) % 251) })
    try append("length_\(length)", BitchatPacket(type: 2, senderID: peerID, recipientID: nil, timestamp: timestamp,
        payload: payload, signature: nil, ttl: 6), signed: true, padded: false)
}
try append("compressed", BitchatPacket(type: 2, senderID: peerID, recipientID: nil, timestamp: timestamp,
    payload: Data(repeating: 0x61, count: 1024), signature: nil, ttl: 7), signed: true, padded: false)
try append("route_v2", BitchatPacket(type: 2, senderID: peerID, recipientID: Data(repeating: 0x22, count: 8),
    timestamp: timestamp, payload: Data("route".utf8), signature: nil, ttl: 4, version: 2,
    route: [Data(repeating: 0x33, count: 8), Data(repeating: 0x44, count: 8)]), signed: true, padded: false)
try append("rsr", BitchatPacket(type: 2, senderID: peerID, recipientID: nil, timestamp: timestamp,
    payload: Data("history".utf8), signature: nil, ttl: 7, isRSR: true), signed: true, padded: false)
for type: UInt8 in [0x10, 0x11] {
    try append("noise_\(type)", BitchatPacket(type: type, senderID: peerID, recipientID: Data(repeating: 0x22, count: 8),
        timestamp: timestamp, payload: Data(repeating: 0x55, count: 32), signature: nil, ttl: 7), signed: false, padded: true)
}
let document: [String: Any] = [
    "source_commit": "30c58eee349e172b37f69faa2113c9d5f2c76c23",
    "generator": "Official BitFoundation codec; CryptoKit Ed25519; synthetic test keys only",
    "timestamp": timestamp, "cases": cases
]
let result = try JSONSerialization.data(withJSONObject: document, options: [.prettyPrinted, .sortedKeys])
print(String(data: result, encoding: .utf8)!)

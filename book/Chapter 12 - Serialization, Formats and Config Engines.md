# Chapter 12: Parsing, Serialization & Config Engines

Interchanging data across networks and microservices requires fast, robust parsing and serialization. AKS includes built-in native engines for JSON (RFC 8259), CSV (RFC 4180), TOML v1.0, INI, Regular Expressions, Base32, Base85, MessagePack, Protocol Buffers, CBOR (RFC 8949), and POSIX ustar TAR archives.

---

## 12.1 JSON (RFC 8259) Engine

The built-in JSON engine implements a non-recursive, memory-efficient scanner:

```aks
let raw_json = """
{
    "service_name": "auth-gateway",
    "port": 9001,
    "cors_allowed": true,
    "endpoints": ["/login", "/refresh", "/logout"]
}
""";

// Parse JSON string into AKS composite structure
let config = JsonParseC(raw_json);

print("Service: " + config["service_name"]);
print("Port: " + String(config["port"]));
print("First Endpoint: " + config["endpoints"][0]);

// Serialize structure back to JSON string
config["port"] = 9002;
let serialized = JsonStringifyC(config);
print("Updated JSON: " + serialized);
```

---

## 12.2 CSV (RFC 4180) Engine

Complies with RFC 4180, handling embedded commas, escaped quotes (`""`), and multiline entries:

```aks
let csv_data = """
id,name,role,salary
101,"Doe, Jane",Architect,185000
102,"Smith, John",Engineer,145000
""";

let rows = CsvParseC(csv_data);
for (row in rows) {
    print("User: " + row["name"] + " | Role: " + row["role"]);
}
```

---

## 12.3 TOML v1.0 & INI Config Parsers

```aks
let toml_source = """
[database]
server = "192.168.1.1"
ports = [ 8001, 8001, 8002 ]
connection_max = 5000
enabled = true
""";

let toml_obj = TomlParseC(toml_source);
print("DB Server: " + toml_obj["database"]["server"]);
```

---

## 12.4 Regular Expressions Engine

AKS provides a linear-time Regular Expressions engine matching character classes, quantifiers, anchors, and sub-group captures:

```aks
let pattern = "^[a-zA-Z0-9_.+-]+@[a-zA-Z0-9-]+\\.[a-zA-Z0-9-.]+$";
let email = "engineer@example.com";

let is_valid_email = RegexMatchC(pattern, email);
print("Email Validation: " + String(is_valid_email)); // true

let text = "Order ID: 10482, Amount: $99";
let replaced = RegexReplaceC("\\$[0-9]+", text, "[CONFIDENTIAL]");
print("Sanitized String: " + replaced); // "Order ID: 10482, Amount: [CONFIDENTIAL]"
```

---

## 12.5 Binary Serialization: MessagePack, CBOR & Protocol Buffers

Binary serialization formats minimize packet size and parsing overhead compared to text formats:

```aks
let packet = {
    "device_id": 4096,
    "temperatures": [21.5, 22.0, 21.8],
    "online": true
};

// MessagePack binary encoding
let msgpack_bytes = MsgPackEncodeC(packet);
print("MessagePack Packet Size: " + String(Length(msgpack_bytes)) + " bytes");

// Protocol Buffers Varint encoding
let varint_encoded = ProtobufEncodeVarintC(150); // [0x96, 0x01]
let decoded_int = ProtobufDecodeVarintC(varint_encoded);
print("Decoded Protobuf Varint: " + String(decoded_int)); // 150
```

---

## 12.6 POSIX ustar TAR Archive Engine

The TAR engine inspects, reads, and extracts files from standard uncompressed or compressed TAR archives:

```aks
let archive_file = "payloads.tar";
let entries = TarExtractListC(archive_file);

print("Archive Contents:");
for (entry in entries) {
    print("File: " + entry.filename + " (" + String(entry.size) + " bytes)");
}
```

---

## 12.7 Summary & Next Steps

In this chapter, we explored:
1. RFC 8259 JSON parsing and serialization.
2. RFC 4180 CSV tabular processing.
3. TOML v1.0 and INI configuration parsers.
4. Regular expressions pattern matching and replacements.
5. MessagePack, CBOR, and Protocol Buffers binary serializers.
6. POSIX ustar TAR archive manipulation.

Proceed to [Chapter 13: Cryptography & Security Engine](Chapter%2013%20-%20Cryptography%20and%20Security%20Engine.md) to explore cryptographic hashing, symmetric ciphers, key exchanges, and digital signatures.

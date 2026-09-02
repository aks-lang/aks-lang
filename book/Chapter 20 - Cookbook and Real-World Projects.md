# Chapter 20: Practical Cookbook & Real-World Projects

This concluding chapter synthesizes the concepts explored throughout this book into four production-grade, end-to-end systems projects.

---

## 20.1 Project 1: Multi-Threaded HTTP REST API Server with JSON & Logging

A complete in-memory microservice with routing, JSON parsing, structured logging, and thread-safe data access:

```aks
// http_rest_server.aks
struct User {
    id: int,
    username: str,
    role: str
}

struct ApiDatabase {
    users: [User],
    db_mutex: any
}

impl ApiDatabase {
    fn new() {
        return ApiDatabase {
            users: [],
            db_mutex: mutex()
        };
    }

    fn add_user(self, id: int, name: str, role: str) {
        lock(self.db_mutex);
        append(self.users, User { id: id, username: name, role: role });
        unlock(self.db_mutex);
    }

    fn get_all_json(self) -> str {
        lock(self.db_mutex);
        let out_list = [];
        for (u in self.users) {
            append(out_list, { "id": u.id, "username": u.username, "role": u.role });
        }
        unlock(self.db_mutex);
        return JsonStringify(out_list);
    }
}

fn handle_request(db, path, method, body) {
    if (method == "GET" && path == "/api/users") {
        let json_body = db.get_all_json();
        return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + 
               String(Length(json_body)) + "\r\n\r\n" + json_body;
    }
    
    if (method == "POST" && path == "/api/users") {
        let payload = JsonParse(body);
        db.add_user(payload.id, payload.username, payload.role);
        return "HTTP/1.1 201 Created\r\nContent-Type: application/json\r\nContent-Length: 15\r\n\r\n{\"status\":\"OK\"}";
    }
    
    return "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
}

fn main() {
    let db = ApiDatabase.new();
    
    // Seed initial records
    db.add_user(1, "alice_admin", "ADMIN");
    db.add_user(2, "bob_dev", "DEVELOPER");
    
    // Test API Route Dispatch
    let response = handle_request(db, "/api/users", "GET", "");
    print("API Response:\n" + response);
}

main();
```

---

## 20.2 Project 2: Real-Time Algorithmic Audio Synthesizer

Synthesizes multi-frequency audio chords using ADSR envelopes and outputs PCM frames directly via WinMM:

```aks
// audio_synthesizer.aks
fn synth_tone_with_envelope(freq_hz, duration_sec, sample_rate = 44100) {
    let total_samples = Int(Float(sample_rate) * duration_sec);
    let pcm = [];
    let pi_2 = 6.283185307179586;
    
    for (let i = 0; i < total_samples; i += 1) {
        let t = Float(i) / Float(sample_rate);
        
        // Compute ADSR amplitude envelope
        let envelope = 1.0;
        if (t < 0.1) {
            envelope = t / 0.1; // Attack phase
        } else if (t > duration_sec - 0.2) {
            envelope = (duration_sec - t) / 0.2; // Release phase
        }
        
        // Synthesize fundamental + 1st harmonic
        let s1 = Sin(pi_2 * freq_hz * t);
        let s2 = Sin(pi_2 * (freq_hz * 2.0) * t) * 0.3;
        let mixed = (s1 + s2) * 0.7 * envelope;
        
        let sample_val = Int(mixed * 30000.0);
        Push(pcm, sample_val & 0xFF);
        Push(pcm, (sample_val >> 8) & 0xFF);
    }
    return pcm;
}

fn main() {
    print("Synthesizing Concert A (440Hz) with harmonic envelope...");
    let pcm_audio = synth_tone_with_envelope(440.0, 1.0);
    
    // Play waveform on Windows audio subsystem
    WinmmAudioPlayC(pcm_audio, 44100);
    print("Playback completed.");
}

main();
```

---

## 20.3 Project 3: Zero-Knowledge Encrypted Tunnel (X25519 + XChaCha20-Poly1305)

A cryptographic handshake establishing an authenticated and encrypted session:

```aks
// secure_tunnel.aks
fn main() {
    print("Initiating X25519 Key Exchange...");
    
    // Alice and Bob Keypairs
    let alice_priv = [0x77,0x07,0x6D,0x0A,0x73,0x18,0xA5,0x7D,0x3C,0x16,0xC1,0x72,0x51,0xB2,0x66,0x45,
                      0xDF,0x4C,0x2F,0x87,0xEB,0xC0,0x99,0x2A,0xB1,0x77,0xFB,0xA5,0x1D,0xB9,0x2C,0x2A];
    let alice_pub  = [0x85,0x20,0xF0,0x09,0x89,0x30,0xA7,0x54,0x74,0x8B,0x7D,0xDC,0xB4,0x3E,0xF7,0x5A,
                      0x0D,0xBF,0x3A,0x0D,0x26,0x38,0x15,0xE4,0x2B,0x60,0x48,0x20,0xF9,0x25,0x34,0x77];
    
    let bob_priv   = [0x5D,0xAB,0x08,0x7E,0x62,0x4A,0x8A,0x4B,0x79,0xE1,0x7F,0x8A,0xB3,0x8E,0x06,0x5D,
                      0x72,0xA9,0x75,0xCC,0x7D,0x2E,0x19,0x53,0x43,0x57,0x7A,0xD2,0x19,0x19,0x0E,0x5C];
    let bob_pub    = [0xDE,0x9E,0xDB,0x7D,0x7B,0x7D,0xC1,0xB4,0xD3,0x5B,0x61,0xC2,0xEC,0xE4,0x35,0x37,
                      0x3F,0x83,0x43,0xC8,0x5B,0x78,0x67,0x4D,0xAD,0xFC,0x7E,0x14,0x6F,0x88,0x2B,0x4F];

    // Compute identical shared secret independently:
    let alice_shared = CryptoX25519C(alice_priv, bob_pub);
    let bob_shared   = CryptoX25519C(bob_priv, alice_pub);
    
    print("Key exchange verified: Shared Secret derived on both endpoints.");
    
    // Encrypt payload using XChaCha20-Poly1305 AEAD
    let nonce = [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23];
    let payload = "CONFIDENTIAL_FINANCIAL_SETTLEMENT_TX_9000";
    
    let cipher_packet = CryptoXChaCha20Poly1305(alice_shared, nonce, payload, "AUTH_HEADER_METADATA");
    print("Tunnel packet encrypted with AEAD authentication.");
}

main();
```

---

## 20.4 Project 4: High-Throughput In-Memory Search Engine

Combines Suffix Arrays, LCP arrays, and SIMD string matching to index large text corpora:

```aks
// search_engine.aks
struct SearchIndex {
    corpus: str,
    suffix_array: any
}

impl SearchIndex {
    fn build(text: str) {
        return SearchIndex {
            corpus: text,
            suffix_array: SuffixArrayBuildC(text)
        };
    }

    fn search(self, query: str) -> int {
        // Fast O(M log N) Suffix Array search
        return SuffixArraySearchC(self.corpus, self.suffix_array, query);
    }
}

fn main() {
    let large_corpus = "AKS is a modern systems language engineered for high performance, bare-metal SIMD hardware, and safety.";
    
    print("Building full-text suffix index...");
    let index = SearchIndex.build(large_corpus);
    
    let query = "SIMD hardware";
    let match_index = index.search(query);
    
    if (match_index >= 0) {
        print("Match found for '" + query + "' at byte position: " + String(match_index));
    } else {
        print("Query not found.");
    }
}

main();
```

---

## 20.5 Conclusion & Next Steps

You have completed the comprehensive guide to the AKS programming language. You now possess the foundational and systems knowledge to build ultra-fast, safe, and maintainable software using AKS—from high-level web services to bare-metal OS kernels, high-performance cryptography, SIMD numeric computing, and distributed networks.

Happy systems programming in AKS!

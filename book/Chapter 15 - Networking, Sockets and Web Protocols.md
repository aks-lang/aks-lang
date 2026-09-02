# Chapter 15: Networking, Sockets & Web Protocols

Building distributed services and network clients requires versatile network primitives. AKS provides native bindings for Windows Sockets 2 (`ws2_32.dll`), TCP/UDP sockets, multicast, streaming HTTP/1.1 parsers, RFC 6455 WebSockets, SOCKS5 proxies, SMTP email dispatchers, SNTP atomic time queries, raw DNS resolution, ICMP ping, and the native Windows WinHTTP client.

---

## 15.1 Raw TCP Sockets

AKS provides synchronous and asynchronous TCP socket APIs for building custom protocols:

```aks
// TCP Client Example
let host = "127.0.0.1";
let port = 8080;

let socket_fd = SocketConnectSync(host, port);
if (socket_fd >= 0) {
    print("Connected to " + host + ":" + String(port));
    
    // Transmit request
    let request_payload = "GET /status HTTP/1.1\r\nHost: " + host + "\r\n\r\n";
    SocketSendSync(socket_fd, request_payload);
    
    // Receive response
    let response = SocketReceiveSync(socket_fd, 4096);
    print("Received Response:\n" + response);
    
    SocketClose(socket_fd);
}
```

---

## 15.2 UDP Multicast

IP Multicast enables transmitting packets to multiple subscriber nodes simultaneously:

```aks
let udp_sock = UdpSocket();
let multicast_group = "239.255.0.1";
let port = 9000;

// Join Multicast Group
let joined = UdpMulticastJoinC(udp_sock, multicast_group);

// Send datagram to multicast group
UdpMulticastSendC(udp_sock, multicast_group, port, "TELEMETRY_BEACON_PING");
print("Multicast beacon transmitted.");
```

---

## 15.3 Streaming HTTP/1.1 Parser State Machine

The streaming parser processes incoming byte chunks incrementally without requiring the entire response to reside in memory at once:

```aks
let parser = Http11ParserNewC();

let raw_chunk1 = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 13\r\n\r\n";
let raw_chunk2 = "Hello, World!";

let state1 = Http11ParserFeedC(parser, raw_chunk1);
let state2 = Http11ParserFeedC(parser, raw_chunk2);

print("HTTP Status Code: " + String(state2.status_code)); // 200
print("Parsed Body: " + state2.body); // "Hello, World!"
```

---

## 15.4 RFC 6455 WebSockets Engine

Encodes and decodes binary/text WebSocket frames with XOR client masking:

```aks
// Encode text frame (Opcode 1 = Text, Mask = true)
let payload_text = "PING_HEARTBEAT";
let frame_bytes = WsFrameEncodeC(1, payload_text, true);

print("Encoded WebSocket Frame Length: " + String(Length(frame_bytes)) + " bytes");

// Decode frame
let decoded = WsFrameDecodeC(frame_bytes);
print("Decoded Opcode: " + String(decoded.opcode)); // 1
print("Decoded Payload: " + decoded.payload);       // "PING_HEARTBEAT"
```

---

## 15.5 SOCKS5 Proxy Tunneling

Establishes authenticated or anonymous TCP tunnels through SOCKS5 proxy firewalls:

```aks
let proxy_ip = "10.0.0.1";
let proxy_port = 1080;
let destination_host = "api.internal.network";
let destination_port = 443;

let tunnel_sock = Socks5ConnectC(proxy_ip, proxy_port, destination_host, destination_port);
if (tunnel_sock >= 0) {
    print("SOCKS5 Tunnel established to destination.");
    SocketClose(tunnel_sock);
}
```

---

## 15.6 Application Protocols & Utilities

### 15.6.1 RFC 4330 SNTP / NTP Atomic Clock Query

Queries standard NTP time servers via UDP port 123 to retrieve sub-millisecond atomic timestamps:

```aks
let epoch_time = NtpQueryTimeC("pool.ntp.org");
print("Current Atomic Epoch Timestamp: " + String(epoch_time));
```

### 15.6.2 Raw DNS Resolution

```aks
let resolved_ip = DnsResolveRawC("google.com", "A");
print("Resolved IP for google.com: " + resolved_ip);
```

### 15.6.3 ICMP IPv4 / IPv6 Network Ping

Sends raw ICMP Echo Request packets and calculates round-trip time:

```aks
let ping_ok = IcmpPingSendC("8.8.8.8", 1000); // 1000ms timeout
print("Google DNS Ping Success: " + String(ping_ok)); // true
```

### 15.6.4 Windows WinHTTP Client

Utilizes the native Windows HTTP services stack with automatic SSL/TLS certificate validation and proxy detection:

```aks
let winhttp_session = WinHttpOpenC("AKS-HTTP-Agent/1.0");
let winhttp_conn = WinHttpConnectC(winhttp_session, "example.com", 80);

let http_response = WinHttpSendRequestC(winhttp_conn, "GET", "/", "");
print("WinHTTP Status: " + String(http_response.status_code));
```

---

## 15.7 Summary & Next Steps

In this chapter, we explored:
1. Raw synchronous and asynchronous TCP sockets.
2. UDP sockets and IP multicast groups.
3. Streaming HTTP/1.1 incremental parsers.
4. RFC 6455 WebSocket frame encoding and decoding.
5. SOCKS5 proxy tunneling.
6. NTP atomic time queries, raw DNS resolution, ICMP ping, and WinHTTP integration.

Proceed to [Chapter 16: Windows OS Kernel & Low-Level Systems Integration](Chapter%2016%20-%20Windows%20OS%20Kernel%20and%20Systems%20Internals.md) to explore low-level Win32 kernel calls, virtual memory, process snapshots, and ETW.

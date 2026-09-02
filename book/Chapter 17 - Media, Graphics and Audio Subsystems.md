# Chapter 17: Media, Graphics & Audio Subsystems

Real-time audio processing, signal synthesis, image decoding, and desktop graphics rendering require direct interaction with media hardware. AKS provides native integration with RIFF/WAVE audio decoders, Windows Multimedia (`winmm.dll`) low-latency PCM playback, BMP/DIB image decoders, and the Windows Graphics Device Interface (GDI) for 32-bit bitmap manipulation.

---

## 17.1 Audio Processing & RIFF/WAVE Header Parsing

AKS parses uncompressed standard RIFF/WAVE audio headers to extract sampling rate, channel count, bit depth, and raw PCM data offsets:

```aks
let audio_file = "assets/alert.wav";
let wave_info = WavFileParseC(audio_file);

print("WAV File Metadata:");
print("Sample Rate: " + String(wave_info.sample_rate) + " Hz");
print("Channels:    " + String(wave_info.channels));
print("Bit Depth:   " + String(wave_info.bit_depth) + "-bit");
print("Data Size:   " + String(wave_info.data_size) + " bytes");
```

---

## 17.2 Real-Time Algorithmic Audio Synthesis & WinMM Playback

Generates dynamic waveforms (sine, sawtooth, square) and streams PCM audio directly to the Windows default playback endpoint using `waveOutWrite`:

```aks
fn synthesize_sine_tone(frequency_hz: float, duration_sec: float, sample_rate: int = 44100) -> [int] {
    let total_samples = Int(Float(sample_rate) * duration_sec);
    let pcm_buffer = [];
    let pi_2 = 6.283185307179586;

    for (let i = 0; i < total_samples; i += 1) {
        let t = Float(i) / Float(sample_rate);
        let sample_float = Sin(pi_2 * frequency_hz * t); // Range [-1.0, 1.0]
        
        // Quantize to 16-bit signed integer [-32768, 32767]
        let sample_i16 = Int(sample_float * 30000.0);
        
        // Push 16-bit little-endian bytes
        Push(pcm_buffer, sample_i16 & 0xFF);
        Push(pcm_buffer, (sample_i16 >> 8) & 0xFF);
    }
    return pcm_buffer;
}

fn main() {
    print("Generating Concert A4 (440 Hz) pure tone...");
    let pcm_bytes = synthesize_sine_tone(440.0, 1.0); // 1-second audio tone

    print("Streaming audio to WinMM WaveOut engine...");
    let success = WinmmAudioPlayC(pcm_bytes, 44100);
    print("Audio Playback Status: " + String(success));
}

main();
```

---

## 17.3 Image Decoding (BMP / DIB)

Decodes uncompressed 24-bit and 32-bit Windows Bitmap (BMP) files:

```aks
let bmp_path = "assets/logo.bmp";
let bmp_metadata = BmpFileParseC(bmp_path);

print("BMP Image Dimensions: " + String(bmp_metadata.width) + "x" + String(bmp_metadata.height));
print("Bits Per Pixel:       " + String(bmp_metadata.bpp));
```

---

## 17.4 Windows GDI 32-Bit DIB Section Graphics

Allocates a hardware-backed 32-bit DIB section bitmap for fast off-screen pixel rendering:

```aks
// Create 200x200 32-bit RGBA drawing canvas
let canvas = GdiBitmapCreateC(200, 200);

// Draw a gradient pattern
for (let y = 0; y < 200; y += 1) {
    for (let x = 0; x < 200; x += 1) {
        // Compute RGB color: Red varies with X, Blue varies with Y
        let r = x % 256;
        let g = 128;
        let b = y % 256;
        let color_rgb = (r << 16) | (g << 8) | b;
        
        GdiBitmapSetPixelC(canvas, x, y, color_rgb);
    }
}

// Save rendered canvas directly to disk
GdiBitmapSaveC(canvas, "output_gradient.bmp");
print("Bitmap rendered and saved successfully to output_gradient.bmp.");
```

---

## 17.5 Summary & Next Steps

In this chapter, we explored:
1. Parsing RIFF/WAVE audio headers.
2. Synthesizing PCM audio waveforms and low-latency playback via WinMM `waveOut`.
3. Decoding Windows BMP images.
4. GDI 32-bit DIB section graphics rendering and saving images to disk.

Proceed to [Chapter 18: Foreign Function Interface (FFI) & Native Interoperability](Chapter%2018%20-%20Foreign%20Function%20Interface%20and%20Interop.md) to explore calling C, Rust, Python, and COM components from AKS.

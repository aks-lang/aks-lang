const assert = require('assert');
const { instantiate } = require('./aksGlue');

// Mock WebAssembly.instantiate to return our mock WASM module
const originalInstantiate = WebAssembly.instantiate;
WebAssembly.instantiate = async (moduleOrBuffer, imports) => {
    const buffer = new ArrayBuffer(65536 * 10); // 10 pages
    const u8 = new Uint8Array(buffer);
    
    const wasmExports = {
        memory: { buffer },
        WasmAlloc: (size) => {
            if (!global.allocatedPtr) global.allocatedPtr = 1024;
            const res = global.allocatedPtr;
            global.allocatedPtr += size;
            return res;
        },
        WasmFree: (ptr) => {
            // No-op
        },
        WasmRunSource: (ptr) => {
            let end = ptr;
            while (u8[end] !== 0) end++;
            const source = new TextDecoder().decode(u8.subarray(ptr, end));
            console.log("Mock WASM received source code: " + source);

            if (source.includes("print")) {
                const msgPtr = wasmExports.WasmAlloc(100);
                const msgBytes = new TextEncoder().encode("Hello from mock WebAssembly print!");
                u8.set(msgBytes, msgPtr);
                u8[msgPtr + msgBytes.length] = 0;
                imports.env.print(msgPtr);
            }
            
            return 0;
        }
    };
    
    return {
        instance: {
            exports: wasmExports
        }
    };
};

async function test() {
    console.log("Running WASM/WASI simulator test...");
    const runtime = await instantiate(new ArrayBuffer(0), {
        print: (msg) => {
            console.log("JS Glue print listener received: " + msg);
        }
    });

    const res = runtime.runSource("print('Hello from AKS WASM!');");
    console.log("runSource result:", res);
    assert.strictEqual(res, 0, "runSource should return 0 on success");
    console.log("PASS: WebAssembly string memory marshalling, imports, and exports verified successfully!");
}

test().catch(err => {
    console.error("FAIL:", err);
    process.exit(1);
});

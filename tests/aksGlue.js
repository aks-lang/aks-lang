// Automatically generated JS glue by aks wasm-bind

class AKSRuntime {
    constructor(wasmInstance) {
        this.instance = wasmInstance;
        this.exports = wasmInstance.exports;
        this.memory = this.exports.memory;
    }

    writeString(str) {
        const bytes = new TextEncoder().encode(str);
        const ptr = this.exports.WasmAlloc(bytes.length + 1);
        const mem = new Uint8Array(this.memory.buffer);
        mem.set(bytes, ptr);
        mem[ptr + bytes.length] = 0;
        return ptr;
    }

    readString(ptr) {
        const mem = new Uint8Array(this.memory.buffer);
        let end = ptr;
        while (mem[end] !== 0) end++;
        return new TextDecoder().decode(mem.subarray(ptr, end));
    }

    runSource(source) {
        const ptr = this.writeString(source);
        const res = this.exports.WasmRunSource(ptr);
        this.exports.WasmFree(ptr);
        return res;
    }

}

async function instantiate(wasmModuleOrBuffer, imports = {}) {
    const wasiImports = {
        fd_write: (fd, iovs, iovs_len, nwritten) => {
            return 0;
        }
    };
    let instanceObj = null;
    const wasmImports = {
        wasi_snapshot_preview1: wasiImports,
        env: {
            print: (ptr) => {
                if (instanceObj) {
                    const msg = instanceObj.readString(ptr);
                    if (imports.print) imports.print(msg);
                    else console.log(msg);
                }
            }
        }
    };
    const { instance } = await WebAssembly.instantiate(wasmModuleOrBuffer, wasmImports);
    instanceObj = new AKSRuntime(instance);
    return instanceObj;
}

module.exports = { instantiate };

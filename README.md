# SUSTemu

A RISC-V simulator for computer architecture education, based on the RV64-IM ISA. Supports multiple execution modes, pipeline simulation, out-of-order execution, branch prediction, and multi-core cache coherence — suitable for architecture lab experiments and performance analysis.

## Features

### ISA Support
- **RV64-IM**: Base integer ISA + multiply/divide extension
- **Zicsr**: CSR instructions (`mhartid`, `mcycle`, etc.), supports `rdcycle` for benchmarking
- System instructions: FENCE, FENCE.I, ECALL, MRET, EBREAK

### Execution Modes

| Mode | Description | Flag |
|------|-------------|------|
| Functional (default) | Interpreted execution; correctness baseline | _(default)_ |
| In-order pipeline | 5-stage IF/ID/EX/MEM/WB with hazard handling | `--inorder` |
| Out-of-order (Tomasulo) | ROB + reservation stations + physical register file + RAT/RRAT | `--ooo` |

### Branch Prediction
- Branch Target Buffer (BTB)
- Tournament predictor: local history (LHT/LPHT) + global history (GHR/GPHT) + meta selector

### Cache Hierarchy
- Two-level cache: private L1I/L1D per core + shared L2
- Configurable sets (`s` bits) and ways (`w`); fixed 64-byte cache lines
- LRU replacement, write-back + write-allocate

### Multi-Core Simulation
- Dual-hart simulation with independent L1I/L1D, pipeline/OOO state, and branch predictor per core
- Write-invalidate cache coherence protocol
- FENCE instruction enforces memory ordering across OOO cores

### Peripherals
- Serial (UART output for bare-metal programs)
- Flash, RTC, VGA, Keyboard

---

## Building and Running

### Dependencies

#### Ubuntu / Linux
```
gcc / g++   llvm-11 (for disassembly)   make
libSDL2-dev   libreadline-dev
```

#### macOS (Apple Silicon / Intel)
Install dependencies via Homebrew:
```bash
brew install llvm sdl2 readline riscv-gnu-toolchain python3
pip3 install kconfiglib
```

**Notes for macOS:**
- The build system auto-detects macOS (`uname -s`) and switches to `clang`/`clang++` with Homebrew paths.
- LLVM 20+ is supported (the disassembler code is compatible with LLVM ≥ 11).
- `kconfiglib` replaces the Linux-specific `mconf`/`conf` tools for `make menuconfig`.

### Build
```bash
make
```

### Run the default test
```bash
make test          # runs test/kernel.bin; exit code 0 = pass
```

### Run a custom image
```bash
./build/sustemu <image.bin>                    # functional mode
./build/sustemu --inorder <image.bin>          # in-order pipeline
./build/sustemu --ooo <image.bin>              # out-of-order execution
./build/sustemu --ooo --dual <image.bin>       # dual-core OOO
```

### Dual-core test
```bash
cd test/dual && make
./build/sustemu --ooo --dual test/dual/dual.bin
```

---

## Acknowledgements

Inspired by [NEMU](https://github.com/NJU-ProjectN/nemu) (Nanjing University emulator). Extended for architecture education with pipeline, OOO, branch prediction, and multi-core modules.

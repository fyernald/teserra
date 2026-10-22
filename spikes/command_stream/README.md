# Spike #15: command-stream recording model

Spike code, never merged (docs/process/development-process.md#spikes). Findings and the recommendation are on issue #15.

`command_stream_spike.cpp` records a representative frame into a byte stream and replays it into a mock backend that counts calls, in four variants:

- **packed:** `{u16 type, u16 size}` header, payload right after it, records on 4-byte boundaries, payloads read back with `memcpy`.
- **aligned:** the same, with records on 8-byte boundaries and payloads read in place.
- **variant:** `std::vector<std::variant<...>>` with variable-size data in side vectors.
- **direct:** no recording; virtual calls straight into the backend, as a lower bound.

The workload is one render pass and N batches of `bindGroup` (1 dynamic offset), `bindVertexBuffers` (2 bindings) and `draw`, with a pipeline change every 50 batches and a debug group every 250. Variable-size payloads: vertex buffer arrays, dynamic offsets and debug strings.

Build and run from a Visual Studio x64 developer prompt:

```bat
cl /nologo /std:c++20 /O2 /DNDEBUG /EHsc /W4 /permissive- /utf-8 /I engine\core\include spikes\command_stream\command_stream_spike.cpp
command_stream_spike.exe 3330
```

The argument is the number of batches; 3330 gives about 10k commands.

## Results

Measured on an Intel Core i5-13420H with MSVC 19.51, `/O2`. Each figure is the median of 1000 frames after 50 warm-up frames; three runs per size agreed within a few percent.

| Commands | Variant | Record µs | Replay µs | ns/command | Bytes |
|---:|---|---:|---:|---:|---:|
| 10,088 | packed | 15.3 | 18.6 | 3.36 | 294,420 |
| 10,088 | aligned | 15.8 | 18.4 | 3.39 | 348,264 |
| 10,088 | variant | 58.9 | 17.9 | 7.61 | 1,613,072 |
| 10,088 | direct | 13.9 | - | 1.38 | - |
| 30,283 | packed | 49.1 | 55.8 | 3.46 | 883,704 |
| 30,283 | aligned | 47.6 | 55.2 | 3.39 | 1,045,320 |
| 30,283 | variant | 194.9 | 81.0 | 9.11 | 4,842,364 |

No variant allocated after warm-up.

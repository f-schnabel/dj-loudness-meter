# Project constraints

- Keep runtime CPU, memory, allocations, I/O, and background work low; app must remain suitable for old or weak hardware.
- Prefer lazy initialization, cached reads, and low-frequency polling for non-audio telemetry.
- Avoid heavy dependencies unless needed for hardware compatibility and inactive on systems that do not need them.

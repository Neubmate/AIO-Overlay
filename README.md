# AIO-Overlay

A lightweight hardware monitoring and control tool built to experiment with low-level system access. 

It uses an SMU wrapper specifically for AMD CPUs to read and tweak parameters in real-time. For GPUs and other motherboard sensors, it currently relies on standard APIs (still looking for a good low-level alternative).

## Features
- **AMD CPU Control:** Low-level access via SMU wrapper to monitor usage, temperatures, and tweak CPU parameters in real time.
- **GPU & Mobo Monitoring:** Reads GPU stats, motherboard temperatures, and voltage rails (5V, 12V).
- **Fan Control:** Real-time fan speed adjustments based on sensor data.
- **Overlay:** Lightweight OSD for real-time stats.

## Roadmap / WIP
- [ ] Find a proper low-level alternative for GPU monitoring/control to replace the current APIs.
- [ ] Refactor existing standard API calls for better performance.

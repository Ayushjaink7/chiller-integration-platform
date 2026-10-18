# Benchmark: one PID control app on 3 chiller models

Scenario: Same as scenario.json but with back-calculation anti-windup turned off, for comparison.

Setpoint 7.0 C, PID Kp=25 Ki=0.15 Kd=0 (identical for every model), settle band +/-0.25 C, time step 1 s, 1500 s per run.

| Model    | Type        | Capacity control   | Undershoot | Settling    | Peak rise after step | Recovery after step | Mean error (last 300 s) | Max error (last 300 s) | Compressor starts | Records in SQLite |
|----------|-------------|--------------------|------------|-------------|----------------------|---------------------|-------------------------|------------------------|-------------------|-------------------|
| CEN-2000 | centrifugal | inlet_guide_vanes  | 0.12 C     | 269 s       | 0.92 C               | 310 s               | 0.017 C                 | +/-0.07 C              | 1                 | 1500              |
| SCR-1000 | screw       | slide_valve        | 0.07 C     | 275 s       | 1.03 C               | 286 s               | 0.015 C                 | +/-0.06 C              | 1                 | 1500              |
| SRL-400  | scroll      | compressor_staging | 0.62 C     | not reached | 2.16 C               | 898 s               | 0.218 C                 | +/-0.47 C              | 11                | 1500              |

4500 standardized black-box records written to SQLite in 18 ms.

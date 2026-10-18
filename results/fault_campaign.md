# Fault-injection campaign

Each fault is injected at t = 900 s into the normal scenario and watched for 400 s. Native model codes are mapped to standard codes F01-F06.

| Model    | Injected fault        | Expected | Native code | Detected | Latency | Tripped |
|----------|-----------------------|----------|-------------|----------|---------|---------|
| CEN-2000 | sensor_failure        | F01      | CEN-101     | yes      | 0 s     | yes     |
| CEN-2000 | condenser_fan_failure | F02      | CEN-204     | yes      | 28 s    | yes     |
| CEN-2000 | pump_failure          | F03      | CEN-207     | yes      | 4 s     | yes     |
| CEN-2000 | capacity_stuck_high   | F06      | CEN-512     | yes      | 31 s    | yes     |
| CEN-2000 | compressor_protection | F05      | CEN-415     | yes      | 4 s     | yes     |
| SCR-1000 | sensor_failure        | F01      | SCR-E01     | yes      | 0 s     | yes     |
| SCR-1000 | condenser_fan_failure | F02      | SCR-E12     | yes      | 28 s    | yes     |
| SCR-1000 | pump_failure          | F03      | SCR-E15     | yes      | 4 s     | yes     |
| SCR-1000 | capacity_stuck_high   | F06      | SCR-E24     | yes      | 38 s    | yes     |
| SCR-1000 | compressor_protection | F05      | SCR-E21     | yes      | 8 s     | yes     |
| SRL-400  | sensor_failure        | F01      | SRL-A1      | yes      | 0 s     | yes     |
| SRL-400  | condenser_fan_failure | F02      | SRL-B3      | yes      | 33 s    | yes     |
| SRL-400  | pump_failure          | F03      | SRL-B4      | yes      | 4 s     | yes     |
| SRL-400  | capacity_stuck_high   | F06      | SRL-D9      | yes      | 12 s    | yes     |
| SRL-400  | compressor_protection | F05      | SRL-D7      | yes      | 19 s    | yes     |

Detected 15 / 15 injected faults with the correct standard code, mean latency 14.2 s, max 38 s. False alarms in 3 fault-free runs: 0.

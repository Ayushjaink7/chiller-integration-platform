# Interview notes: Multi-Model Chiller Integration Platform

## The 30-second version

"Chiller makers sell many models with different compressors, sensors and fault codes. I built a C++17 platform where one PID control application runs unchanged on three simulated models (screw, centrifugal and scroll) through a hardware abstraction layer. Each model is one class plus one JSON config. All models log the same black-box record to SQLite and map their own fault codes to six standard codes. It caught 15 of 15 injected faults with no false alarms, and a React dashboard shows it live through a REST API."

## Numbers to remember

- 3 models, 1 control app, same PID gains (Kp 25, Ki 0.15, Kd 0)
- Continuous models hold 7 °C within **±0.07 °C**; the 4-stage scroll within **±0.65 °C**
- Back-calculation on the scroll: peak rise **2.16 → 1.03 °C**, compressor starts **11 → 8**
- Faults: **15/15** detected, mean **14 s**, worst 38 s, **0** false alarms
- **93** GoogleTest cases; the Windows build gives the same numbers as Linux

## Likely questions

**Why an interface with virtual functions?**
The control code must not know which chiller it drives. `ControlApp` holds a `ChillerModel&` and calls `readSensors`, `setCapacity`, `checkFaults`, `step`. Each model overrides only what is different. A factory creates the right class from the `type` field in the JSON, so adding a model never touches the control code.

**What exactly differs between the models?**
How a capacity request becomes hardware action. Screw: slide valve, anything from 25 to 100 %. Centrifugal: guide vanes with a 30 % minimum, because at low load the compressor surges. Scroll: four on/off compressors, so only 25 % steps, with a deadband and a 60 s minimum between switches so compressors don't short-cycle. Also the model-specific sensors (oil pressure / surge margin / discharge temperature) and their protection.

**The scroll can only give 75 or 100 %. What does the PID do at 80 % load?**
It limit-cycles: three compressors are slightly too few, four are too many, so the water swings about ±0.65 °C. That is physics. What I fixed is integral windup: while the model is waiting for its next stage, the integral used to keep growing and caused big overshoots. Now the PID gets told what the model actually commanded and pulls its integral toward that (back-calculation, tracking time 60 s). For the smooth models, commanded equals requested, so nothing changes for them.

**Explain your PID.**
Error = measured − setpoint (positive means water too warm, so more cooling). Output is clamped to 0–100 %. Anti-windup: the integral stops while the output is saturated and the error pushes it further. Derivative on measurement, so changing the setpoint doesn't cause a spike. Kd is 0 because the thermal loop is slow and the sensor is noisy, which makes it a PI controller, normal for chillers.

**How did you tune it?**
I ran the benchmark over a small grid of Kp and Ki values. Larger gains reacted faster but either undershot more or didn't settle inside ±0.25 °C before the load step, because the compressor lags 12–20 s behind the command. Kp 25, Ki 0.15 settled cleanly on all three models.

**How does fault detection work?**
Limit checks on each reading: sensor valid range, condenser pressure, flow, freeze limit, and the model-specific protection. Any new fault trips the chiller (capacity to zero) and is latched until an operator reset. F06 is model-based: I run the command through the same lag a healthy actuator has, and if the measured capacity stays more than 15 % away for 10 s, the actuator is not obeying. That caught a stuck actuator in 12–38 s instead of 300+ s waiting for the freeze limit.

**Why doesn't a broken sensor trigger the freeze alarm?**
The freeze check only runs on a valid reading. A −40 °C open-circuit value is a sensor failure, not cold water. There is a unit test for exactly that.

**What is in the black-box record, and why SQLite?**
Same columns for every model (time, setpoint, load, supply/return temperature, flow, condenser pressure, power, capacity requested/commanded/delivered, state, standard and native fault codes) plus `extras_json` for model-specific sensors. SQLite is a single file, needs no server, and is common in embedded Linux. I batch inserts in transactions with prepared statements and WAL mode: 4,500 records in under 20 ms.

**How is the server thread-safe?**
The simulation runs on its own thread and ticks at `dt / speed`. Every public method of `LiveSimulation` and `BlackBoxLogger` takes a mutex, so HTTP handler threads and the simulation thread never touch state at the same time.

**How do you know the results are reproducible?**
The noise uses a fixed seed and Box-Muller on raw `mt19937` output, which the C++ standard fully specifies, unlike `std::normal_distribution`. The Windows build produces the identical benchmark table. A unit test checks that the same seed gives identical runs.

**What would change on real hardware?**
`SimulatedChiller` would be replaced by a class that reads real sensors and writes real actuators, for example over Modbus RTU or BACnet. `ControlApp`, the PID, the fault mapping and the logger stay the same. On an RTOS or Linux with PREEMPT_RT, the control tick would be a periodic real-time task.

**Is this real Carrier data?**
No. It's a simulation with plausible parameters and a lumped physical model. The point is the architecture: one control app, a HAL, configuration per model, and a standard record and fault format.

## Things to be able to point to in the code

- `include/chiller/chiller_model.hpp`: the HAL interface
- `src/scroll_chiller.cpp`: `translateRequest`, the staging logic
- `src/pid_controller.cpp`: `update` and `applyActuatorFeedback`
- `src/simulated_chiller.cpp`: physics in `step`, F06 residual check
- `src/control_app.cpp`: the six steps of one tick
- `tests/test_faults.cpp`: the parameterized 3 × 5 fault campaign

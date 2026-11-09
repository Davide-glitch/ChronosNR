# ChronosNR: Dual-Timescale 5G NR eMBB & URLLC MAC Scheduler

**ChronosNR** is a discrete-event C++17 simulation of a **5G New Radio (NR) Downlink MAC Scheduler** in **OMNeT++**. It models dual-timescale resource allocation (`1.0 ms` eMBB slots and `0.1 ms` URLLC minislots), dynamic **URLLC minislot superposition/puncturing**, copy-on-send retention buffering, and **high-priority eMBB retransmission**.

Modeled on the 3GPP Release 15/16 superposition/puncturing framework and the theoretical analysis in:
> A. Anand, G. de Veciana, and S. Shakkottai, *"Joint Scheduling of URLLC and eMBB Traffic in 5G Wireless Networks,"* **IEEE/ACM Transactions on Networking**, vol. 28, no. 2, pp. 477–490, April 2020.

---

## Architecture & Data Flow

```
====================================================================================================
                             ChronosNRNetwork (ChronosNRNetwork.ned)
====================================================================================================

  +----------------------------------------------------+
  |               embbScheduler (1.0 ms)               |  [Defined in: SchedulersAndSink.cc]
  |  - Reads queue lengths via ChronosNRModules.h      |  - Runs at Priority 2 every 1.0 ms
  |  - Serves highPrioRetxQueue FIRST                  |  - Policies: WRR (W=6,2,1), PF, MAX_CI, RR
  +----------------------------------------------------+
                            |
                            | SchedGrant (assignedPrb = b) [ChronosNRMsg.msg]
                            v
  +----------------------------------------------------+
  |         embbUser[0..4] (Compound Module)           |  [Defined in: UserModules.cc]
  |                                                    |
  |  +-------------------+    +---------------------+  |     1. Pops block from highPrioRetxQueue
  |  |        gen        |--->|       buffer        |  |        (or waitingQueue)
  |  | (PacketGenerator) |    |  (EmbbUserBuffer)   |  |     2. Sends ONLY A COPY: origBlock->dup()
  |  +-------------------+    |                     |  |     3. Keeps original in keptQueue
  |                           |  - waitingQueue     |  |
  |                           |  - keptQueue        |  |======== eMBB_Copy ========+
  |                           |  - highPrioRetxQueue|  |                           |
  |                           +---------------------+  |                           |
  +----------------------------------^-----------------+                           |
                                     |                                             v
              PrbFeedback            |                            +--------------------------------+
     - ACK_INTACT: delete keptOrig   |                            |          prb[0..9]             |
     - NACK_OVERWRITTEN: move        +----------------------------|         (PrbChannel)           |
       keptOrig -> highPrioRetxQueue       (At 1.0 ms Slot End)   |   [Defined in: PrbChannel.cc]  |
                                                                  |                                |
  +----------------------------------------------------+          |  States in Qtenv GUI:          |
  |           urllcUser (Compound Module)              |          |  - GREEN : FREE                |
  |                                                    |          |  - BLUE  : eMBB_Copy active    |
  |  +-------------------+    +---------------------+  |          |  - CYAN  : FREE -> URLLC       |
  |  |        gen        |--->|       buffer        |  |          |  - RED   : PUNCTURED (xCount)  |
  |  | (PacketGenerator) |    | (UrllcQueueModule)  |  |          +--------------------------------+
  |  +-------------------+    +---------------------+  |                    ^              |
  +----------------------------------^-----------------+                    |              |
                                     |                     URLLC_Block      |              |
                            +--------+--------------------------------------+              |
                            |                                                              |
                            | SchedGrant (Free PRB first; else Uniform Random Puncture)    |
  +-------------------------+--------------------------+                                   |
  |              urllcScheduler (0.1 ms)               |  [Defined in: SchedulersAndSink.cc]
  |  - Wakes up every 0.1 ms minislot (Priority 4)     |                                   |
  |  - Scans prb[0..9] for FREE channel first          |                                   v
  |  - If all full: overwrites an eMBB-occupied PRB    |                     +--------------------+
  +----------------------------------------------------+                     |        sink        |
                                                                             |    (RadioSink)     |
                                                                             | [SchedulersAndSink]|
                                                                             +--------------------+
```

---

## Directory Structure

```text
ChronosNR/
├── src/                                  # C++17 Simple Modules, NED Topology, & Message DSL
│   ├── package.ned                       # Declares NED package chronosnr.src
│   ├── ChronosNRMsg.msg                  # FiveGBlock, SchedGrant, PrbFeedback definitions
│   ├── ChronosNRNetwork.ned              # Compound User modules, Schedulers, PRBs, Sink
│   ├── ChronosNRModules.h                # Shared C++ interfaces & O(1) queue/channel getters
│   ├── PrbChannel.cc                     # Shared radio channel state machine (1ms / 0.1ms)
│   ├── UserModules.cc                    # Generators & 3-Queue Retention Buffers
│   └── SchedulersAndSink.cc              # EmbbScheduler, UrllcScheduler, RadioSink
├── simulations/                          # Simulation Configurations & Telemetry Outputs
│   ├── omnetpp.ini                       # General, HeavyPuncturing, LoadAndPolicySweep
│   └── results/                          # Scalar (.sca) and vector (.vec) outputs
├── scripts/                              # Post-Processing & Plotting Automation
│   └── plot_results.py                   # Parses simulations/results/*.sca -> docs/plots/*.png
├── docs/
│   └── plots/                            # Output directory for generated PNG charts
├── .gitignore                            # Ignores build artifacts and raw telemetry
└── README.md                             # Documentation & architecture
```

---

## Build and Execution Guide

Ensure your OMNeT++ environment is loaded (`opp_env shell` or `setenv`).

### 1. Build the Binary
```bash
cd src
opp_makemake -f --deep -O ../out
make -j$(nproc)
```

### 2. Run Interactive GUI (Qtenv)
```bash
cd ../simulations
../out/gcc-release/src/ChronosNR -u Qtenv -n ../src:. omnetpp.ini
```

### 3. Run Experiments (Cmdenv) & Generate Charts
```bash
cd ../simulations

# Run heavy puncturing stress test
../out/gcc-release/src/ChronosNR -u Cmdenv -c HeavyPuncturing -n ../src:. omnetpp.ini

# Run 16-run policy & load matrix (WRR, PF, MAX_CI, RR across 30%, 50%, 70%, 90% load)
../out/gcc-release/src/ChronosNR -u Cmdenv -c LoadAndPolicySweep -n ../src:. omnetpp.ini

# Generate plots
python3 ../scripts/plot_results.py
```

---

## Evaluation Benchmark Charts

When the simulation runs and the plotting script executes, graphs are populated in `docs/plots/`:

### 1. eMBB Delay vs. Network Load
![Mean eMBB Delay vs Network Load](docs/plots/plot1_delay_vs_load.png)

### 2. User Priority Class Differentiation (WRR: HP=6, MP=2, LP=1)
![WRR Delay by User Priority](docs/plots/plot2_priority_weights.png)

### 3. Dual-Timescale Latency: eMBB (1.0 ms) vs. URLLC (0.1 ms)
![Dual-Timescale Latency](docs/plots/plot3_embb_vs_urllc.png)

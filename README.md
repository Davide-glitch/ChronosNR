# ChronosNR: Dual-Timescale 5G NR eMBB & URLLC MAC Scheduler

**ChronosNR** is a discrete-event C++17 simulation of a **5G New Radio (NR) Downlink MAC Scheduler** in **OMNeT++**. It models dual-timescale resource allocation (`1.0 ms` eMBB slots and `0.1 ms` URLLC minislots), dynamic **URLLC minislot superposition/puncturing**, copy-on-send retention buffering, and **high-priority eMBB retransmission**.

Modeled on the 3GPP Release 15/16 superposition/puncturing framework and the theoretical analysis in:
> A. Anand, G. de Veciana, and S. Shakkottai, *"Joint Scheduling of URLLC and eMBB Traffic in 5G Wireless Networks,"* **IEEE/ACM Transactions on Networking**, vol. 28, no. 2, pp. 477–490, April 2020.

---

## 1. End-to-End Infrastructure & Build Pipeline

```
+===================================================================================================+
|                              CHRONOSNR INFRASTRUCTURE & BUILD PIPELINE                            |
+===================================================================================================+

  [1. SOURCE LAYER: ChronosNR/src/]
  +--------------------------+
  |    ChronosNRMsg.msg      | ---> (opp_msgc compiler) ---> ChronosNRMsg_m.h / ChronosNRMsg_m.cc
  | - FiveGBlock             |                               (Auto-generates deep-copy dup() logic)
  | - SchedGrant             |                                                 |
  | - PrbFeedback            |                                                 v
  +--------------------------+                               +-----------------------------------+
  |  ChronosNRModules.h      | ----------------------------> |       g++ / clang++ (C++17)       |
  |  PrbChannel.cc           | ----------------------------> |   Links against OMNeT++ libraries |
  |  UserModules.cc          | ----------------------------> |   (-loppsim, -loppqtenv, -loppcmd)|
  |  SchedulersAndSink.cc    | ----------------------------> +-----------------------------------+
  +--------------------------+                                                 |
                                                                               v
  [2. RUNTIME & CONFIGURATION LAYER]                         +-----------------------------------+
  +--------------------------+                               |           BINARY EXECUTABLE       |
  | src/ChronosNRNetwork.ned | === Dynamic NED Loader =====> |        out/*/src (ChronosNR)      |
  | src/package.ned          |                               +-----------------------------------+
  +--------------------------+                                        |                 |
  | simulations/omnetpp.ini  | === Injects Configs ==========>        |                 |
  | - [General]              |                                        v                 v
  | - [HeavyPuncturing]      |                             +----------------+   +----------------+
  | - [LoadAndPolicySweep]   |                             |   Qtenv GUI    |   |   Cmdenv CLI   |
  +--------------------------+                             | (Live Visual   |   | (Fast Batch    |
                                                           |  Inspection)   |   |  16-Run Sweep) |
                                                           +----------------+   +----------------+
                                                                                        |
  [3. TELEMETRY & ANALYTICS PIPELINE]                                                   v
  +-----------------------------------+                      +-----------------------------------+
  |  docs/plots/*.png                 | <=================== |     simulations/results/*.sca     |
  |  - plot1_delay_vs_load.png        |   scripts/           | - System_eMBB_Mean_Delay_ms       |
  |  - plot2_priority_weights.png     |   plot_results.py    | - System_URLLC_Mean_Delay_ms      |
  |  - plot3_embb_vs_urllc.png        |   (Matplotlib)       | - User_0..4_Mean_Delay_ms         |
  +-----------------------------------+                      +-----------------------------------+
```

---

## 2. Logical Module & Gate Topology Architecture (`ChronosNRNetwork`)

```
+===================================================================================================+
|                        CHRONOSNR LOGICAL NETWORK ARCHITECTURE (chronosnr.src)                     |
+===================================================================================================+

   +--------------------------------------------------------------------+
   |                        embbScheduler (Simple)                      |
   |  Class: EmbbScheduler [src/SchedulersAndSink.cc]                   |
   |  - Clock: cycleTimer = 1.0 ms (Slot Boundary, Priority 2)          |
   |  - Policies: WRR (W = 6,2,2,1,1), PF (Eq. 4-6), MAX_CI, RR         |
   |  - C++ Direct Inspection: reads getRetxQueueLength() &             |
   |    getWaitingQueueLength() from EmbbUserBuffer; reads              |
   |    isCompletelyFree() from PrbChannel                              |
   +--------------------------------------------------------------------+
        |                                  ^
        | Gate: grantOut[u]                | C++ O(1) Pointer Inspection
        | Msg:  SchedGrant(assignedPrb=b)  | (ChronosNRModules.h)
        v                                  |
   +====================================================================+
   |              embbUser[0 .. numUsers-1] (Compound Module)           |
   |                                                                    |
   |  +----------------------+          +----------------------------+  |
   |  |         gen          |          |           buffer           |  |
   |  |  (PacketGenerator)   |   in     |     (EmbbUserBuffer)       |  |
   |  |                      | -------> |  [src/UserModules.cc]      |  |
   |  | - trafficClass = 0   | FiveG    |                            |  |
   |  | - userWeight = W[u]  | Block    |  1. highPrioRetxQueue (P1) |  |
   |  | - interArrivalTime   | (Prio 3) |  2. waitingQueue      (P2) |  |
   |  +----------------------+          |  3. keptQueue (Retention)  |  |
   |                                    |                            |  |
   |                                    |  On SchedGrant(b):         |  |
   |                                    |  - Pop origBlock (P1 > P2) |  |
   |                                    |  - Send origBlock->dup()   |  |
   |                                    |  - Insert origBlock into   |  |
   |                                    |    keptQueue               |  |
   |                                    +----------------------------+  |
   +=======================================|======================^=====+
                                           |                      |
                  Gate: prbOut[b]          |                      | Gate: feedbackIn[b]
                  Msg:  FiveGBlock         |                      | Msg:  PrbFeedback (Priority 1)
                        ("eMBB_Copy",      |                      |       - ACK_INTACT:
                         isCopy=true,      |                      |         delete keptOrig
                         Priority 2)       |                      |       - NACK_OVERWRITTEN:
                                           v                      |         keptOrig -> highPrioRetxQueue
                                  +=========================================+
                                  |       prb[0 .. numPRBs-1] (Simple)      |
                                  |       Class: PrbChannel                 |
                                  |       [src/PrbChannel.cc]               |
                                  |-----------------------------------------|
                                  | State Variables:                        |
                                  | - currentEmbbCopy (FiveGBlock*)         |
                                  | - currentUrllcBlock (FiveGBlock*)       |
                                  | - overwrittenByUrllc (bool)             |
                                  | - overwriteCountThisSlot (int)          |
                                  |                                         |
                                  | Timers (Priority 0):                    |
                                  | - endEmbbSlotTimer (1.0 ms)             |
                                  | - endUrllcMinislotTimer (0.1 ms)        |
                                  |                                         |
                                  | Qtenv Visual States:                    |
                                  | - GREEN : FREE                          |
                                  | - BLUE  : eMBB U# Block#                |
                                  | - CYAN  : FREE->URLLC                   |
                                  | - RED   : PUNCTURED U# (xCount)         |
                                  +=========================================+
                                           ^                      |
                  Gate: prbOut[b]          |                      | Gate: out --> sink.in[b]
                  Msg:  FiveGBlock         |                      | Msg:  - URLLC_Block (every 0.1 ms)
                        ("URLLC_Block",    |                      |       - Intact eMBB_Copy (at 1.0 ms
                         Priority 4)       |                      |         if overwrittenByUrllc==false)
                                           |                      v
   +=======================================|============================+      +------------------------+
   |                 urllcUser (Compound Module)                        |      |      sink (Simple)     |
   |                                                                    |      |   Class: RadioSink     |
   |  +----------------------+          +----------------------------+  |      | [SchedulersAndSink.cc] |
   |  |         gen          |   in     |           buffer           |  |      |------------------------|
   |  |  (PacketGenerator)   | -------> |    (UrllcQueueModule)      |  |      | Records to .sca/.vec:  |
   |  | - trafficClass = 1   | FiveG    |    [src/UserModules.cc]    |  |      | - embbDelay            |
   |  | - Exponential IAT    | Block    | - urllcWaitingQueue        |  |      | - urllcDelay           |
   |  +----------------------+ (Prio 3) +----------------------------+  |      | - User_0..4_Mean_Delay |
   +=======================================^============================+      | - RetxCount Histogram  |
                                           |                                   +------------------------+
        Gate: grantOut                     |
        Msg:  SchedGrant(assignedPrb=b)    |
              (Priority 4)                 |
   +--------------------------------------------------------------------+
   |                       urllcScheduler (Simple)                      |
   |  Class: UrllcScheduler [src/SchedulersAndSink.cc]                  |
   |  - Clock: cycleTimer = 0.1 ms (Minislot Boundary, Priority 4)      |
   |  - Rule 1: Scan prb[0..9] for isCompletelyFree() -> Schedule Free  |
   |  - Rule 2: If all occupied -> Pick isOccupiedByEmbb() PRB          |
   |            Uniformly at Random (Theorem 1) -> Overwrite/Puncture   |
   +--------------------------------------------------------------------+
```

---

## 3. Discrete-Event State Machine & FES Priority Timeline

Because `1.0 ms` is an exact multiple of `0.1 ms`, multiple simulation events coincide at identical timestamps ($t = k \times 1.0\text{ ms}$). ChronosNR enforces strict deterministic ordering inside OMNeT++'s Future Event Set (FES) via explicit message priorities (`0` = highest / earliest, `4` = lowest):

```
+===================================================================================================+
|             DETERMINISTIC FES EVENT ORDERING AT SLOT BOUNDARIES (t = k * 1.0 ms)                  |
+===================================================================================================+

  SIMULATION TIMESTAMP: t = 1.000 ms
  |
  |--► [PRIORITY 0] PrbChannel::endUrllcMinislotTimer & PrbChannel::endEmbbSlotTimer
  |      1. Any URLLC block finishing at t = 1.0 ms is delivered to RadioSink.
  |      2. Each PRB evaluates its 1.0 ms eMBB copy:
  |         - Emits PrbFeedback (Priority 1) to embbUser[ownerUser].
  |         - If (overwrittenByUrllc == false): forwards intact eMBB_Copy to RadioSink.
  |         - If (overwrittenByUrllc == true) : deletes corrupted eMBB_Copy.
  |         - Resets PRB state to FREE (currentEmbbCopy = nullptr, overwrittenByUrllc = false).
  |
  |--► [PRIORITY 1] EmbbUserBuffer::handleMessage(feedbackIn)
  |      1. Looks up keptOrig inside keptQueue by blockId.
  |      2. Removes keptOrig from keptQueue.
  |      3. Branches on PrbFeedback.overwritten:
  |         - FALSE (Intact)     --> delete keptOrig (drops retained copy as required).
  |         - TRUE  (Punctured)  --> keptOrig->retxCount++; inserts keptOrig into highPrioRetxQueue.
  |
  |--► [PRIORITY 2] EmbbScheduler::cycleTimer & SchedGrant --> EmbbUserBuffer::handleMessage(grantIn)
  |      1. EmbbScheduler reads updated highPrioRetxQueue and waitingQueue lengths across all users.
  |      2. For each FREE PRB b = 0 .. numPRBs-1:
  |         - PASS 1 (Strict High Priority): Allocates PRB to any user with highPrioRetxQueue > 0.
  |         - PASS 2 (Normal Scheduling)   : Allocates PRB via WRR, PF (Iterative Gradient), MAX_CI, or RR.
  |         - Terminates loop immediately if all PRBs are allocated OR all user queues are empty.
  |      3. EmbbUserBuffer pops origBlock, sends origBlock->dup() ("eMBB_Copy") to prb[b],
  |         and stores origBlock in keptQueue for the new 1.0 ms cycle.
  |
  |--► [PRIORITY 3] PacketGenerator::sendTimer
  |      1. New eMBB and URLLC blocks arrive and enter waitingQueue / urllcWaitingQueue.
  |
  +--► [PRIORITY 4] UrllcScheduler::cycleTimer (Fires at t = 0.0, 0.1, 0.2, ..., 1.0 ms)
         1. Checks urllcWaitingQueue length.
         2. While URLLC blocks are waiting:
            - STEP A (Free PRBs First): Scans prb[0..9] for isCompletelyFree().
              If found -> sends URLLC_Free_Grant -> PRB turns CYAN (FREE->URLLC).
            - STEP B (Puncturing)     : If all PRBs are occupied by eMBB -> selects an eMBB-occupied
              PRB uniformly at random -> sends URLLC_Puncture_Grant -> PrbChannel sets
              overwrittenByUrllc = true and turns RED (PUNCTURED).
```

---

## 4. File-to-Architecture Responsibility Matrix

| Directory & File | Layer | Component / Class Name | Core Architectural Responsibility |
| :--- | :--- | :--- | :--- |
| `src/package.ned` | NED Package | `chronosnr.src` | Registers the NED namespace so `simulations/omnetpp.ini` can resolve `chronosnr.src.ChronosNRNetwork`. |
| `src/ChronosNRMsg.msg` | Data / Signaling | `FiveGBlock`, `SchedGrant`, `PrbFeedback` | Defines packet fields (`trafficClass`, `blockId`, `userId`, `retxCount`, `overwritten`) and generates `FiveGBlock::dup()` for copy-on-send. |
| `src/ChronosNRNetwork.ned` | Topology DSL | `ChronosNRNetwork`, `EmbbUser`, `UrllcUser` | Wires compound users (gen + buffer), dual schedulers (1.0 ms & 0.1 ms), shared `prb[0..9]` channels, and sink. |
| `src/ChronosNRModules.h` | C++ Interface | `PrbChannel`, `EmbbUserBuffer`, `UrllcQueueModule` | Provides $O(1)$ C++ getters so schedulers can inspect queue depths and channel occupancy states without extra message delay. |
| `src/PrbChannel.cc` | Physical/MAC Channel | `PrbChannel` | Manages the shared PRB lifecycle (1.0 ms eMBB slot vs. 0.1 ms URLLC minislot), tracks URLLC overwriting, and sends `ACK_INTACT` / `NACK_OVERWRITTEN` feedback. |
| `src/UserModules.cc` | Traffic & Buffering | `PacketGenerator`, `EmbbUserBuffer`, `UrllcQueueModule` | Implements exponential packet generation and the 3-queue eMBB retention lifecycle (`waitingQueue`, `keptQueue`, `highPrioRetxQueue`). |
| `src/SchedulersAndSink.cc` | Control & Telemetry | `EmbbScheduler`, `UrllcScheduler`, `RadioSink` | Executes 1.0 ms eMBB scheduling (WRR, PF, MAX_CI, RR), 0.1 ms URLLC free/puncture scheduling, and records per-user and system latency scalars. |
| `simulations/omnetpp.ini` | Experiment Config | `[General]`, `[HeavyPuncturing]`, `[LoadAndPolicySweep]` | Parameterizes traffic arrival rates (30%, 50%, 70%, 90% load), scheduling policies, user counts, and PRB counts. |
| `scripts/plot_results.py` | Analytics | `parse_sca()`, `matplotlib` | Parses `simulations/results/LoadAndPolicySweep-*.sca` and renders the 3 benchmark charts into `docs/plots/*.png`. |

---

## 5. Build and Execution Guide (WSL / OMNeT++)

### Option A: Using the `opp_env` WSL Environment
Open your WSL terminal in the `opp_env` distro:
```bash
# 1. Enter the OMNeT++ environment
opp_env shell omnetpp-6.4.0

# 2. Navigate to your ChronosNR source directory and compile
cd /c/Student/CAD/OMNET/ChronosNR/src
opp_makemake -f --deep -O ../out
make -j$(nproc)

# 3. Run interactive GUI (Qtenv)
cd ../simulations
../out/clang-release/src -u Qtenv -n ../src:. omnetpp.ini

# 4. Run CLI batch simulation (Load & Policy Sweep)
../out/clang-release/src -u Cmdenv -c LoadAndPolicySweep -n ../src:. omnetpp.ini

# 5. Generate plots
python3 ../scripts/plot_results.py
```

### Option B: Using Standard WSL (Ubuntu / Kali Linux)
```bash
# 1. Source OMNeT++ environment
source ~/omnetpp-*/setenv

# 2. Compile
cd /mnt/c/Student/CAD/OMNET/ChronosNR/src
opp_makemake -f --deep -O ../out
make -j$(nproc)

# 3. Run GUI simulation
cd ../simulations
../out/gcc-release/src/ChronosNR -u Qtenv -n ../src:. omnetpp.ini

# 4. Run CLI simulations and generate plots
../out/gcc-release/src/ChronosNR -u Cmdenv -c LoadAndPolicySweep -n ../src:. omnetpp.ini
python3 ../scripts/plot_results.py
```

---

## 6. Performance Evaluation & Benchmark Plots

Generated figures are saved directly into `docs/plots/`:

### 1. eMBB Delay vs. Network Load Across Scheduling Policies
![Mean eMBB Delay vs Network Load](docs/plots/plot1_delay_vs_load.png)

### 2. User Priority Differentiation Under Weighted Round Robin (WRR)
![WRR Delay by User Priority](docs/plots/plot2_priority_weights.png)

### 3. Dual-Timescale Latency: eMBB (1.0 ms) vs. URLLC (0.1 ms)
![Dual-Timescale Latency](docs/plots/plot3_embb_vs_urllc.png)

#!/usr/bin/env python3
import glob
import os
import re
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESULTS_DIR = os.path.join(ROOT_DIR, "simulations", "results")
PLOTS_DIR = os.path.join(ROOT_DIR, "docs", "plots")
os.makedirs(PLOTS_DIR, exist_ok=True)

def parse_sca(filepath):
    data = {"itervars": {}, "scalars": {}}
    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if line.startswith("itervar "):
                parts = line.split()
                if len(parts) >= 3:
                    val = parts[2].replace('\\', '').replace('"', '').strip()
                    data["itervars"][parts[1]] = val
            elif line.startswith("scalar "):
                m = re.match(r'^scalar\s+(\S+)\s+(\S+)\s+([0-9.eE+-]+)', line)
                if m:
                    mod, name, val = m.group(1), m.group(2), float(m.group(3))
                    data["scalars"][f"{mod}.{name}"] = val
    return data

iat_to_load = {
    "1.66ms": 30,
    "1.0ms": 50,
    "0.71ms": 70,
    "0.55ms": 90
}

sweep_files = sorted(glob.glob(os.path.join(RESULTS_DIR, "LoadAndPolicySweep-*.sca")))
records = []
for fp in sweep_files:
    d = parse_sca(fp)
    pol = d["itervars"].get("policy")
    iat = d["itervars"].get("iat")
    if pol and iat in iat_to_load:
        load_pct = iat_to_load[iat]
        embb_ms = d["scalars"].get("ChronosNRNetwork.sink.System_eMBB_Mean_Delay_ms", 0.0)
        urllc_ms = d["scalars"].get("ChronosNRNetwork.sink.System_URLLC_Mean_Delay_ms", 0.0)
        u0_ms = d["scalars"].get("ChronosNRNetwork.sink.User_0_Mean_Delay_ms", 0.0)
        u1_ms = d["scalars"].get("ChronosNRNetwork.sink.User_1_Mean_Delay_ms", 0.0)
        u2_ms = d["scalars"].get("ChronosNRNetwork.sink.User_2_Mean_Delay_ms", 0.0)
        u3_ms = d["scalars"].get("ChronosNRNetwork.sink.User_3_Mean_Delay_ms", 0.0)
        u4_ms = d["scalars"].get("ChronosNRNetwork.sink.User_4_Mean_Delay_ms", 0.0)
        records.append({
            "policy": pol,
            "load": load_pct,
            "embb_ms": embb_ms,
            "urllc_ms": urllc_ms,
            "hp_ms": u0_ms,
            "mp_ms": (u1_ms + u2_ms) / 2.0,
            "lp_ms": (u3_ms + u4_ms) / 2.0
        })

if not records:
    print(f"[plot_results.py] No LoadAndPolicySweep-*.sca files found in {RESULTS_DIR}. Run simulations first.")
    exit(0)

# --- PLOT 1: Mean eMBB Delay vs. Network Load across Scheduling Policies ---
plt.figure(figsize=(8, 5))
markers = {"WRR": "o", "PF": "s", "MAX_CI": "^", "RR": "d"}
for pol in ["WRR", "PF", "MAX_CI", "RR"]:
    pts = sorted([r for r in records if r["policy"] == pol], key=lambda x: x["load"])
    if pts:
        plt.plot([p["load"] for p in pts], [p["embb_ms"] for p in pts],
                 marker=markers.get(pol, "o"), linewidth=2, label=f"Policy: {pol}")

plt.title("ChronosNR: 5G eMBB Mean Block Delay vs. Network Load (Under URLLC Puncturing)")
plt.xlabel("Network Load (%)")
plt.ylabel("Mean eMBB End-to-End Delay (ms)")
plt.xticks([30, 50, 70, 90], ["30%", "50%", "70%", "90%"])
plt.grid(True, linestyle="--", alpha=0.6)
plt.legend()
plt.tight_layout()
plt.savefig(os.path.join(PLOTS_DIR, "plot1_delay_vs_load.png"), dpi=160)
plt.close()

# --- PLOT 2: User Priority Class Delay (HP W=6, MP W=2, LP W=1) under WRR ---
wrr_pts = sorted([r for r in records if r["policy"] == "WRR"], key=lambda x: x["load"])
if wrr_pts:
    plt.figure(figsize=(8, 5))
    loads = [p["load"] for p in wrr_pts]
    plt.plot(loads, [p["hp_ms"] for p in wrr_pts], marker="o", linewidth=2.2, label="High Priority (HP: W=6, User 0)")
    plt.plot(loads, [p["mp_ms"] for p in wrr_pts], marker="s", linewidth=2.2, label="Medium Priority (MP: W=2, Users 1-2)")
    plt.plot(loads, [p["lp_ms"] for p in wrr_pts], marker="^", linewidth=2.2, label="Low Priority (LP: W=1, Users 3-4)")
    plt.title("ChronosNR WRR: Mean Delay by User Priority Weight W[i]")
    plt.xlabel("Network Load (%)")
    plt.ylabel("Mean eMBB Delay (ms)")
    plt.xticks([30, 50, 70, 90], ["30%", "50%", "70%", "90%"])
    plt.grid(True, linestyle="--", alpha=0.6)
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(PLOTS_DIR, "plot2_priority_weights.png"), dpi=160)
    plt.close()

# --- PLOT 3: eMBB (1.0 ms cycle) vs. URLLC (0.1 ms minislot cycle) Delay ---
plt.figure(figsize=(8, 5))
pf_pts = sorted([r for r in records if r["policy"] == "PF"], key=lambda x: x["load"])
if pf_pts:
    loads = [p["load"] for p in pf_pts]
    plt.plot(loads, [p["embb_ms"] for p in pf_pts], marker="o", linewidth=2.2, color="#1f77b4", label="eMBB Mean Delay (1.0 ms Slot + Retx)")
    plt.plot(loads, [p["urllc_ms"] for p in pf_pts], marker="s", linewidth=2.2, color="#d62728", label="URLLC Mean Delay (0.1 ms Minislot Puncturing)")
    plt.yscale("log")
    plt.title("ChronosNR Dual-Timescale Latency: eMBB (1.0 ms) vs. URLLC (0.1 ms)")
    plt.xlabel("eMBB Network Load (%)")
    plt.ylabel("Mean End-to-End Delay (ms, log scale)")
    plt.xticks([30, 50, 70, 90], ["30%", "50%", "70%", "90%"])
    plt.grid(True, which="both", linestyle="--", alpha=0.6)
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(PLOTS_DIR, "plot3_embb_vs_urllc.png"), dpi=160)
    plt.close()

print(f"==> Benchmark charts generated in {PLOTS_DIR}")

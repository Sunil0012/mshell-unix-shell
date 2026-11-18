#!/usr/bin/env python3
"""Repeatable, alternating shell process-launch benchmark."""
import argparse
import csv
import os
import shutil
import statistics
import subprocess
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--commands", type=int, default=1000, help="external commands per workload")
    ap.add_argument("--runs", type=int, default=15, help="measured runs per shell")
    ap.add_argument("--csv", default="benchmarks/results.csv", help="CSV output path; use '-' to disable")
    args = ap.parse_args()
    if args.commands < 1 or args.runs < 1: ap.error("--commands and --runs must be positive")
    shells = [("mshell", [os.path.join(ROOT,"mshell")]), ("bash", [shutil.which("bash") or "bash"])]
    results = {name: [] for name, _ in shells}
    with tempfile.NamedTemporaryFile(mode="w", prefix="mshell-bench-", delete=False) as workload:
        workload_path = workload.name
        workload.writelines("/bin/true\n" for _ in range(args.commands))
    try:
        def sample(command):
            start = time.perf_counter()
            p = subprocess.run(command + [workload_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            elapsed = time.perf_counter() - start
            if p.returncode: raise SystemExit(f"{command[0]} returned {p.returncode}")
            return elapsed
        print(f"Workload: {args.commands} external /bin/true launches; {args.runs} runs per shell")
        print("Warm-up: one run per shell; measured order alternates")
        for _, command in shells: sample(command)
        for i in range(args.runs):
            for name, command in (shells if i % 2 == 0 else list(reversed(shells))):
                elapsed=sample(command);results[name].append(elapsed)
                print(f"run {i+1:02d} {name:6s} {elapsed:.4f} sec")
        print("\nSummary (median, IQR, min, max)")
        for name,_ in shells:
            vals=results[name]; med=statistics.median(vals)
            quart=statistics.quantiles(vals,n=4,method="inclusive") if len(vals)>1 else [med]*3
            print(f"{name:6s} median={med:.4f}s IQR={quart[2]-quart[0]:.4f}s min={min(vals):.4f}s max={max(vals):.4f}s")
        paired=[a-b for a,b in zip(results["mshell"],results["bash"])]
        print(f"Paired difference (mshell - bash): median={statistics.median(paired):+.4f}s; positive means mshell took longer")
        if args.csv != "-":
            with open(args.csv,"w",newline="") as f:
                w=csv.writer(f);w.writerow(["pair","shell","commands","elapsed_seconds","paired_delta_mshell_minus_bash"])
                for name,vals in results.items():
                    for i,value in enumerate(vals):w.writerow([i+1,name,args.commands,f"{value:.8f}",f"{paired[i]:.8f}"])
            print(f"Raw measurements: {args.csv}")
    finally: os.unlink(workload_path)

if __name__ == "__main__": main()

#!/usr/bin/env python3
"""Automatisation de la procédure de burn-in pour le terrarium."""
import argparse
import time
import requests

PROFILE_STEPS = [
    {"cct": {"day": 9000, "warm": 2000}, "uva": {"set": 5000, "clamp": 8000}, "uvb": {"set": 4500, "clamp": 6000, "period_s": 60, "duty_pm": 4500}, "sky": 1},
    {"cct": {"day": 3000, "warm": 1000}, "uva": {"set": 0, "clamp": 8000}, "uvb": {"set": 0, "clamp": 6000, "period_s": 90, "duty_pm": 0}, "sky": 0},
]

STATUS_ENDPOINT = "/api/status"
LIGHT_ENDPOINT = "/api/light/dome0"


def push_step(base_url: str, step: dict) -> None:
    resp = requests.post(base_url + LIGHT_ENDPOINT, json=step, timeout=10)
    resp.raise_for_status()


def log_status(base_url: str) -> None:
    resp = requests.get(base_url + STATUS_ENDPOINT, timeout=10)
    resp.raise_for_status()
    data = resp.json()
    print(time.strftime("%Y-%m-%d %H:%M:%S"), data.get("summary"))


def main() -> None:
    parser = argparse.ArgumentParser(description="Cycle automatique burn-in")
    parser.add_argument("--host", default="https://terrarium.local", help="URL de base du contrôleur")
    parser.add_argument("--cycles", type=int, default=24, help="Nombre de cycles lumineux")
    parser.add_argument("--period", type=int, default=900, help="Durée (s) par étape")
    args = parser.parse_args()

    for cycle in range(args.cycles):
        for idx, step in enumerate(PROFILE_STEPS):
            print(f"[cycle {cycle+1}/{args.cycles}] étape {idx+1}/{len(PROFILE_STEPS)}")
            push_step(args.host, step)
            for _ in range(args.period // 30):
                log_status(args.host)
                time.sleep(30)


if __name__ == "__main__":
    main()

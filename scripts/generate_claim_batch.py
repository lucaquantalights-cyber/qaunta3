#!/usr/bin/env python3

import csv
import json
import secrets
import sys
from datetime import datetime, timezone
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[1]
INVENTORY_DIR = PROJECT_DIR / "claim_inventory"
INVENTORY_FILE = INVENTORY_DIR / "claim_inventory.json"
PRINT_CSV = INVENTORY_DIR / "claim_codes_to_print.csv"
DEFAULT_COUNT = 100
DEFAULT_BATCH = datetime.now().strftime("BATCH-%Y%m%d")
ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"


def fail(message):
    print(f"\nERROR: {message}")
    sys.exit(1)


def now_iso():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def generate_code():
    left = "".join(secrets.choice(ALPHABET) for _ in range(4))
    right = "".join(secrets.choice(ALPHABET) for _ in range(4))
    return f"{left}-{right}"


def load_inventory():
    if not INVENTORY_FILE.exists():
        return {"schemaVersion": 1, "updatedAt": now_iso(), "claims": []}
    try:
        return json.loads(INVENTORY_FILE.read_text(encoding="utf-8"))
    except Exception as exc:
        fail(f"Could not read {INVENTORY_FILE}: {exc}")


def save_inventory(inventory):
    INVENTORY_DIR.mkdir(parents=True, exist_ok=True)
    if INVENTORY_FILE.exists():
        INVENTORY_FILE.with_suffix(".json.bak").write_bytes(INVENTORY_FILE.read_bytes())
    inventory["updatedAt"] = now_iso()
    INVENTORY_FILE.write_text(json.dumps(inventory, indent=4) + "\n", encoding="utf-8")


def write_print_csv(claims):
    INVENTORY_DIR.mkdir(parents=True, exist_ok=True)
    fields = ["batch", "sequence", "claimCode", "status", "deviceId", "label", "macAddress", "chipId"]
    with PRINT_CSV.open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for item in claims:
            writer.writerow({field: item.get(field, "") or "" for field in fields})


def main():
    print("========================================")
    print("       Quanta Claim Batch Generator")
    print("========================================")

    count_text = input(f"Number of claim codes [{DEFAULT_COUNT}]: ").strip()
    batch = input(f"Batch name [{DEFAULT_BATCH}]: ").strip() or DEFAULT_BATCH

    try:
        count = int(count_text) if count_text else DEFAULT_COUNT
    except ValueError:
        fail("Number of claim codes must be an integer.")

    if count < 1 or count > 10000:
        fail("Count must be between 1 and 10000.")

    inventory = load_inventory()
    claims = inventory.setdefault("claims", [])
    existing_codes = {str(item.get("claimCode", "")).upper() for item in claims if item.get("claimCode")}
    seqs = [int(item.get("sequence", 0)) for item in claims if str(item.get("batch", "")) == batch]
    sequence = max(seqs, default=0) + 1
    created = []

    while len(created) < count:
        code = generate_code()
        if code in existing_codes:
            continue
        item = {
            "claimCode": code,
            "batch": batch,
            "sequence": sequence,
            "status": "UNUSED",
            "createdAt": now_iso(),
            "assignedAt": None,
            "deviceId": None,
            "label": None,
            "macAddress": None,
            "chipId": None,
            "firebaseStatus": "NOT_ASSIGNED",
        }
        claims.append(item)
        created.append(item)
        existing_codes.add(code)
        sequence += 1

    save_inventory(inventory)
    write_print_csv(created)

    print("\nCLAIM BATCH CREATED")
    print(f"Batch:          {batch}")
    print(f"Codes created:  {len(created)}")
    print(f"Inventory:      {INVENTORY_FILE}")
    print(f"Printing CSV:   {PRINT_CSV}")


if __name__ == "__main__":
    main()

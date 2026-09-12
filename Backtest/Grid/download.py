#!/usr/bin/env python3
"""
Version optimisée pour des centaines de milliers de PETITS fichiers.
- 128 threads (latence-bound, le GIL n'est pas limitant)
- get_object + écriture directe (bien moins d'overhead que download_file)
- skip des fichiers déjà présents (reprise possible)
"""

import os
import sys
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed

import boto3
from botocore.config import Config

BUCKET = "sonarx-hyperliquid-public"
PREFIX = "market_data/hip3/xyz:XYZ100/l2-summary-snapshots/"
PROFILE = "main"
DEST_DIR = sys.argv[1] if len(sys.argv) > 1 else "./l2-summary-snapshots"
MAX_WORKERS = 32
RP = {"RequestPayer": "requester"}

session = boto3.Session(profile_name=PROFILE)
s3 = session.client(
    "s3",
    config=Config(
        max_pool_connections=MAX_WORKERS + 10,
        retries={"max_attempts": 5, "mode": "adaptive"},
    ),
)

downloaded_bytes = 0
done_files = 0
lock = threading.Lock()


def list_all_keys():
    keys = []
    paginator = s3.get_paginator("list_objects_v2")
    for page in paginator.paginate(Bucket=BUCKET, Prefix=PREFIX, **RP):
        for obj in page.get("Contents", []):
            if not obj["Key"].endswith("/"):
                keys.append((obj["Key"], obj["Size"]))
    return keys


def download_one(key: str, size: int):
    global downloaded_bytes, done_files
    rel = key[len(PREFIX):]
    local_path = os.path.join(DEST_DIR, rel)

    # reprise : skip si déjà téléchargé avec la bonne taille
    try:
        if os.path.getsize(local_path) == size:
            with lock:
                done_files += 1
            return
    except OSError:
        pass

    os.makedirs(os.path.dirname(local_path) or ".", exist_ok=True)
    body = s3.get_object(Bucket=BUCKET, Key=key, **RP)["Body"].read()
    with open(local_path, "wb") as f:
        f.write(body)

    with lock:
        downloaded_bytes += len(body)
        done_files += 1


def printer(total_files: int, stop: threading.Event):
    while not stop.wait(2):
        with lock:
            mb = downloaded_bytes / (1024 * 1024)
            n = done_files
        print(f"\r{mb:,.1f} Mo téléchargés  ({n}/{total_files} fichiers)", end="", flush=True)


def main():
    print("Listing des objets...")
    keys = list_all_keys()
    total_mb = sum(s for _, s in keys) / (1024 * 1024)
    print(f"{len(keys)} fichiers ({total_mb:,.1f} Mo)")

    stop = threading.Event()
    threading.Thread(target=printer, args=(len(keys), stop), daemon=True).start()

    errors = []
    with ThreadPoolExecutor(max_workers=MAX_WORKERS) as pool:
        futs = {pool.submit(download_one, k, s): k for k, s in keys}
        for fut in as_completed(futs):
            try:
                fut.result()
            except Exception as e:
                errors.append((futs[fut], e))

    stop.set()
    with lock:
        mb = downloaded_bytes / (1024 * 1024)
    print(f"\rTerminé : {mb:,.1f} Mo dans {DEST_DIR}")
    if errors:
        print(f"{len(errors)} erreurs, ex: {errors[0]}")


if __name__ == "__main__":
    main()

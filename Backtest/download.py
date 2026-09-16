#!/usr/bin/env python3
"""
Téléchargement S3 optimisé pour des centaines de milliers de PETITS fichiers.

- 128 threads (latence-bound, le GIL n'est pas limitant)
- get_object + écriture directe (bien moins d'overhead que download_file)
- skip des fichiers déjà présents (reprise possible)
- retry avec backoff sur erreurs réseau/SSL, + passes de rattrapage finales
  SANS re-lister le bucket
- cache du listing sur disque (le listing de 239k objets est lent)

Usage :
    python download_s3_retry.py [dossier_destination]
    python download_s3_retry.py --refresh-list   # force un nouveau listing
"""

import json
import os
import random
import socket
import ssl
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

import boto3
from botocore.config import Config
from botocore.exceptions import (
    ClientError,
    ConnectionClosedError,
    EndpointConnectionError,
    ReadTimeoutError,
)

try:
    from botocore.exceptions import SSLError as BotoSSLError
except ImportError:
    BotoSSLError = ssl.SSLError

try:
    from urllib3.exceptions import ProtocolError, HTTPError as Urllib3Error
except ImportError:
    ProtocolError = Urllib3Error = ()

# ---------------- Configuration ----------------
BUCKET = "sonarx-hyperliquid-public"
PREFIX = "market_data/hip3/xyz:XYZ100/l2-summary-snapshots/"
PROFILE = "main"
DEST_DIR = "./l2-summary-snapshots"
MAX_WORKERS = 128
ATTEMPTS_PER_FILE = 4      # tentatives immédiates dans le thread
FINAL_PASSES = 3           # passes de rattrapage sur ce qui reste en échec
LIST_CACHE = ".s3_listing_cache.json"
RP = {"RequestPayer": "requester"}
# ------------------------------------------------

args = [a for a in sys.argv[1:] if not a.startswith("--")]
REFRESH_LIST = "--refresh-list" in sys.argv
if args:
    DEST_DIR = args[0]

# Erreurs transitoires -> on retente
RETRIABLE = (
    BotoSSLError,
    ssl.SSLError,
    ConnectionClosedError,
    EndpointConnectionError,
    ReadTimeoutError,
    ConnectionError,
    socket.timeout,
    ProtocolError,
    Urllib3Error,
)

session = boto3.Session(profile_name=PROFILE)
s3 = session.client(
    "s3",
    config=Config(
        max_pool_connections=MAX_WORKERS + 10,
        retries={"max_attempts": 5, "mode": "adaptive"},
        connect_timeout=15,
        read_timeout=60,
    ),
)

downloaded_bytes = 0
done_files = 0
retried = 0
lock = threading.Lock()


def is_retriable(exc: Exception) -> bool:
    if isinstance(exc, RETRIABLE):
        return True
    # 500/502/503/504 et throttling
    if isinstance(exc, ClientError):
        code = exc.response.get("ResponseMetadata", {}).get("HTTPStatusCode", 0)
        return code in (429, 500, 502, 503, 504)
    return False


def list_all_keys():
    """Liste les objets, avec cache sur disque (le listing est très lent)."""
    if os.path.exists(LIST_CACHE) and not REFRESH_LIST:
        with open(LIST_CACHE) as f:
            keys = json.load(f)
        print(f"Listing chargé depuis le cache ({LIST_CACHE}) — "
              f"utilise --refresh-list pour le régénérer")
        return [tuple(k) for k in keys]

    print("Listing des objets (peut prendre plusieurs minutes)...")
    keys = []
    paginator = s3.get_paginator("list_objects_v2")
    for page in paginator.paginate(Bucket=BUCKET, Prefix=PREFIX, **RP):
        for obj in page.get("Contents", []):
            if not obj["Key"].endswith("/"):
                keys.append((obj["Key"], obj["Size"]))
        print(f"\r{len(keys):,} objets listés", end="", flush=True)
    print()

    tmp = LIST_CACHE + ".tmp"
    with open(tmp, "w") as f:
        json.dump(keys, f)
    os.replace(tmp, LIST_CACHE)
    return keys


def download_one(key: str, size: int):
    """Télécharge un objet, avec retry + backoff sur erreurs transitoires."""
    global downloaded_bytes, done_files, retried

    rel = key[len(PREFIX):]
    local_path = os.path.join(DEST_DIR, rel)

    # reprise : skip si déjà présent avec la bonne taille
    try:
        if os.path.getsize(local_path) == size:
            with lock:
                done_files += 1
            return
    except OSError:
        pass

    last_exc = None
    for attempt in range(ATTEMPTS_PER_FILE):
        try:
            body = s3.get_object(Bucket=BUCKET, Key=key, **RP)["Body"].read()
            break
        except Exception as e:
            last_exc = e
            if not is_retriable(e) or attempt == ATTEMPTS_PER_FILE - 1:
                raise
            with lock:
                retried += 1
            # backoff exponentiel + jitter pour ne pas resynchroniser les threads
            time.sleep(min(0.3 * 2 ** attempt, 5) * (0.5 + random.random()))
    else:
        raise last_exc

    os.makedirs(os.path.dirname(local_path) or ".", exist_ok=True)
    tmp = local_path + ".part"
    with open(tmp, "wb") as f:
        f.write(body)
    os.replace(tmp, local_path)  # écriture atomique : jamais de fichier tronqué

    with lock:
        downloaded_bytes += len(body)
        done_files += 1


def run_pass(items, total_files, label=""):
    """Lance un pool sur `items`, renvoie la liste des (key, size, exc) en échec."""
    failed = []
    with ThreadPoolExecutor(max_workers=min(MAX_WORKERS, max(len(items), 1))) as pool:
        futs = {pool.submit(download_one, k, s): (k, s) for k, s in items}
        for fut in as_completed(futs):
            k, s = futs[fut]
            try:
                fut.result()
            except Exception as e:
                failed.append((k, s, e))
    return failed


def printer(total_files: int, stop: threading.Event):
    while not stop.wait(2):
        with lock:
            mb = downloaded_bytes / (1024 * 1024)
            n, r = done_files, retried
        suffix = f"  [{r} retries]" if r else ""
        print(f"\r{mb:,.1f} Mo  ({n:,}/{total_files:,} fichiers){suffix}",
              end="", flush=True)


def main():
    keys = list_all_keys()
    total_mb = sum(s for _, s in keys) / (1024 * 1024)
    print(f"{len(keys):,} fichiers ({total_mb:,.1f} Mo)")

    stop = threading.Event()
    threading.Thread(target=printer, args=(len(keys), stop), daemon=True).start()

    failed = run_pass(keys, len(keys))

    # Passes de rattrapage : on réutilise la liste en mémoire, pas de re-listing.
    # Workers réduits à chaque passe : moins de concurrence = moins de coupures SSL.
    global MAX_WORKERS
    for i in range(1, FINAL_PASSES + 1):
        if not failed:
            break
        stop.set()
        print(f"\r{len(failed)} échecs — passe de rattrapage {i}/{FINAL_PASSES}"
              f" ({' ' * 20})")
        time.sleep(2)
        MAX_WORKERS = max(8, MAX_WORKERS // 4)
        stop = threading.Event()
        threading.Thread(target=printer, args=(len(keys), stop), daemon=True).start()
        failed = run_pass([(k, s) for k, s, _ in failed], len(keys))

    stop.set()
    time.sleep(0.1)
    with lock:
        mb = downloaded_bytes / (1024 * 1024)
    print(f"\rTerminé : {mb:,.1f} Mo dans {DEST_DIR} "
          f"({done_files:,}/{len(keys):,} fichiers, {retried} retries){' ' * 20}")

    if failed:
        print(f"\n{len(failed)} fichiers toujours en échec après "
              f"{FINAL_PASSES} passes :")
        for k, _, e in failed[:5]:
            print(f"  - {k}: {type(e).__name__}: {e}")
        with open("echecs.txt", "w") as f:
            f.write("\n".join(k for k, _, _ in failed))
        print("Liste complète dans echecs.txt")
        sys.exit(1)


if __name__ == "__main__":
    main()

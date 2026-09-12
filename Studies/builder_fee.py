import requests
import lz4.frame
import pandas as pd
from pathlib import Path
from datetime import datetime, timedelta


# =========================
# CONFIG
# =========================

START_DATE = "20260902"
END_DATE = "20260908"

BUILDER = "0x42f3226007290b02c5a0b15bccbb1ba6df04f992"

DATA_DIR = Path("builder_data")
DATA_DIR.mkdir(exist_ok=True)


# =========================
# DOWNLOAD + EXTRACT
# =========================

def download_day(date):
    url = (
        f"https://stats-data.hyperliquid.xyz/Mainnet/"
        f"builder_fills/{BUILDER.lower()}/{date}.csv.lz4"
    )

    lz4_file = DATA_DIR / f"{date}.csv.lz4"
    csv_file = DATA_DIR / f"{date}.csv"

    if csv_file.exists():
        print(f"{date} : already downloaded")
        return csv_file

    print(f"{date} : downloading...")

    r = requests.get(
        url,
        headers={
            "User-Agent": "Mozilla/5.0",
            "Accept": "*/*",
        },
    )

    if r.status_code != 200:
        print(f"{date} : HTTP {r.status_code}")
        return None

    lz4_file.write_bytes(r.content)

    print(f"{date} : extracting...")

    data = lz4.frame.decompress(r.content)
    csv_file.write_bytes(data)

    lz4_file.unlink()

    print(f"{date} : OK")

    return csv_file


# =========================
# DOWNLOAD ALL DAYS
# =========================

start = datetime.strptime(START_DATE, "%Y%m%d")
end = datetime.strptime(END_DATE, "%Y%m%d")

files = []

current = start

while current <= end:
    date = current.strftime("%Y%m%d")

    file = download_day(date)

    if file is not None:
        files.append(file)

    current += timedelta(days=1)


# =========================
# LOAD
# =========================

print("\nLoading data...")

df = pd.concat(
    [pd.read_csv(file) for file in files],
    ignore_index=True
)

print(f"Rows loaded: {len(df):,}")


# =========================
# BASIC DATA
# =========================

df["volume"] = df["px"] * df["sz"]

df["fee_bps"] = (
    df["builder_fee"] / df["volume"] * 100_000
)


# =========================
# GLOBAL
# =========================

volume = df["volume"].sum()
fees = df["builder_fee"].sum()

print("\n" + "=" * 60)
print("GLOBAL")
print("=" * 60)

print(f"Period              : {START_DATE} -> {END_DATE}")
print(f"Trades              : {len(df):,}")
print(f"Users distincts     : {df['user'].nunique():,}")
print(f"Volume              : ${volume:,.2f}")
print(f"Builder fees        : ${fees:,.2f}")


# =========================
# USERS
# =========================

users = (
    df.groupby("user")
    .agg(
        volume=("volume", "sum"),
        fees=("builder_fee", "sum"),
        trades=("user", "size"),
    )
)


# =========================
# ZERO FEE
# =========================

zero_fee_users = users["fees"] == 0
paid_users = ~zero_fee_users

zero_volume = users.loc[zero_fee_users, "volume"].sum()

print("\n--- ZERO FEE ---")

print(f"Users 0 fee         : {zero_fee_users.sum():,}")
print(f"Volume 0 fee        : ${zero_volume:,.2f}")
print(f"Share volume 0 fee  : {zero_volume / volume:.2%}")


# =========================
# FEE RATE PAID USERS
# =========================

paid_volume = users.loc[paid_users, "volume"].sum()
paid_fees = users.loc[paid_users, "fees"].sum()

fee_rate_paid = paid_fees / paid_volume

print("\n--- FEE RATE ---")

print(f"Paid users          : {paid_users.sum():,}")
print(f"Paid volume         : ${paid_volume:,.2f}")
print(f"Paid fees           : ${paid_fees:,.2f}")
print(f"Fee / volume        : {fee_rate_paid:.6%}")
print(f"Fee / volume (bps)  : {fee_rate_paid * 100_000:.2f} bps")


# =========================
# ZERO FEE USERS
# + TOKEN PREFERENCE
# =========================

zero_fee_df = users[zero_fee_users].copy()

token_pref = (
    df[df["user"].isin(zero_fee_df.index)]
    .groupby(["user", "coin"])["volume"]
    .sum()
    .reset_index()
    .sort_values(
        ["user", "volume"],
        ascending=[True, False]
    )
    .drop_duplicates("user")
    .set_index("user")
    .rename(
        columns={
            "coin": "preferred_coin",
            "volume": "preferred_coin_volume",
        }
    )
)

zero_fee_df = zero_fee_df.join(token_pref)

print("\n--- ZERO FEE USERS ---")

print(
    zero_fee_df
    .sort_values("volume", ascending=False)
    .to_string()
)


# =========================
# TOP USERS
# =========================

print("\n--- TOP 20 USERS ---")

print(
    users
    .sort_values("volume", ascending=False)
    .head(20)
    .to_string()
)


# =========================
# FEE BPS DISTRIBUTION
# =========================

df["fee_bps_rounded"] = (
    df["fee_bps"]
    .round()
    .astype(int)
)

bps_stats = (
    df
    .groupby("fee_bps_rounded")
    .agg(
        trades=("user", "size"),
        volume=("volume", "sum"),
        fees=("builder_fee", "sum"),
    )
    .sort_values("trades", ascending=False)
)

# Shares sur TOUS les trades / TOUT le volume
bps_stats["trade_share"] = (
    bps_stats["trades"] / len(df)
)

bps_stats["volume_share"] = (
    bps_stats["volume"] / volume
)

print("\n--- FEE BPS UTILISÉS ---")

print(
    bps_stats
    .head(30)
    .to_string(
        formatters={
            "trade_share": "{:.2%}".format,
            "volume_share": "{:.2%}".format,
        }
    )
)

print("\n--- CHECK ---")
print(f"Trade share total  : {bps_stats['trade_share'].sum():.2%}")
print(f"Volume share total : {bps_stats['volume_share'].sum():.2%}")


# =========================
# SAVE RESULTS
# =========================

users.to_csv(DATA_DIR / "users.csv")
zero_fee_df.to_csv(DATA_DIR / "zero_fee_users.csv")
bps_stats.to_csv(DATA_DIR / "fee_bps.csv")

print("\nResults saved:")
print(DATA_DIR / "users.csv")
print(DATA_DIR / "zero_fee_users.csv")
print(DATA_DIR / "fee_bps.csv")
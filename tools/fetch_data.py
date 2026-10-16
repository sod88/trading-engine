#!/usr/bin/env python3
"""Download real daily price history from Yahoo Finance into CSV files.

Writes one file per ticker in the same layout as a manual Yahoo Finance
download (Date,Open,High,Low,Close,Adj Close,Volume), which the engine loads
directly. The engine uses Adj Close to remove dividend and split jumps.
Standard library only.

    python3 tools/fetch_data.py SPY QQQ TLT --start 2010-01-01
    ./build/backtest data/SPY.csv data/QQQ.csv data/TLT.csv

Yahoo's endpoint is unofficial and free: fine for research, but it can
change or rate-limit without notice.
"""

import argparse
import csv
import datetime as dt
import json
import pathlib
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

URL = "https://query1.finance.yahoo.com/v8/finance/chart/{ticker}"


def fetch_chart(ticker: str, start: dt.date, end: dt.date) -> dict:
    to_epoch = lambda d: int(dt.datetime(d.year, d.month, d.day, tzinfo=dt.timezone.utc).timestamp())
    query = urllib.parse.urlencode({
        "period1": to_epoch(start),
        "period2": to_epoch(end + dt.timedelta(days=1)),
        "interval": "1d",
        "events": "div,split",
        "includeAdjustedClose": "true",
    })
    req = urllib.request.Request(
        URL.format(ticker=urllib.parse.quote(ticker)) + "?" + query,
        headers={"User-Agent": "Mozilla/5.0 (quant-engine fetch_data.py)"},
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.load(resp)


def rows_from_chart(payload: dict) -> list:
    """Turns Yahoo's chart JSON into [date, open, high, low, close, adj_close, volume] rows."""
    chart = payload.get("chart", {})
    if chart.get("error"):
        raise ValueError(chart["error"].get("description") or str(chart["error"]))
    results = chart.get("result") or []
    if not results:
        raise ValueError("no data returned")
    res = results[0]
    stamps = res.get("timestamp") or []
    quote = res["indicators"]["quote"][0]
    adj = (res["indicators"].get("adjclose") or [{}])[0].get("adjclose") or quote["close"]
    # Timestamps are the session open in UTC; shift to exchange time for the date.
    offset = dt.timedelta(seconds=res.get("meta", {}).get("gmtoffset", 0))

    rows = []
    for i, ts in enumerate(stamps):
        values = [quote["open"][i], quote["high"][i], quote["low"][i], quote["close"][i], adj[i]]
        if any(v is None for v in values):
            continue  # holidays / halts show up as nulls
        day = (dt.datetime.fromtimestamp(ts, dt.timezone.utc) + offset).date().isoformat()
        volume = quote["volume"][i] or 0
        rows.append([day] + [round(v, 6) for v in values] + [int(volume)])
    # Yahoo occasionally repeats the last bar; keep the latest one per date.
    by_date = {r[0]: r for r in rows}
    return [by_date[d] for d in sorted(by_date)]


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("tickers", nargs="+", help="e.g. SPY QQQ AAPL BRK-B")
    p.add_argument("--start", default="2010-01-01", help="first date, YYYY-MM-DD")
    p.add_argument("--end", default=dt.date.today().isoformat(), help="last date, YYYY-MM-DD")
    p.add_argument("--out", default="data", help="output directory")
    args = p.parse_args()

    start, end = dt.date.fromisoformat(args.start), dt.date.fromisoformat(args.end)
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    failed = 0
    for n, ticker in enumerate(args.tickers):
        if n:
            time.sleep(1)  # be polite to the endpoint
        try:
            rows = rows_from_chart(fetch_chart(ticker, start, end))
        except (urllib.error.URLError, ValueError, KeyError, IndexError) as e:
            print(f"{ticker}: failed ({e})", file=sys.stderr)
            failed += 1
            continue
        if not rows:
            print(f"{ticker}: no rows in that date range", file=sys.stderr)
            failed += 1
            continue
        path = out / f"{ticker.upper()}.csv"
        with path.open("w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["Date", "Open", "High", "Low", "Close", "Adj Close", "Volume"])
            w.writerows(rows)
        print(f"{ticker}: {len(rows)} days ({rows[0][0]} .. {rows[-1][0]}) -> {path}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

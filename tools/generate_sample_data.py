#!/usr/bin/env python3
"""Generate synthetic daily OHLCV data for testing the engine.

Prices follow a geometric Brownian motion whose drift switches between bull,
bear and sideways regimes, so trend-following strategies have something to
react to. The data is fake: use it to check the plumbing, not to judge a
strategy. Standard library only.

    python3 tools/generate_sample_data.py --days 2000 --seed 42 --out data/sample.csv
"""

import argparse
import datetime as dt
import math
import random


def business_days(start: dt.date, n: int):
    d = start
    while n > 0:
        if d.weekday() < 5:
            yield d
            n -= 1
        d += dt.timedelta(days=1)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--days", type=int, default=2000)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--start", default="2018-01-02")
    p.add_argument("--price", type=float, default=100.0)
    p.add_argument("--out", default="data/sample.csv")
    args = p.parse_args()

    rng = random.Random(args.seed)
    regimes = [(0.25, 0.15), (-0.20, 0.25), (0.02, 0.12)]  # (annual drift, annual vol)
    regime = regimes[0]
    days_left = 0
    price = args.price
    dt_year = 1 / 252

    with open(args.out, "w", newline="") as f:
        f.write("date,open,high,low,close,volume\n")
        for day in business_days(dt.date.fromisoformat(args.start), args.days):
            if days_left <= 0:
                regime = rng.choice(regimes)
                days_left = rng.randint(60, 250)
            days_left -= 1
            mu, sigma = regime

            # Overnight gap, then the intraday move.
            gap = rng.gauss(0, sigma * 0.3 * math.sqrt(dt_year))
            open_ = price * math.exp(gap)
            ret = (mu - 0.5 * sigma**2) * dt_year + sigma * math.sqrt(dt_year) * rng.gauss(0, 1)
            close = open_ * math.exp(ret)
            wiggle = sigma * math.sqrt(dt_year)
            high = max(open_, close) * (1 + abs(rng.gauss(0, wiggle * 0.5)))
            low = min(open_, close) * (1 - abs(rng.gauss(0, wiggle * 0.5)))
            volume = int(1_000_000 * math.exp(rng.gauss(0, 0.3)))

            f.write(f"{day.isoformat()},{open_:.2f},{high:.2f},{low:.2f},{close:.2f},{volume}\n")
            price = close

    print(f"wrote {args.days} bars to {args.out}")


if __name__ == "__main__":
    main()

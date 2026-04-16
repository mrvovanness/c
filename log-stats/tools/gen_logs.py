#!/usr/bin/env python3
"""
Synthetic generator of "combined" format web-server access logs
for testing hw-10 log-stats. Deterministic (fixed seed).

Usage:
    python3 gen_logs.py <output_dir> <num_files> <lines_per_file>

Prints the expected aggregated statistics (total bytes, top-10 URLs by
traffic, top-10 referers by request count) to stdout so they can be
compared against the C program's output.
"""
import os
import random
import sys
from collections import Counter, defaultdict

SEED = 42

URLS = (
    [f"/page/{i}" for i in range(200)]
    + [f"/static/asset-{i}.js" for i in range(50)]
    + ["/", "/api/v1/users", "/api/v1/orders", "/login", "/logout"]
)

REFERERS = [
    "https://google.com/",
    "https://yandex.ru/",
    "https://duckduckgo.com/",
    "https://example.com/home",
    "https://example.com/products",
    "-",
] + [f"https://example.com/ref/{i}" for i in range(40)]

METHODS = ["GET", "GET", "GET", "GET", "POST", "HEAD"]
STATUSES = [200, 200, 200, 200, 304, 404, 500]
UAS = ['"Mozilla/5.0"', '"curl/7.81.0"', '"Googlebot/2.1"']


def zipf_weights(n, skew=1.2):
    """Weights ~ 1/(i+1)^skew — produces a heavy-tailed distribution."""
    return [1.0 / (i + 1) ** skew for i in range(n)]


def main():
    if len(sys.argv) != 4:
        print(
            f"Usage: {sys.argv[0]} <dir> <num_files> <lines_per_file>",
            file=sys.stderr,
        )
        sys.exit(1)

    out_dir = sys.argv[1]
    num_files = int(sys.argv[2])
    lines_per_file = int(sys.argv[3])

    os.makedirs(out_dir, exist_ok=True)
    rng = random.Random(SEED)

    url_w = zipf_weights(len(URLS))
    ref_w = zipf_weights(len(REFERERS))

    total_bytes = 0
    url_bytes = defaultdict(int)
    ref_count = Counter()

    for f in range(num_files):
        path = os.path.join(out_dir, f"access-{f:03d}.log")
        with open(path, "w") as fp:
            for _ in range(lines_per_file):
                url = rng.choices(URLS, weights=url_w, k=1)[0]
                ref = rng.choices(REFERERS, weights=ref_w, k=1)[0]
                method = rng.choice(METHODS)
                status = rng.choice(STATUSES)
                ua = rng.choice(UAS)
                size = 0 if status == 304 else rng.randint(100, 50000)
                size_field = "-" if size == 0 else str(size)
                ip = (
                    f"{rng.randint(1, 255)}."
                    f"{rng.randint(0, 255)}."
                    f"{rng.randint(0, 255)}."
                    f"{rng.randint(1, 255)}"
                )
                ts = (
                    f"[16/Apr/2026:"
                    f"{rng.randint(0, 23):02d}:"
                    f"{rng.randint(0, 59):02d}:"
                    f"{rng.randint(0, 59):02d} +0000]"
                )
                line = (
                    f'{ip} - - {ts} "{method} {url} HTTP/1.1" '
                    f"{status} {size_field} "
                    f'"{ref}" {ua}\n'
                )
                fp.write(line)

                total_bytes += size
                url_bytes[url] += size
                ref_count[ref] += 1

    print(f"Generated {num_files} file(s) in {out_dir}")
    print(f"Total lines: {num_files * lines_per_file}")
    print()
    print(f"Total bytes: {total_bytes}")
    print()
    print("Top 10 URLs by traffic:")
    for i, (u, b) in enumerate(
        sorted(url_bytes.items(), key=lambda x: -x[1])[:10], 1
    ):
        print(f"  {i:2d}. {b:10d}  {u}")
    print()
    print("Top 10 Referers:")
    for i, (r, c) in enumerate(ref_count.most_common(10), 1):
        print(f"  {i:2d}. {c:10d}  {r}")


if __name__ == "__main__":
    main()

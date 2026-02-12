#!/bin/sh
set -e

cd "$(dirname "$0")"

./solution cp1251.txt cp1251 out_cp1251.txt
./solution koi8.txt koi8-r out_koi8r.txt
./solution iso-8859-5.txt iso-8859-5 out_iso88595.txt

diff out_cp1251.txt out_koi8r.txt && echo "cp1251 vs koi8-r: MATCH"
diff out_cp1251.txt out_iso88595.txt && echo "cp1251 vs iso-8859-5: MATCH"

echo ""
echo "Output preview:"
head -c 200 out_cp1251.txt
echo ""

rm -f out_cp1251.txt out_koi8r.txt out_iso88595.txt

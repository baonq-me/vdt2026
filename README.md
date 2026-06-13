# vdt2026

## Guide

Compile

```
gcc -O2 -Wall -Wextra -o matmul matmul.c
```

Run 

```
chmod +x matmul
./matmul -s -o ijk 2048 2048 2048
```

Benchmark

```
echo "=== cache effect at 1024^3 (all 6 orders) ===" && for o in ikj kij ijk jik kji jki; do printf "order=%-4s  " "$o"; /usr/bin/env bash -c "TIMEFORMAT='%3R s'; time ./matmul -s -o $o 1024 1024 1024"; done
```

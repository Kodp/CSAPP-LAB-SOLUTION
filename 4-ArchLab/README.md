### partC
```
cd sim/
make clean && make VERSION=full
cd pipe/
./correctness.pl -p -f ncopy_cadd_shl_swtable.ys && ./benchmark.pl -f ncopy_cadd_shl_swtable.ys
```
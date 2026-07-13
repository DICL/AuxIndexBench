# plot_e11_hash_sigma.gp — key metric: hash-index latency vs key-bit bias
# Biased key bits (sigma) skew bucket occupancy → longer chains.
# NOTE: the CSV does not record sigma, so rows are mapped to the sweep
# list IN FILE ORDER. Pass the exact list you ran:
#   gnuplot -e "infile='e11_hash_sigma.csv'; sigmas='0.2 0.4 0.6 0.8 1.0 1.5 2.0 3.0'" plot_e11_hash_sigma.gp
if (!exists("infile"))  infile  = "e11_hash_sigma.csv"
if (!exists("outfile")) outfile = "e11_hash_sigma.png"
if (!exists("sigmas"))  sigmas  = "0.2 0.4 0.6 0.8 1.0 1.5 2.0 3.0"
clean = "/tmp/aib_e11_clean.csv"
data  = "/tmp/aib_e11.dat"
system(sprintf("perl -0777 -pe 's/\\r//g; s/\\n(?!(?:index|btree|hash|fastfair|wbtree|utree|fptree|lbtree|dptree|bztree),)//g; s/\"([^\"]*)\"/($t=$1)=~s!,!;!gr/ge' \"%s\" > \"%s\"", infile, clean))
awkprog = sprintf("awk -F, -v SIG='%s' '\
NR==1 { for (i=1;i<=NF;i++) h[$i]=i; ns=split(SIG, sig, \" \"); next } \
$(h[\"index\"])==\"hash\" { r++; if (r<=ns) \
  printf \"%%s %%g %%g\\n\", sig[r], $(h[\"lookup_mean_ns\"]), $(h[\"lookup_p99_ns\"]); }'", sigmas)
system(sprintf("%s \"%s\" > \"%s\"", awkprog, clean, data))
set terminal pngcairo enhanced color font "Helvetica,12" size 900,540
set output outfile
set title "Hash index: key-bit bias degrades bucket balance"
set xlabel "Key-bit bias sigma"
set ylabel "Lookup latency (ns)"
set grid xtics ytics
set key top left
plot data using 1:2 with linespoints lw 2 pt 7 lc rgb "#ff7f0e" title "hash mean", \
     data using 1:3 with linespoints lw 1.2 dt 2 pt 6 lc rgb "#ff7f0e" title "hash p99"

# plot_e1_lookup.gp — poster figure 1
#
# Left  : index-only lookup latency (polluter time EXCLUDED) vs pollution.
#         Log-x/log-y; near-straight lines on log-x = logarithmic growth
#         (upper tree levels evicted one at a time, not linearly).
# Right : convergence — (slowest index) / (fastest index) per pollution
#         level. Falling curve = index choice matters less and less as
#         realistic post-lookup work grows.
#
# Usage:
#   gnuplot -e "infile='e1_polluter.csv'" plot_e1_lookup.gp
if (!exists("infile"))  infile  = "e1_polluter.csv"
if (!exists("outfile")) outfile = "e1_lookup.png"
clean = "/tmp/aib_e1l_clean.csv"
data  = "/tmp/aib_e1l.dat"

system(sprintf("perl -0777 -pe 's/\\r//g; s/\\n(?!(?:index|btree|hash|fastfair|wbtree|utree|fptree|lbtree|dptree|bztree),)//g; s/\"([^\"]*)\"/($t=$1)=~s!,!;!gr/ge' \"%s\" > \"%s\"", infile, clean))

awkprog = "awk -F, '\
NR==1 { for (i=1;i<=NF;i++) h[$i]=i; next } \
{ \
  idx=$(h[\"index\"]); \
  b=$(h[\"bytes_per_call\"])+0; \
  if ($(h[\"workload\"])==\"none\") b=0; \
  key=idx SUBSEP b; \
  sum[key]+=$(h[\"lookup_mean_ns\"]); cnt[key]++; bytes[b]=1; \
} \
END { \
  ni=split(\"btree fastfair wbtree utree fptree\", idxs, \" \"); \
  n=0; for (b in bytes) arr[++n]=b; \
  for (i=1;i<=n;i++) for (j=i+1;j<=n;j++) \
    if (arr[i]+0>arr[j]+0) { t=arr[i]; arr[i]=arr[j]; arr[j]=t; } \
  for (i=1;i<=n;i++) { \
    b=arr[i]+0; x=(b==0 ? 0.5 : b/1024.0); \
    printf \"%g\", x; \
    mn=1e30; mx=-1; \
    for (k=1;k<=ni;k++) { \
      key=idxs[k] SUBSEP b; \
      if (cnt[key]>0) { v=sum[key]/cnt[key]; printf \" %g\", v; \
                        if (v<mn) mn=v; if (v>mx) mx=v; } \
      else printf \" NaN\"; \
    } \
    if (mx>0) printf \" %g\", mx/mn; else printf \" NaN\"; \
    printf \"\\n\"; \
  } \
}'"
system(sprintf("%s \"%s\" > \"%s\"", awkprog, clean, data))

set terminal pngcairo enhanced color font "Helvetica,12" size 1200,520
set output outfile
set multiplot layout 1,2

set logscale x 2
set xrange [0.4:20000]
set xtics ("none" 0.5, "4K" 4, "32K" 32, "256K" 256, "2M" 2048, "16M" 16384)
set xlabel "Post-lookup working set per op"
set grid xtics ytics
set datafile missing "NaN"

set title "index lookup latency (polluter time excluded)"
set ylabel "Mean lookup latency (ns)"
set logscale y
set key top left
plot data using 1:2 with linespoints lw 2 pt 7  lc rgb "#1f77b4" title "btree", \
     data using 1:3 with linespoints lw 2 pt 5  lc rgb "#2ca02c" title "fastfair", \
     data using 1:4 with linespoints lw 2 pt 9  lc rgb "#d62728" title "wbtree", \
     data using 1:5 with linespoints lw 2 pt 11 lc rgb "#9467bd" title "utree", \
     data using 1:6 with linespoints lw 2 pt 13 lc rgb "#8c564b" title "fptree"

set title "convergence: slowest / fastest index"
set ylabel "Latency ratio (slowest / fastest)"
unset logscale y
set yrange [1:*]
set key top right
plot data using 1:7 with linespoints lw 2.5 pt 7 lc rgb "#333333" \
     title "index spread"

unset multiplot

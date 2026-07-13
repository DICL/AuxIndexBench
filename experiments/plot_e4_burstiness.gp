# plot_e4_burstiness.gp — poster figure 5
#
# "Same mean rate, very different tails."
# Inter-arrival CV² swept at a FIXED mean rate (cv²=1 is Poisson).
# Queue mean (solid) and queue p99.99 (dashed) both inflate with
# burstiness even though the average load never changes — a benchmark
# that only sets a rate, without a burstiness knob, understates tails.
#
# Usage:
#   gnuplot -e "infile='e4_burstiness.csv'" plot_e4_burstiness.gp
if (!exists("infile"))  infile  = "e4_burstiness.csv"
if (!exists("outfile")) outfile = "e4_burstiness.png"
clean = "/tmp/aib_e4_clean.csv"
data  = "/tmp/aib_e4.dat"

system(sprintf("perl -0777 -pe 's/\\r//g; s/\\n(?!(?:index|btree|hash|fastfair|wbtree|utree|fptree|lbtree|dptree|bztree),)//g; s/\"([^\"]*)\"/($t=$1)=~s!,!;!gr/ge' \"%s\" > \"%s\"", infile, clean))

# Emits: cv2  <queue_mean per index...>  <queue_p9999 per index...>
awkprog = "awk -F, '\
NR==1 { for (i=1;i<=NF;i++) h[$i]=i; next } \
{ \
  idx=$(h[\"index\"]); v=$(h[\"cv2\"])+0; \
  key=idx SUBSEP v; \
  q[key]+=$(h[\"queue_mean_ns\"]); t[key]+=$(h[\"queue_p9999_ns\"]); c[key]++; \
  vs[v]=1; \
} \
END { \
  ni=split(\"btree fastfair wbtree utree fptree\", idxs, \" \"); \
  n=0; for (v in vs) arr[++n]=v+0; \
  for (i=1;i<=n;i++) for (j=i+1;j<=n;j++) \
    if (arr[i]>arr[j]) { x=arr[i]; arr[i]=arr[j]; arr[j]=x; } \
  for (i=1;i<=n;i++) { \
    v=arr[i]; printf \"%g\", v; \
    for (k=1;k<=ni;k++) { key=idxs[k] SUBSEP v; \
      if (c[key]>0) printf \" %g\", q[key]/c[key]/1000.0; else printf \" NaN\"; } \
    for (k=1;k<=ni;k++) { key=idxs[k] SUBSEP v; \
      if (c[key]>0) printf \" %g\", t[key]/c[key]/1000.0; else printf \" NaN\"; } \
    printf \"\\n\"; \
  } \
}'"
system(sprintf("%s \"%s\" > \"%s\"", awkprog, clean, data))

set terminal pngcairo enhanced color font "Helvetica,12" size 900,540
set output outfile
set title "Burstiness at a constant mean rate\n{/*0.8 queue mean (solid) and queue p99.99 (dashed) vs inter-arrival CV^2; CV^2=1 is Poisson}"
set logscale x
set logscale y
set xlabel "Inter-arrival CV^2 (same mean rate)"
set ylabel "Queueing delay ({/Symbol m}s)"
set grid xtics ytics
set datafile missing "NaN"
set key top left

set arrow from 1, graph 0 to 1, graph 1 nohead lw 0.8 dt 3 lc rgb "#999999"
set label "Poisson" at 1.05, graph 0.95 font ",10" tc rgb "#666666"

plot data using 1:2 with linespoints lw 2 pt 7    lc rgb "#1f77b4" title "btree mean", \
     data using 1:7 with linespoints lw 1.2 dt 2 pt 6 lc rgb "#1f77b4" title "btree p99.99", \
     data using 1:3 with linespoints lw 2 pt 5    lc rgb "#2ca02c" title "fastfair mean", \
     data using 1:8 with linespoints lw 1.2 dt 2 pt 4 lc rgb "#2ca02c" title "fastfair p99.99", \
     data using 1:4 with linespoints lw 2 pt 9    lc rgb "#d62728" title "wbtree mean", \
     data using 1:9 with linespoints lw 1.2 dt 2 pt 8 lc rgb "#d62728" title "wbtree p99.99", \
     data using 1:5 with linespoints lw 2 pt 11   lc rgb "#9467bd" title "utree mean", \
     data using 1:10 with linespoints lw 1.2 dt 2 pt 10 lc rgb "#9467bd" title "utree p99.99", \
     data using 1:6 with linespoints lw 2 pt 13   lc rgb "#8c564b" title "fptree mean", \
     data using 1:11 with linespoints lw 1.2 dt 2 pt 12 lc rgb "#8c564b" title "fptree p99.99"

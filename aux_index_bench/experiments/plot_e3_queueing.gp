# plot_e3_queueing.gp — poster figure 4
#
# "The latency you see is not the latency the index caused."
# Open-loop Poisson: mean service time (dashed, ≈flat) vs mean queueing
# delay (solid, explodes near saturation) as the offered rate grows.
# A closed-loop benchmark can never show this — it self-throttles.
#
# Usage:
#   gnuplot -e "infile='e3_arrival_rate.csv'" plot_e3_queueing.gp
if (!exists("infile"))  infile  = "e3_arrival_rate.csv"
if (!exists("outfile")) outfile = "e3_queueing.png"
clean = "/tmp/aib_e3_clean.csv"
data  = "/tmp/aib_e3.dat"

system(sprintf("perl -0777 -pe 's/\\r//g; s/\\n(?!(?:index|btree|hash|fastfair|wbtree|utree|fptree|lbtree|dptree|bztree),)//g; s/\"([^\"]*)\"/($t=$1)=~s!,!;!gr/ge' \"%s\" > \"%s\"", infile, clean))

# Emits: rate_mops  <queue_mean per index...>  <svc_mean per index...>
awkprog = "awk -F, '\
NR==1 { for (i=1;i<=NF;i++) h[$i]=i; next } \
{ \
  idx=$(h[\"index\"]); r=$(h[\"rate\"])+0; \
  key=idx SUBSEP r; \
  q[key]+=$(h[\"queue_mean_ns\"]); s[key]+=$(h[\"svc_mean_ns\"]); c[key]++; \
  rs[r]=1; \
} \
END { \
  ni=split(\"btree fastfair wbtree utree fptree\", idxs, \" \"); \
  n=0; for (r in rs) arr[++n]=r+0; \
  for (i=1;i<=n;i++) for (j=i+1;j<=n;j++) \
    if (arr[i]>arr[j]) { t=arr[i]; arr[i]=arr[j]; arr[j]=t; } \
  for (i=1;i<=n;i++) { \
    r=arr[i]; printf \"%g\", r/1e6; \
    for (k=1;k<=ni;k++) { key=idxs[k] SUBSEP r; \
      if (c[key]>0) printf \" %g\", q[key]/c[key]/1000.0; else printf \" NaN\"; } \
    for (k=1;k<=ni;k++) { key=idxs[k] SUBSEP r; \
      if (c[key]>0) printf \" %g\", s[key]/c[key]/1000.0; else printf \" NaN\"; } \
    printf \"\\n\"; \
  } \
}'"
system(sprintf("%s \"%s\" > \"%s\"", awkprog, clean, data))

set terminal pngcairo enhanced color font "Helvetica,12" size 900,540
set output outfile
set title "Open-loop arrivals: queueing delay explodes, service time barely moves\n{/*0.8 queue wait (solid) vs service time (dashed); closed-loop benchmarks cannot show this}"
set xlabel "Offered rate (M ops/s)"
set ylabel "Latency ({/Symbol m}s)"
set logscale y
set grid xtics ytics
set datafile missing "NaN"
set key top left

plot data using 1:2 with linespoints lw 2 pt 7    lc rgb "#1f77b4" title "btree queue", \
     data using 1:7 with linespoints lw 1.2 dt 2 pt 6 lc rgb "#1f77b4" title "btree service", \
     data using 1:3 with linespoints lw 2 pt 5    lc rgb "#2ca02c" title "fastfair queue", \
     data using 1:8 with linespoints lw 1.2 dt 2 pt 4 lc rgb "#2ca02c" title "fastfair service", \
     data using 1:4 with linespoints lw 2 pt 9    lc rgb "#d62728" title "wbtree queue", \
     data using 1:9 with linespoints lw 1.2 dt 2 pt 8 lc rgb "#d62728" title "wbtree service", \
     data using 1:5 with linespoints lw 2 pt 11   lc rgb "#9467bd" title "utree queue", \
     data using 1:10 with linespoints lw 1.2 dt 2 pt 10 lc rgb "#9467bd" title "utree service", \
     data using 1:6 with linespoints lw 2 pt 13   lc rgb "#8c564b" title "fptree queue", \
     data using 1:11 with linespoints lw 1.2 dt 2 pt 12 lc rgb "#8c564b" title "fptree service"

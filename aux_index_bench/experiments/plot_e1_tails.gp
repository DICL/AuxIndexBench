# plot_e1_tails.gp — poster figure 2
#
# "Pollution moves the mean, not the tail."
# Per index, lookup mean (solid) and lookup p99.99 (dashed), both
# normalised to the pollution-free baseline. Solid climbs ~2x while
# dashed stays near 1 → the extreme tail is caused by something other
# than cache pollution (OS noise, TLB shootdowns, SMIs), so index-side
# cache tuning cannot fix it.
#
# Usage:
#   gnuplot -e "infile='e1_polluter.csv'" plot_e1_tails.gp
if (!exists("infile"))  infile  = "e1_polluter.csv"
if (!exists("outfile")) outfile = "e1_tails.png"
clean = "/tmp/aib_e1t_clean.csv"
data  = "/tmp/aib_e1t.dat"

system(sprintf("perl -0777 -pe 's/\\r//g; s/\\n(?!(?:index|btree|hash|fastfair|wbtree|utree|fptree|lbtree|dptree|bztree),)//g; s/\"([^\"]*)\"/($t=$1)=~s!,!;!gr/ge' \"%s\" > \"%s\"", infile, clean))

# Emits: x  <mean_norm per index...>  <p9999_norm per index...>
awkprog = "awk -F, '\
NR==1 { for (i=1;i<=NF;i++) h[$i]=i; next } \
{ \
  idx=$(h[\"index\"]); \
  b=$(h[\"bytes_per_call\"])+0; \
  if ($(h[\"workload\"])==\"none\") b=0; \
  key=idx SUBSEP b; \
  sm[key]+=$(h[\"lookup_mean_ns\"]);  cm[key]++; \
  st[key]+=$(h[\"lookup_p9999_ns\"]); \
  bytes[b]=1; \
} \
END { \
  ni=split(\"btree fastfair wbtree utree fptree\", idxs, \" \"); \
  n=0; for (b in bytes) arr[++n]=b; \
  for (i=1;i<=n;i++) for (j=i+1;j<=n;j++) \
    if (arr[i]+0>arr[j]+0) { t=arr[i]; arr[i]=arr[j]; arr[j]=t; } \
  for (k=1;k<=ni;k++) { \
    kb=idxs[k] SUBSEP 0; \
    bm[k]=(cm[kb]>0 ? sm[kb]/cm[kb] : -1); \
    bt[k]=(cm[kb]>0 ? st[kb]/cm[kb] : -1); \
  } \
  for (i=1;i<=n;i++) { \
    b=arr[i]+0; x=(b==0 ? 0.5 : b/1024.0); \
    printf \"%g\", x; \
    for (k=1;k<=ni;k++) { \
      key=idxs[k] SUBSEP b; \
      if (cm[key]>0 && bm[k]>0) printf \" %g\", (sm[key]/cm[key])/bm[k]; \
      else printf \" NaN\"; \
    } \
    for (k=1;k<=ni;k++) { \
      key=idxs[k] SUBSEP b; \
      if (cm[key]>0 && bt[k]>0) printf \" %g\", (st[key]/cm[key])/bt[k]; \
      else printf \" NaN\"; \
    } \
    printf \"\\n\"; \
  } \
}'"
system(sprintf("%s \"%s\" > \"%s\"", awkprog, clean, data))

set terminal pngcairo enhanced color font "Helvetica,12" size 900,540
set output outfile
set title "Pollution moves the mean, not the tail\n{/*0.8 lookup mean (solid) vs lookup p99.99 (dashed), normalised to no-pollution}"
set logscale x 2
set xrange [0.4:20000]
set xtics ("none" 0.5, "4K" 4, "32K" 32, "256K" 256, "2M" 2048, "16M" 16384)
set xlabel "Post-lookup working set per op"
set ylabel "Normalised to no-pollution baseline"
set yrange [0.5:3]
set grid xtics ytics
set datafile missing "NaN"
set key top left maxrows 5

plot 1 with lines lw 0.8 dt 3 lc rgb "#999999" notitle, \
     data using 1:2 with linespoints lw 2 pt 7    lc rgb "#1f77b4" title "btree mean", \
     data using 1:7 with linespoints lw 1.2 dt 2 pt 6 lc rgb "#1f77b4" title "btree p99.99", \
     data using 1:3 with linespoints lw 2 pt 5    lc rgb "#2ca02c" title "fastfair mean", \
     data using 1:8 with linespoints lw 1.2 dt 2 pt 4 lc rgb "#2ca02c" title "fastfair p99.99", \
     data using 1:5 with linespoints lw 2 pt 11   lc rgb "#9467bd" title "utree mean", \
     data using 1:10 with linespoints lw 1.2 dt 2 pt 10 lc rgb "#9467bd" title "utree p99.99"

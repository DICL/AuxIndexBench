# plot_e12_scaling.gp — poster figure 3
#
# "Cache pollution neutralises the value of lock-free design."
# Three panels (no / mid / heavy pollution): closed-loop mixed-workload
# throughput (s=0.6,u=0.1,i=0.3) vs worker threads. Thin dotted lines =
# ideal linear scaling from each index's own 1-worker point.
#   * left panel: lock-free indexes (fastfair/fptree/utree) pull away
#     from global-lock indexes as workers grow;
#   * right panel: everyone collapses onto the same memory-bound curve —
#     the CC design stops mattering.
#
# Usage:
#   gnuplot -e "infile='e12_concurrency.csv'" plot_e12_scaling.gp
#   panel byte sizes default to 0 / 65536 / 1048576; override with
#   -e "b1=0; b2=65536; b3=1048576"
if (!exists("infile"))  infile  = "e12_concurrency.csv"
if (!exists("outfile")) outfile = "e12_scaling.png"
if (!exists("b1")) b1 = 0
if (!exists("b2")) b2 = 65536
if (!exists("b3")) b3 = 1048576
clean = "/tmp/aib_e12_clean.csv"
data  = "/tmp/aib_e12.dat"

system(sprintf("perl -0777 -pe 's/\\r//g; s/\\n(?!(?:index|btree|hash|fastfair|wbtree|utree|fptree|lbtree|dptree|bztree),)//g; s/\"([^\"]*)\"/($t=$1)=~s!,!;!gr/ge' \"%s\" > \"%s\"", infile, clean))

# Emits per row: bytes workers  <tput per index...>  <ideal per index...>
awkprog = "awk -F, '\
NR==1 { for (i=1;i<=NF;i++) h[$i]=i; next } \
{ \
  idx=$(h[\"index\"]); \
  b=$(h[\"bytes_per_call\"])+0; \
  if ($(h[\"workload\"])==\"none\") b=0; \
  w=$(h[\"workers\"])+0; \
  key=idx SUBSEP b SUBSEP w; \
  sum[key]+=$(h[\"throughput_mops\"]); cnt[key]++; \
  bs[b]=1; ws[w]=1; \
} \
END { \
  ni=split(\"btree fastfair wbtree utree fptree\", idxs, \" \"); \
  nw=0; for (w in ws) wa[++nw]=w+0; \
  for (i=1;i<=nw;i++) for (j=i+1;j<=nw;j++) \
    if (wa[i]>wa[j]) { t=wa[i]; wa[i]=wa[j]; wa[j]=t; } \
  for (b in bs) { \
    for (k=1;k<=ni;k++) { \
      kb=idxs[k] SUBSEP b SUBSEP wa[1]; \
      base[k]=(cnt[kb]>0 ? sum[kb]/cnt[kb] : -1); \
    } \
    for (i=1;i<=nw;i++) { \
      printf \"%d %d\", b, wa[i]; \
      for (k=1;k<=ni;k++) { \
        key=idxs[k] SUBSEP b SUBSEP wa[i]; \
        if (cnt[key]>0) printf \" %g\", sum[key]/cnt[key]; else printf \" NaN\"; \
      } \
      for (k=1;k<=ni;k++) { \
        if (base[k]>0) printf \" %g\", base[k]*wa[i]/wa[1]; else printf \" NaN\"; \
      } \
      printf \"\\n\"; \
    } \
  } \
}'"
system(sprintf("%s \"%s\" > \"%s\"", awkprog, clean, data))

set terminal pngcairo enhanced color font "Helvetica,11" size 1500,520
set output outfile
set multiplot layout 1,3 \
    title "E12: pollution neutralises lock-free design (mix s=0.6,u=0.1,i=0.3; dotted = ideal scaling)"

set logscale x 2
set xlabel "Worker threads"
set grid xtics ytics
set datafile missing "NaN"
set key top left font ",9"

lbl(b) = (b == 0 ? "no pollution" : sprintf("%d KiB / op", b/1024))

do for [p=1:3] {
    B = (p==1 ? b1 : (p==2 ? b2 : b3))
    set title lbl(B)
    if (p == 1) { set ylabel "Throughput (Mops/s)" } else { unset ylabel }
    plot data using ($1==B ? $2 : NaN):3  with linespoints lw 2 pt 7  lc rgb "#1f77b4" title "btree (lock)", \
         data using ($1==B ? $2 : NaN):4  with linespoints lw 2 pt 5  lc rgb "#2ca02c" title "fastfair (lock-free)", \
         data using ($1==B ? $2 : NaN):5  with linespoints lw 2 pt 9  lc rgb "#d62728" title "wbtree (lock)", \
         data using ($1==B ? $2 : NaN):6  with linespoints lw 2 pt 11 lc rgb "#9467bd" title "utree (lock-free)", \
         data using ($1==B ? $2 : NaN):7  with linespoints lw 2 pt 13 lc rgb "#8c564b" title "fptree (lock-free)", \
         data using ($1==B ? $2 : NaN):9  with lines lw 0.8 dt 3 lc rgb "#2ca02c" notitle, \
         data using ($1==B ? $2 : NaN):11 with lines lw 0.8 dt 3 lc rgb "#9467bd" notitle, \
         data using ($1==B ? $2 : NaN):12 with lines lw 0.8 dt 3 lc rgb "#8c564b" notitle
}
unset multiplot

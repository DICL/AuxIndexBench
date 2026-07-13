# plot_e9_mix_pollute.gp — key metric: mixed-workload lookup vs pollution
# Complement of E1 (read-only): with writes in the mix (s=0.6,u=0.4),
# does pollution still grow lookup logarithmically? Lines per index.
# Usage: gnuplot -e "infile='e9_mix_x_pollute.csv'" plot_e9_mix_pollute.gp
if (!exists("infile"))  infile  = "e9_mix_x_pollute.csv"
if (!exists("outfile")) outfile = "e9_mix_pollute.png"
clean = "/tmp/aib_e9_clean.csv"
data  = "/tmp/aib_e9.dat"
system(sprintf("perl -0777 -pe 's/\\r//g; s/\\n(?!(?:index|btree|hash|fastfair|wbtree|utree|fptree|lbtree|dptree|bztree),)//g; s/\"([^\"]*)\"/($t=$1)=~s!,!;!gr/ge' \"%s\" > \"%s\"", infile, clean))
awkprog = "awk -F, '\
NR==1 { for (i=1;i<=NF;i++) h[$i]=i; next } \
{ b=$(h[\"bytes_per_call\"])+0; if ($(h[\"workload\"])==\"none\") b=0; \
  key=$(h[\"index\"]) SUBSEP b; \
  s[key]+=$(h[\"lookup_mean_ns\"]); c[key]++; bs[b]=1; } \
END { ni=split(\"btree fastfair wbtree utree fptree\", idxs, \" \"); \
  n=0; for (b in bs) arr[++n]=b+0; \
  for (i=1;i<=n;i++) for (j=i+1;j<=n;j++) if (arr[i]>arr[j]) {t=arr[i];arr[i]=arr[j];arr[j]=t;} \
  for (i=1;i<=n;i++) { b=arr[i]; x=(b==0 ? 0.5 : b/1024.0); printf \"%g\", x; \
    for (k=1;k<=ni;k++) { key=idxs[k] SUBSEP b; \
      if (c[key]>0) printf \" %g\", s[key]/c[key]; else printf \" NaN\"; } \
    printf \"\\n\"; } }'"
system(sprintf("%s \"%s\" > \"%s\"", awkprog, clean, data))
set terminal pngcairo enhanced color font "Helvetica,12" size 900,540
set output outfile
set title "Mixed workload (s=0.6,u=0.4): index time vs pollution\n{/*0.8 write path (clflush) shifts curves up; log-in-pollution shape persists}"
set logscale x 2
set logscale y
set xrange [0.4:20000]
set xtics ("none" 0.5, "4K" 4, "32K" 32, "256K" 256, "2M" 2048, "16M" 16384)
set xlabel "Post-op working set per op"
set ylabel "Mean index time per op (ns)"
set grid xtics ytics
set datafile missing "NaN"
set key top left
plot data using 1:2 with linespoints lw 2 pt 7  lc rgb "#1f77b4" title "btree", \
     data using 1:3 with linespoints lw 2 pt 5  lc rgb "#2ca02c" title "fastfair", \
     data using 1:4 with linespoints lw 2 pt 9  lc rgb "#d62728" title "wbtree", \
     data using 1:5 with linespoints lw 2 pt 11 lc rgb "#9467bd" title "utree", \
     data using 1:6 with linespoints lw 2 pt 13 lc rgb "#8c564b" title "fptree"

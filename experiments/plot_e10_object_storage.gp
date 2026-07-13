# plot_e10_object_storage.gp — key metric: lookup latency under realistic
# post-op work. Two panels: object-access (touch an object of size B) vs
# storage-stack (buffer-copy pattern). Calibrates the synthetic polluter
# axis against work that real applications do after an index hit.
# Usage: gnuplot -e "infile='e10_object_storage.csv'" plot_e10_object_storage.gp
if (!exists("infile"))  infile  = "e10_object_storage.csv"
if (!exists("outfile")) outfile = "e10_object_storage.png"
clean = "/tmp/aib_e10_clean.csv"
data  = "/tmp/aib_e10.dat"
system(sprintf("perl -0777 -pe 's/\\r//g; s/\\n(?!(?:index|btree|hash|fastfair|wbtree|utree|fptree|lbtree|dptree|bztree),)//g; s/\"([^\"]*)\"/($t=$1)=~s!,!;!gr/ge' \"%s\" > \"%s\"", infile, clean))
# Emits: bytes_kib  <object: per-index>  <storage: per-index>
awkprog = "awk -F, '\
NR==1 { for (i=1;i<=NF;i++) h[$i]=i; next } \
{ w=$(h[\"workload\"]); b=$(h[\"bytes_per_call\"])+0; \
  key=$(h[\"index\"]) SUBSEP w SUBSEP b; \
  s[key]+=$(h[\"lookup_mean_ns\"]); c[key]++; bs[b]=1; } \
END { ni=split(\"btree fastfair wbtree utree fptree\", idxs, \" \"); \
  n=0; for (b in bs) arr[++n]=b+0; \
  for (i=1;i<=n;i++) for (j=i+1;j<=n;j++) if (arr[i]>arr[j]) {t=arr[i];arr[i]=arr[j];arr[j]=t;} \
  for (i=1;i<=n;i++) { b=arr[i]; printf \"%g\", b/1024.0; \
    for (k=1;k<=ni;k++) { key=idxs[k] SUBSEP \"object\" SUBSEP b; \
      if (c[key]>0) printf \" %g\", s[key]/c[key]; else printf \" NaN\"; } \
    for (k=1;k<=ni;k++) { key=idxs[k] SUBSEP \"storage\" SUBSEP b; \
      if (c[key]>0) printf \" %g\", s[key]/c[key]; else printf \" NaN\"; } \
    printf \"\\n\"; } }'"
system(sprintf("%s \"%s\" > \"%s\"", awkprog, clean, data))
set terminal pngcairo enhanced color font "Helvetica,12" size 1200,540
set output outfile
set multiplot layout 1,2 title "Realistic post-op work vs index lookup latency"
set logscale x 2
set xlabel "Object / buffer size per op (KiB)"
set grid xtics ytics
set datafile missing "NaN"
set key top left
set title "object access"
set ylabel "Mean lookup latency (ns)"
plot data using 1:2 with linespoints lw 2 pt 7  lc rgb "#1f77b4" title "btree", \
     data using 1:3 with linespoints lw 2 pt 5  lc rgb "#2ca02c" title "fastfair", \
     data using 1:4 with linespoints lw 2 pt 9  lc rgb "#d62728" title "wbtree", \
     data using 1:5 with linespoints lw 2 pt 11 lc rgb "#9467bd" title "utree", \
     data using 1:6 with linespoints lw 2 pt 13 lc rgb "#8c564b" title "fptree"
set title "storage stack"
unset ylabel
plot data using 1:7  with linespoints lw 2 pt 7  lc rgb "#1f77b4" title "btree", \
     data using 1:8  with linespoints lw 2 pt 5  lc rgb "#2ca02c" title "fastfair", \
     data using 1:9  with linespoints lw 2 pt 9  lc rgb "#d62728" title "wbtree", \
     data using 1:10 with linespoints lw 2 pt 11 lc rgb "#9467bd" title "utree", \
     data using 1:11 with linespoints lw 2 pt 13 lc rgb "#8c564b" title "fptree"
unset multiplot

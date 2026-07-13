# plot_e5_skew.gp — key metric: lookup latency vs Zipf theta
# Skew concentrates queries on hot keys → upper path stays cached →
# latency falls. How much each index benefits from skew.
# Usage: gnuplot -e "infile='e5_skew.csv'" plot_e5_skew.gp
if (!exists("infile"))  infile  = "e5_skew.csv"
if (!exists("outfile")) outfile = "e5_skew.png"
clean = "/tmp/aib_e5_clean.csv"
data  = "/tmp/aib_e5.dat"
system(sprintf("perl -0777 -pe 's/\\r//g; s/\\n(?!(?:index|btree|hash|fastfair|wbtree|utree|fptree|lbtree|dptree|bztree),)//g; s/\"([^\"]*)\"/($t=$1)=~s!,!;!gr/ge' \"%s\" > \"%s\"", infile, clean))
awkprog = "awk -F, '\
NR==1 { for (i=1;i<=NF;i++) h[$i]=i; next } \
{ t=$(h[\"theta\"])+0; key=$(h[\"index\"]) SUBSEP sprintf(\"%.3f\",t); \
  s[key]+=$(h[\"lookup_mean_ns\"]); c[key]++; ts[sprintf(\"%.3f\",t)]=1; } \
END { ni=split(\"btree fastfair wbtree utree fptree\", idxs, \" \"); \
  n=0; for (t in ts) arr[++n]=t; \
  for (i=1;i<=n;i++) for (j=i+1;j<=n;j++) if (arr[i]+0>arr[j]+0) {x=arr[i];arr[i]=arr[j];arr[j]=x;} \
  for (i=1;i<=n;i++) { printf \"%s\", arr[i]; \
    for (k=1;k<=ni;k++) { key=idxs[k] SUBSEP arr[i]; \
      if (c[key]>0) printf \" %g\", s[key]/c[key]; else printf \" NaN\"; } \
    printf \"\\n\"; } }'"
system(sprintf("%s \"%s\" > \"%s\"", awkprog, clean, data))
set terminal pngcairo enhanced color font "Helvetica,12" size 900,540
set output outfile
set title "Access skew: hot keys keep the cache warm"
set xlabel "Zipf theta (0 = uniform)"
set ylabel "Mean lookup latency (ns)"
set grid xtics ytics
set datafile missing "NaN"
set key top right
plot data using 1:2 with linespoints lw 2 pt 7  lc rgb "#1f77b4" title "btree", \
     data using 1:3 with linespoints lw 2 pt 5  lc rgb "#2ca02c" title "fastfair", \
     data using 1:4 with linespoints lw 2 pt 9  lc rgb "#d62728" title "wbtree", \
     data using 1:5 with linespoints lw 2 pt 11 lc rgb "#9467bd" title "utree", \
     data using 1:6 with linespoints lw 2 pt 13 lc rgb "#8c564b" title "fptree"

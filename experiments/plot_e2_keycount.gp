# plot_e2_keycount.gp — key metric: index-only lookup latency vs index size
# The O(log n) picture: near-straight lines on log-x = logarithmic depth.
# Usage: gnuplot -e "infile='e2_keycount.csv'" plot_e2_keycount.gp
if (!exists("infile"))  infile  = "e2_keycount.csv"
if (!exists("outfile")) outfile = "e2_keycount.png"
clean = "/tmp/aib_e2_clean.csv"
data  = "/tmp/aib_e2.dat"
system(sprintf("perl -0777 -pe 's/\\r//g; s/\\n(?!(?:index|btree|hash|fastfair|wbtree|utree|fptree|lbtree|dptree|bztree),)//g; s/\"([^\"]*)\"/($t=$1)=~s!,!;!gr/ge' \"%s\" > \"%s\"", infile, clean))
awkprog = "awk -F, '\
NR==1 { for (i=1;i<=NF;i++) h[$i]=i; next } \
{ k=$(h[\"keys\"])+0; key=$(h[\"index\"]) SUBSEP k; \
  s[key]+=$(h[\"lookup_mean_ns\"]); c[key]++; ks[k]=1; } \
END { ni=split(\"btree fastfair wbtree utree fptree lbtree\", idxs, \" \"); \
  n=0; for (k in ks) arr[++n]=k+0; \
  for (i=1;i<=n;i++) for (j=i+1;j<=n;j++) if (arr[i]>arr[j]) {t=arr[i];arr[i]=arr[j];arr[j]=t;} \
  for (i=1;i<=n;i++) { printf \"%g\", arr[i]; \
    for (kk=1;kk<=ni;kk++) { key=idxs[kk] SUBSEP arr[i]; \
      if (c[key]>0) printf \" %g\", s[key]/c[key]; else printf \" NaN\"; } \
    printf \"\\n\"; } }'"
system(sprintf("%s \"%s\" > \"%s\"", awkprog, clean, data))
set terminal pngcairo enhanced color font "Helvetica,12" size 900,540
set output outfile
set title "Lookup latency vs index size (polluter excluded)"
set logscale x
set xlabel "Keys in index"
set ylabel "Mean lookup latency (ns)"
set grid xtics ytics
set datafile missing "NaN"
set key top left
plot data using 1:2 with linespoints lw 2 pt 7  lc rgb "#1f77b4" title "btree", \
     data using 1:3 with linespoints lw 2 pt 5  lc rgb "#2ca02c" title "fastfair", \
     data using 1:4 with linespoints lw 2 pt 9  lc rgb "#d62728" title "wbtree", \
     data using 1:5 with linespoints lw 2 pt 11 lc rgb "#9467bd" title "utree", \
     data using 1:6 with linespoints lw 2 pt 13 lc rgb "#8c564b" title "fptree", \
     data using 1:7 with linespoints lw 2 pt 4  lc rgb "#e377c2" title "lbtree"

# plot_e7_clients.gp — key metric: drain throughput vs consumer threads
# Open-loop Poisson at fixed offered rate: how many worker threads it
# takes each index to keep up (throughput plateaus at the offered rate).
# Usage: gnuplot -e "infile='e7_clients.csv'" plot_e7_clients.gp
if (!exists("infile"))  infile  = "e7_clients.csv"
if (!exists("outfile")) outfile = "e7_clients.png"
clean = "/tmp/aib_e7_clean.csv"
data  = "/tmp/aib_e7.dat"
system(sprintf("perl -0777 -pe 's/\\r//g; s/\\n(?!(?:index|btree|hash|fastfair|wbtree|utree|fptree|lbtree|dptree|bztree),)//g; s/\"([^\"]*)\"/($t=$1)=~s!,!;!gr/ge' \"%s\" > \"%s\"", infile, clean))
awkprog = "awk -F, '\
NR==1 { for (i=1;i<=NF;i++) h[$i]=i; next } \
{ cl=$(h[\"clients\"])+0; key=$(h[\"index\"]) SUBSEP cl; \
  tp[key]+=$(h[\"throughput_mops\"]); qd[key]+=$(h[\"e2e_p99_ns\"]); c[key]++; \
  cs[cl]=1; rate=$(h[\"rate\"])+0; } \
END { ni=split(\"btree fastfair wbtree utree fptree\", idxs, \" \"); \
  n=0; for (x in cs) arr[++n]=x+0; \
  for (i=1;i<=n;i++) for (j=i+1;j<=n;j++) if (arr[i]>arr[j]) {t=arr[i];arr[i]=arr[j];arr[j]=t;} \
  printf \"# offered_rate_mops %g\\n\", rate/1e6; \
  for (i=1;i<=n;i++) { printf \"%d\", arr[i]; \
    for (k=1;k<=ni;k++) { key=idxs[k] SUBSEP arr[i]; \
      if (c[key]>0) printf \" %g %g\", tp[key]/c[key], qd[key]/c[key]/1e6; \
      else printf \" NaN NaN\"; } \
    printf \"\\n\"; } }'"
system(sprintf("%s \"%s\" > \"%s\"", awkprog, clean, data))
offered = system(sprintf("head -1 %s | awk '{print $3}'", data)) + 0
set terminal pngcairo enhanced color font "Helvetica,12" size 1200,540
set output outfile
set multiplot layout 1,2 title "Consumer scaling at fixed offered rate"
set logscale x 2
set xlabel "Consumer threads"
set grid xtics ytics
set datafile missing "NaN"
set key bottom right
set title "completed throughput"
set ylabel "Throughput (Mops/s)"
plot offered with lines lw 1 dt 3 lc rgb "#666666" title sprintf("offered %.1f M/s", offered), \
     data using 1:2  with linespoints lw 2 pt 7  lc rgb "#1f77b4" title "btree", \
     data using 1:4  with linespoints lw 2 pt 5  lc rgb "#2ca02c" title "fastfair", \
     data using 1:6  with linespoints lw 2 pt 9  lc rgb "#d62728" title "wbtree", \
     data using 1:8  with linespoints lw 2 pt 11 lc rgb "#9467bd" title "utree", \
     data using 1:10 with linespoints lw 2 pt 13 lc rgb "#8c564b" title "fptree"
set title "e2e p99"
set ylabel "e2e p99 (ms)"
set logscale y
set key top right
plot data using 1:3  with linespoints lw 2 pt 7  lc rgb "#1f77b4" title "btree", \
     data using 1:5  with linespoints lw 2 pt 5  lc rgb "#2ca02c" title "fastfair", \
     data using 1:7  with linespoints lw 2 pt 9  lc rgb "#d62728" title "wbtree", \
     data using 1:9  with linespoints lw 2 pt 11 lc rgb "#9467bd" title "utree", \
     data using 1:11 with linespoints lw 2 pt 13 lc rgb "#8c564b" title "fptree"
unset multiplot

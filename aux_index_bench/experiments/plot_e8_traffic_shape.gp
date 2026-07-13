# plot_e8_traffic_shape.gp — key metric: tail latency across traffic shapes
# Same MEAN rate, different shapes (steady/burst/level/sine/sine+burst).
# Grouped bars of e2e p99.99: a single "rate" knob cannot reproduce these.
# Usage: gnuplot -e "infile='e8_traffic_shape.csv'" plot_e8_traffic_shape.gp
if (!exists("infile"))  infile  = "e8_traffic_shape.csv"
if (!exists("outfile")) outfile = "e8_traffic_shape.png"
clean = "/tmp/aib_e8_clean.csv"
data  = "/tmp/aib_e8.dat"
system(sprintf("perl -0777 -pe 's/\\r//g; s/\\n(?!(?:index|btree|hash|fastfair|wbtree|utree|fptree|lbtree|dptree|bztree),)//g; s/\"([^\"]*)\"/($t=$1)=~s!,!;!gr/ge' \"%s\" > \"%s\"", infile, clean))
# Shape derived from traffic params.
awkprog = "awk -F, '\
NR==1 { for (i=1;i<=NF;i++) h[$i]=i; next } \
{ sa=$(h[\"sin_amp\"])+0; lp=$(h[\"level_period\"])+0; bp=$(h[\"burst_prob\"])+0; \
  shape=\"steady\"; \
  if (sa>0 && bp>0) shape=\"sine+burst\"; \
  else if (sa>0) shape=\"sine\"; \
  else if (lp>0) shape=\"level\"; \
  else if (bp>0) shape=\"burst\"; \
  if (!(shape in seen)) { seen[shape]=1; order[++nsh]=shape; } \
  key=$(h[\"index\"]) SUBSEP shape; \
  t[key]+=$(h[\"e2e_p9999_ns\"]); c[key]++; } \
END { ni=split(\"btree fastfair wbtree utree fptree\", idxs, \" \"); \
  for (i=1;i<=nsh;i++) { sh=order[i]; printf \"\\\"%s\\\"\", sh; \
    for (k=1;k<=ni;k++) { key=idxs[k] SUBSEP sh; \
      if (c[key]>0) printf \" %g\", t[key]/c[key]/1e6; else printf \" NaN\"; } \
    printf \"\\n\"; } }'"
system(sprintf("%s \"%s\" > \"%s\"", awkprog, clean, data))
set terminal pngcairo enhanced color font "Helvetica,12" size 1000,560
set output outfile
set title "Traffic shape vs tail latency (same mean offered rate)"
set ylabel "e2e p99.99 (ms)"
set style data histogram
set style histogram clustered gap 1
set style fill solid 0.85 border -1
set boxwidth 0.9
set grid ytics
set datafile missing "NaN"
set key top left
set logscale y
plot data using 2:xtic(1) lc rgb "#1f77b4" title "btree", \
     ''   using 3         lc rgb "#2ca02c" title "fastfair", \
     ''   using 4         lc rgb "#d62728" title "wbtree", \
     ''   using 5         lc rgb "#9467bd" title "utree", \
     ''   using 6         lc rgb "#8c564b" title "fptree"

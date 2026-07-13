# plot_e6_opmix.gp — key metric: service time across CRUD mixes
# Grouped bars: how each index's mean service time responds to writes.
# PMEM trees pay clflush on the write path — invisible in read-only runs.
# Usage: gnuplot -e "infile='e6_opmix.csv'" plot_e6_opmix.gp
if (!exists("infile"))  infile  = "e6_opmix.csv"
if (!exists("outfile")) outfile = "e6_opmix.png"
clean = "/tmp/aib_e6_clean.csv"
data  = "/tmp/aib_e6.dat"
system(sprintf("perl -0777 -pe 's/\\r//g; s/\\n(?!(?:index|btree|hash|fastfair|wbtree|utree|fptree|lbtree|dptree|bztree),)//g; s/\"([^\"]*)\"/($t=$1)=~s!,!;!gr/ge' \"%s\" > \"%s\"", infile, clean))
# One row per mix (input order), columns per index; label = shortened mix.
awkprog = "awk -F, '\
NR==1 { for (i=1;i<=NF;i++) h[$i]=i; next } \
{ m=$(h[\"op_mix\"]); gsub(/\"/,\"\",m); \
  if (!(m in seen)) { seen[m]=1; order[++nm]=m; } \
  key=$(h[\"index\"]) SUBSEP m; s[key]+=$(h[\"svc_mean_ns\"]); c[key]++; } \
END { ni=split(\"btree fastfair wbtree utree fptree\", idxs, \" \"); \
  for (i=1;i<=nm;i++) { m=order[i]; lbl=m; \
    gsub(/s=/,\"s\",lbl); gsub(/u=/,\"u\",lbl); gsub(/i=/,\"i\",lbl); \
    gsub(/d=/,\"d\",lbl); gsub(/sc=/,\"sc\",lbl); gsub(/;/,\" \",lbl); gsub(/0\\./,\".\",lbl); \
    printf \"\\\"%s\\\"\", lbl; \
    for (k=1;k<=ni;k++) { key=idxs[k] SUBSEP m; \
      if (c[key]>0) printf \" %g\", s[key]/c[key]/1000.0; else printf \" NaN\"; } \
    printf \"\\n\"; } }'"
system(sprintf("%s \"%s\" > \"%s\"", awkprog, clean, data))
set terminal pngcairo enhanced color font "Helvetica,11" size 1100,560
set output outfile
set title "Service time across op mixes (polluter included)"
set ylabel "Mean service time ({/Symbol m}s)"
set style data histogram
set style histogram clustered gap 1
set style fill solid 0.85 border -1
set boxwidth 0.9
set xtics rotate by -25 font ",9"
set yrange [0:*]
set bmargin 5
set grid ytics
set datafile missing "NaN"
set key top left
plot data using 2:xtic(1) lc rgb "#1f77b4" title "btree", \
     ''   using 3         lc rgb "#2ca02c" title "fastfair", \
     ''   using 4         lc rgb "#d62728" title "wbtree", \
     ''   using 5         lc rgb "#9467bd" title "utree", \
     ''   using 6         lc rgb "#8c564b" title "fptree"

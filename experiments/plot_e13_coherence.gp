# plot_e13_coherence.gp
#
# Usage:
#   gnuplot -e "infile='e13_coherence.csv'" plot_e13_coherence.gp
#   gnuplot -e "infile='e13_coherence.csv'; infile2='e13_uniform.csv'" plot_e13_coherence.gp
#
# infile   : zipf / cache-resident run   (solid lines)
# infile2  : optional uniform / DRAM-sized contrast run (dashed lines).
#            If the solid lines dive at wf<=0.01 while the dashed ones
#            hug 1.0, the dive is coherence (hot shared lines), not
#            write-path cost.
#
# Panels:
#   left  = absolute throughput (Mops/s) vs write fraction
#   right = throughput normalised to wf=0  <-- the money plot
if (!exists("infile"))  infile  = "e13_coherence.csv"
if (!exists("outfile")) outfile = "e13_coherence.png"
have2 = exists("infile2") ? 1 : 0

clean  = "/tmp/aib_e13_clean.csv"
data   = "/tmp/aib_e13.dat"
clean2 = "/tmp/aib_e13_clean2.csv"
data2  = "/tmp/aib_e13_2.dat"

# Repair wrapped lines; commas inside quotes -> semicolons.
fixcmd = "perl -0777 -pe 's/\\r//g; s/\\n(?!(?:index|btree|hash|fastfair|wbtree|utree|fptree|lbtree|dptree|bztree),)//g; s/\"([^\"]*)\"/($t=$1)=~s!,!;!gr/ge'"

# Aggregate: mean throughput by (index, write-fraction); write-fraction
# derived from the op_mix string (wf = 1 - s). Emits:
#   x  ff fp ut  ff_norm fp_norm ut_norm
awkprog = "awk -F, '\
NR==1 { for (i=1;i<=NF;i++) h[$i]=i; next } \
{ \
  idx=$(h[\"index\"]); \
  m=$(h[\"op_mix\"]); gsub(/\"/,\"\",m); \
  wf=0; n=split(m, parts, \";\"); \
  for (p=1;p<=n;p++) if (parts[p] ~ /^s=/) { s=substr(parts[p],3)+0; wf=1-s; } \
  key=idx SUBSEP sprintf(\"%.6f\", wf); \
  sum[key]+=$(h[\"throughput_mops\"]); cnt[key]++; wfs[sprintf(\"%.6f\", wf)]=1; \
} \
END { \
  ni=split(\"fastfair fptree utree\", idxs, \" \"); \
  n=0; for (w in wfs) arr[++n]=w; \
  for (i=1;i<=n;i++) for (j=i+1;j<=n;j++) \
    if (arr[i]+0 > arr[j]+0) { t=arr[i]; arr[i]=arr[j]; arr[j]=t; } \
  for (k=1;k<=ni;k++) { \
    b=idxs[k] SUBSEP sprintf(\"%.6f\", 0); \
    base[k]=(cnt[b]>0 ? sum[b]/cnt[b] : -1); \
  } \
  for (i=1;i<=n;i++) { \
    wf=arr[i]+0; \
    x=(wf==0 ? 0.0003 : wf); \
    printf \"%g\", x; \
    for (k=1;k<=ni;k++) { \
      key=idxs[k] SUBSEP arr[i]; \
      if (cnt[key]>0) printf \" %g\", sum[key]/cnt[key]; else printf \" NaN\"; \
    } \
    for (k=1;k<=ni;k++) { \
      key=idxs[k] SUBSEP arr[i]; \
      if (cnt[key]>0 && base[k]>0) printf \" %g\", (sum[key]/cnt[key])/base[k]; \
      else printf \" NaN\"; \
    } \
    printf \"\\n\"; \
  } \
}'"

system(sprintf("%s \"%s\" > \"%s\"", fixcmd, infile, clean))
system(sprintf("%s \"%s\" > \"%s\"", awkprog, clean, data))
if (have2) system(sprintf("%s \"%s\" > \"%s\"", fixcmd, infile2, clean2))
if (have2) system(sprintf("%s \"%s\" > \"%s\"", awkprog, clean2, data2))

set terminal pngcairo enhanced color font "Helvetica,12" size 1200,540
set output outfile

set multiplot layout 1,2 title "E13: coherence sensitivity — tiny write fractions vs throughput"

set logscale x
set xrange [0.0002:0.6]
set xtics ("0" 0.0003, "0.1%%" 0.001, "0.5%%" 0.005, "1%%" 0.01, \
           "2%%" 0.02, "5%%" 0.05, "10%%" 0.1, "20%%" 0.2, "40%%" 0.4)
set xlabel "Write fraction (update+insert)"
set grid xtics ytics
set datafile missing "NaN"
set key bottom left

# ---- left: absolute -------------------------------------------------------
set ylabel "Throughput (Mops/s)"
set title "absolute"
if (have2) {
  plot data  using 1:2 with linespoints lw 2 pt 7  lc rgb "#2ca02c" title "fastfair (zipf)", \
       data  using 1:3 with linespoints lw 2 pt 5  lc rgb "#8c564b" title "fptree (zipf)", \
       data  using 1:4 with linespoints lw 2 pt 9  lc rgb "#9467bd" title "utree (zipf)", \
       data2 using 1:2 with linespoints lw 1.2 dt 2 pt 6  lc rgb "#2ca02c" title "fastfair (uniform)", \
       data2 using 1:3 with linespoints lw 1.2 dt 2 pt 4  lc rgb "#8c564b" title "fptree (uniform)", \
       data2 using 1:4 with linespoints lw 1.2 dt 2 pt 8  lc rgb "#9467bd" title "utree (uniform)"
} else {
  plot data  using 1:2 with linespoints lw 2 pt 7  lc rgb "#2ca02c" title "fastfair", \
       data  using 1:3 with linespoints lw 2 pt 5  lc rgb "#8c564b" title "fptree", \
       data  using 1:4 with linespoints lw 2 pt 9  lc rgb "#9467bd" title "utree"
}

# ---- right: normalised ----------------------------------------------------
set ylabel "Throughput / Throughput(wf=0)"
set title "normalised to read-only (drop at wf{\\<=}1% {/Symbol \\273} coherence, not Amdahl)"
set yrange [0:1.1]
if (have2) {
  plot 1 with lines lw 0.8 dt 3 lc rgb "#888888" notitle, \
       data  using 1:5 with linespoints lw 2 pt 7  lc rgb "#2ca02c" title "fastfair (zipf)", \
       data  using 1:6 with linespoints lw 2 pt 5  lc rgb "#8c564b" title "fptree (zipf)", \
       data  using 1:7 with linespoints lw 2 pt 9  lc rgb "#9467bd" title "utree (zipf)", \
       data2 using 1:5 with linespoints lw 1.2 dt 2 pt 6  lc rgb "#2ca02c" title "fastfair (uniform)", \
       data2 using 1:6 with linespoints lw 1.2 dt 2 pt 4  lc rgb "#8c564b" title "fptree (uniform)", \
       data2 using 1:7 with linespoints lw 1.2 dt 2 pt 8  lc rgb "#9467bd" title "utree (uniform)"
} else {
  plot 1 with lines lw 0.8 dt 3 lc rgb "#888888" notitle, \
       data  using 1:5 with linespoints lw 2 pt 7  lc rgb "#2ca02c" title "fastfair", \
       data  using 1:6 with linespoints lw 2 pt 5  lc rgb "#8c564b" title "fptree", \
       data  using 1:7 with linespoints lw 2 pt 9  lc rgb "#9467bd" title "utree"
}

unset multiplot

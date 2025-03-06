#
#  More figures for import into the pdf and html versions of the User Manual.
#
#  EAM - January 2025
#

if (strstrt(GPVAL_TERMINALS, " windows ") == 0) {
   fontspec = "Times,12"
} else {
   fontspec = "Tahoma,12"
}
MANUAL_FIGURES = 1

if (!exists("winhelp")) winhelp = 0
if (winhelp > 0) {
#   prefer pngcairo over gd based png
    if (strstrt(GPVAL_TERMINALS, " pngcairo ") > 0) {
        set term pngcairo font fontspec size 448,225 dashlength 0.2 fontscale 0.6
    } else {
        set term png font fontspec size 448,225 dashlength 0.2 fontscale 0.6
    }
    out = "./windows/"
} else if (GNUTERM eq "svg") {
    set term svg font 'Calisto MT,14' size 600,400
    out = "./html/"
} else if (GNUTERM eq "tikz") {
    set term tikz color fontscale 0.75 clip size 3.0in, 1.7in
    out = "./"
} else {
    set term pdfcairo color font fontspec size 3.5,2.0 dashlength 0.2
# pdfs that need colour have their own terminal setting, check below
    out = "./"
}

set loadpath '../demo'

if (GPVAL_TERM eq "pngcairo" || GPVAL_TERM eq "png") ext=".png"
if (GPVAL_TERM eq "pdfcairo" || GPVAL_TERM eq "pdf") ext=".pdf"
if (GPVAL_TERM eq "svg") ext=".svg"
if (GPVAL_TERM eq "tikz") ext=".tex"

set encoding utf8

set linetype 4 lc "black"

#
# Marks example custom point shapes
# =================================
#
set output out . 'figure_markpoints' . ext
set title "Custom point shapes defined as marks"
set xrange [-16 : -8.3]
set yrange [-1.1 : 1.1]
unset border
unset tics
unset key

FILLSTROKE = 3
array Square = [{-1,-1}, { 1,-1}, { 1, 1}, {-1, 1}, {-1,-1}]
array Triangle = [{0,1.2}, {1.04,-0.6}, {-1.04,-0.6}, {0,1.2}]
set mark 1 Square using 2:3:(FILLSTROKE) fc bgnd title "square"
set mark 2 Triangle using 2:3:(FILLSTROKE) fc bgnd title "triangle"
set angle degrees
set mark 3 [0:360:10] '+' using (sin($1)):(cos($1)):(FILLSTROKE) \
         title "circle" fc bgnd
set mark 4 [0:360:72] '+' using (sin($1)):(cos($1)):(FILLSTROKE) \
         title "pentagon" fc bgnd
unset angle

plot -sin(x/4.) with linesmarks marktype 4 pointinterval 10, \
     -sin(x/3.) with linesmarks marktype 3 pointinterval  7, \
     -sin(x/2.) with linesmarks marktype 2 pointinterval  7, \
     -sin(x/1.) with linesmarks marktype 1 pointinterval  7

reset

#
# Marks example: scatterplots 
# ===========================
# fill color and border color controlled by the plot command
# (based on iris.dem)
#
array Square = [{-0.5,-0.5}, { 0.5,-0.5}, { 0.5, 0.5}, {-0.5, 0.5}, {-0.5,-0.5}]
$Triangle << EOD
0 0.5
0.433013 -0.25
-0.433013 -0.25
0 0.5
EOD
set mark 1 Square using 2:3
set mark 2 $Triangle using 1:2
set mark 3 $Triangle using 1:(-$2)

species(name) = strstrt(name,"setosa") ? 1 \
              : strstrt(name,"versicolor") ? 2 \
              : strstrt(name,"virginica") ? 3 \
              : 0

set datafile separator comma
set datafile columnhead
set table $setosa
  plot "iris.dat" using 2:3 with table if species(strcol(5)) == 1
set table $versicolor
  plot "iris.dat" using 2:3 with table if species(strcol(5)) == 2
set table $virginica
  plot "iris.dat" using 2:3 with table if species(strcol(5)) == 3
unset table

unset datafile columnhead
unset datafile separator

set border 3
set offset 0.5, 0.5, 0.5, 0.5
set xlabel "Sepal Width"
set ylabel "Petal Length"
set tics nomirror out
set size square
set key Left reverse rmargin top offset -5

set grid xtics ytics lt 1 lc '#e0e0e0'
set object rect from graph 0, 0 to graph 1, 1 fill solid fc "gray" behind

# First plot has marks with white border + color fill

set output out . 'figure_markscatter1' . ext
set title ""
set style fill solid border lc 'white'
plot $setosa     with marks mt 1 ps 1.5 title "setosa", \
     $versicolor with marks mt 2 ps 2   title "versicolor", \
     $virginica  with marks mt 3 ps 2   title "virginica"

# Second plot has marks with color border and gray fill

set output out . 'figure_markscatter2' . ext
set title ""
set style fill solid
plot $setosa with marks mt 1 ps 1.5 lw 2 fc "gray" \
	fs border lc '#1f77b4' notitle, \
     $versicolor with marks mt 2 ps 2 lw 2 fc "gray" \
	fs border lc '#ff7f0e' notitle, \
     $virginica with marks mt 3 ps 2 lw 2 fc "gray" \
	fs border lc '#2ca02c' notitle, \
     keyentry with marks mt 1 ps 1.5 lw 2 title "setosa" \
	fs border lc '#1f77b4' fc bgnd, \
     keyentry with marks mt 2 ps 2 lw 2 title "versicolor" \
	fs border lc '#ff7f0e' fc bgnd, \
     keyentry with marks mt 3 ps 2 lw 2 title "virginica" \
	fs border lc '#2ca02c' fc bgnd
reset

#
# Marks example: annotation
# ===========================
# Define a single horizontally-scalable mark to use as a bracket
# that forms part of the annotation labels placed above the 
# graph proper.
#
set output out . 'figure_marksannotate' . ext
set title "Adjustable width mark used to indicate grouping"
set title offset 0,2

$bar_data <<EOD
A        2       1
B        5       1
C        7       1
D        6       1
E        4       2
F        2       2
G        7       2
H        3       3
I        6       3
J        7       3
K        8       3
L        5       4
M        3       4
EOD

$group_stats << EOD
 "Group 1" 4 1
 "Group 2" 3 6
 "Group 3" 4 10
 "Group 4" 2 15
EOD

set bmargin screen 0.2

set autoscale xfix
set yrange [0:10]
set offsets gr 0.05, gr 0.05
unset key

set xtics nomirror
set ytics 0,1,8 nomirror rangelimited
set link y2
set border 3
array dummy[1]

# Description of a single mark consisting of a half-height horizontal square bracket
# with a centered vertical bar above it:
#
#                  |
#       |⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺|
#
# When plotted, the using specfier will provide x, y, width, height

$group_mark <<EOD
# x  y   1=stroke
  0 -0.5  1
  0  0    1
  1  0    1
  1 -0.5  1

0.5  0.0  1
0.5  0.5  1
EOD

set mark 1 $group_mark 

plot $bar_data using ($0+$3):2:(0.8):3:xtic(1) \
                 with boxes lc variable fill solid 0.3, \
     $group_stats using ($3-0.5):(9):2:(1) axes x1y2 \
                 with marks mt 1 units xy lt -1 lw 1, \
     $group_stats using ($3-0.5+$2/2):(9):1 axes x1y2 \
                 with labels center offset 0,1.7, \
     dummy using (1):(0) with points pt -1 # force y data range to [0:*]

unset mark 1
reset

#
# Marks example: parametric marks
# ===============================
#
set output out . 'figure_parametric_marks' . ext
set title "Mark shapes defined by parametric formulas"

set margin 0,0,1,3
unset border
unset tics
unset key

set angle degrees
set mark 1 [t=0:360:10]  "+" using (sin(t)):(cos(t)) ### circle
set mark 2 [t=0:360:120] "+" using (sin(t)):(cos(t)) ### triangle
set mark 3 [t=0:360:90]  "+" using (sin(t)):(cos(t)) ### diamond
set mark 4 [t=0:360:72]  "+" using (sin(t)):(cos(t)) ### pentagon
set mark 5 [t=0:360:60]  "+" using (sin(t)):(cos(t)) ### hexagon
set mark 6 [t=0:360:10]  "+" using (cos(t)):(0.8*(sqrt(abs(cos(t)))+sin(t))) ### heart
set mark 7 [t=0:360:10]  "+" using (r=0.5*abs(cos(3/2.0*t))+0.5, r*sin(t)):(r*cos(t)) ### 3 petals
set mark 8 [t=0:360:72/2] "+" using (r=0.4*(cos(5*t)+2), r*sin(t)):(r*cos(t)) ### star
set mark 9 [t=0:360:72/20] "+" using (r=0.18*(cos(5*t)+5), r*sin(t)):(r*cos(t)) ### 5 petals

array dummy[1]
S = 1.25         # scale factor

set xrange [0:10]
set yrange [0:12]
plot \
     for [k=1:9] dummy using (k):(11):(sprintf("%i",k)) with labels center,                 \
     for [k=1:9] dummy using (k):(9) with marks mt k ps S fill solid 0.5 border lc 'black', \
     for [k=1:9] dummy using (k):(7) with marks mt k ps S fill solid (1./k) fc 'black',     \
     for [k=1:9] dummy using (k):(5) with marks mt k ps S fill solid 0.0 border lc 'black', \
     for [k=1:9] dummy using (k):(3) with marks mt k ps S fill solid 1.0 noborder fc 'red', \
     for [k=1:9] dummy using (k):(1) with marks mt k ps S fill solid 1.0 border lc 'blue'

unset mark
reset

#
# Marks example: windbarbs
# ========================
#
set output out . 'figure_windbarbs' . ext
# set title "Rotation of marks in 5th 'using' column of plot command"
#
# Windbarbs representing 5, 25, and 50 knot wind speed
# scaled and rotated according to the 5-column form of the plot command
# The marks will be scaled to 7% of the plot width (scale=0.07 units gxx)
#     plot with marks using x:y:scale:scale:angle units gxx

$MARK_5 <<EOM
0	0	1
0	1	1

0	0.870	1
0.235	0.956	1
EOM

$MARK_25 <<EOM
0	0	1
0	1	1

0	1	1
0.470	1.171	1

0	0.870	1
0.470	1.041	1

0	0.740	1
0.235	0.826	1
EOM

$MARK_50 <<EOM
0	0	1
0	1.100	1

0	0.900	3
0.470	1.071	3
0	1.100	3
0	0.900	3
EOM

set mark  5 $MARK_5   title " 5 knots" fill solid
set mark 25 $MARK_25  title "25 knots" fill solid
set mark 50 $MARK_50  title "50 knots" fill solid

unset border; unset tics; set tmargin 0
set key horizontal top center reverse Left samplen 1 height 3
set xrange [0:10]
set yrange [-0.5:8]
SIZE = 0.07     # barb size will be 5% of graph width (units gxx)

plot [t=1:9:2] '+' using (t):(5):(SIZE):(SIZE):(36*t) notitle with marks mt 5 units gxx, \
     [t=1:9:2] '+' using (t):(3):(SIZE):(SIZE):(15*t) notitle with marks mt 25 units gxx, \
     [t=1:9:2] '+' using (t):(1):(SIZE):(SIZE):(25*t) notitle with marks mt 50 units gxx, \
     keyentry with marks mt  5 ps 4 lc "black" title "5 knots", \
     keyentry with marks mt 25 ps 4 lc "black" title "25 knots", \
     keyentry with marks mt 50 ps 4 lc "black" title "50 knots"
reset
unset mark

#
# Watchpoint example contour label placement
# ==========================================

set output out . 'figure_watch_contours' . ext

if (!strstrt(GPVAL_COMPILE_OPTIONS, "+WATCHPOINTS")) {
    clear
} else {
    sinc(x) = (x==0) ? 1.0 : sin(x) / x
    set linetype 5 lc "dark-blue"
    set view map scale 1.1
    set xrange [-1.6 : 2.5]
    set yrange [-0.5 : 2.2]
    set contour
    set cntrparam levels incr 0, .2, 4
    unset key

    set style watchpoint labels center nopoint font "Xerox Serif Narrow,10"
    set style textbox noborder opaque margins 0.5, 0.5
    line(a,b) = a*x+b - y
    a = 0.6
    b = 0.5
    set tics 1.0 scale 0 format "%.1f"
    set ytics offset 1

    splot 2.0 * sinc(sqrt(x*sin(x)+y*(y+sin(3.*x)))) with lines nosurface \
	  watch line(a,b)=0 label sprintf("%.1f",z)
}

reset

# =========================================
# close last file
# =========================================
unset output
reset

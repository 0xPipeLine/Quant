set terminal pngcairo size 800,600
set output 'test.png'
set title 'Test Gnuplot'
set xlabel 'X'
set ylabel 'Y'
plot 'test.dat' using 1:2 with linespoints lw 2 lc rgb 'blue' title 'y=x^2'

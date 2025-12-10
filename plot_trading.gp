set encoding utf8
set terminal pngcairo size 1400,1000 font 'Arial,12'
set output 'trading_performance.png'
set multiplot layout 2,2 title 'Analyse des Strategies de Trading' font ',16'

# Graphique 1: Prix vs Temps
set title 'Evolution du Prix'
set xlabel 'Temps (pas)'
set ylabel 'Prix'
set grid
plot 'trading_data.dat' using 1:2 with lines title 'Prix' lc rgb '#f39c12'

# Graphique 2: PnL des Strategies
set title 'Performance des Strategies (PnL )'
set xlabel 'Temps (pas)'
set ylabel 'PnL ()'
set grid
plot 'trading_data.dat' using 1:4 with lines title 'Simple MM' lc rgb '#3498db', \
     'trading_data.dat' using 1:5 with lines title 'Avellaneda' lc rgb '#e74c3c', \
     'trading_data.dat' using 1:6 with lines title 'Envelope' lc rgb '#2ecc71'

# Graphique 3: Comparaison Prix vs Strategies
set title 'Prix vs Strategies ()'
set xlabel 'Temps (pas)'
set ylabel 'Performance ()'
set grid
plot 'trading_data.dat' using 1:3 with lines title 'Prix ()' lc rgb '#f39c12', \
     'trading_data.dat' using 1:4 with lines title 'Simple MM' lc rgb '#3498db', \
     'trading_data.dat' using 1:5 with lines title 'Avellaneda' lc rgb '#e74c3c', \
     'trading_data.dat' using 1:6 with lines title 'Envelope' lc rgb '#2ecc71'

# Graphique 4: Drawdown Analysis
set title 'Analyse des Drawdowns'
set xlabel 'Temps (pas)'
set ylabel 'PnL Cumule ()'
set grid
plot 'trading_data.dat' using 1:4 with filledcurves y1=0 fillstyle solid 0.3 lc rgb '#3498db' title 'Simple MM', \
     'trading_data.dat' using 1:5 with filledcurves y1=0 fillstyle solid 0.3 lc rgb '#e74c3c' title 'Avellaneda', \
     'trading_data.dat' using 1:6 with filledcurves y1=0 fillstyle solid 0.3 lc rgb '#2ecc71' title 'Envelope'

unset multiplot

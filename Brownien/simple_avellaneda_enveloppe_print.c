#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <string.h>

#define N 5000  // Réduire pour des graphiques plus lisibles
#define MAX_ORDERS 300000

typedef struct {
    bool is_buy;
    float size;
    int id;
    float price;
    bool active;
} Order;

typedef struct {
    Order *orders;
    int max_orders;
    int orders_count;
    int *free_ids;
    int free_count;
} OrderBook;

typedef struct {
    double capital;
    double usd;
    double coin;
    double busd;
    double bcoin;
    OrderBook orderbook;
    double fees_limit;
    double fees_market;
    double min_order;
    double volume;
    double *pnl_history;
    int pnl_count;
    int max_pnl_entries;
} Portfolio;

typedef struct {
    int n;
    double dt;
    double S0;
    double volatility;
    double min_drift;
    double max_drift;
    double jump_min;
    double jump_max;
    double jump_prob;
    double mu;
    double *returns;
    double *jumps;
    double *total_returns;
    double *prices;
} Market;

typedef struct {
    Portfolio portfolio_simple;
    Portfolio portfolio_avellaneda;
    Portfolio portfolio_envelope;
    Market market;
} Tester;

/* utilitaires aleatoires */
static double rand_uniform(double a, double b){
    return a + (b - a) * ((double) rand() / (double) RAND_MAX);
}
static double rand_normal(){
    /* Box-Muller */
    static bool has_next = false;
    static double next_value = 0.0;
    
    if (has_next) {
        has_next = false;
        return next_value;
    }
    
    double u1 = ((double) rand() + 1.0) / ((double) RAND_MAX + 1.0);
    double u2 = ((double) rand() + 1.0) / ((double) RAND_MAX + 1.0);
    double z0 = sqrt(-2.0*log(u1)) * cos(2.0*M_PI*u2);
    next_value = sqrt(-2.0*log(u1)) * sin(2.0*M_PI*u2);
    has_next = true;
    return z0;
}

/* MARKET */
int market_init(Market *m, int n){
    if (m == NULL || n <= 0) return -1;
    
    m->n = n;
    m->dt = 1.0/(365.0*24.0*60.0);
    m->S0 = 180.0;
    m->volatility = 0.14;
    m->min_drift = -0.11;
    m->max_drift = 0.11;
    m->jump_min = 0.005;
    m->jump_max = 0.01;
    m->jump_prob = 0.0000006;
    m->mu = rand_uniform(m->min_drift, m->max_drift);

    m->returns = (double*) calloc(n, sizeof(double));
    m->jumps = (double*) calloc(n, sizeof(double));
    m->total_returns = (double*) calloc(n, sizeof(double));
    m->prices = (double*) calloc(n, sizeof(double));
    
    if(!m->returns || !m->jumps || !m->total_returns || !m->prices) {
        market_free(m);
        return -1;
    }

    double cumulative = 0.0;
    for(int i = 0; i < n; ++i){
        m->returns[i] = (m->mu - 0.5 * m->volatility * m->volatility) * m->dt
                        + m->volatility * sqrt(m->dt) * rand_normal();
        
        m->jumps[i] = 0.0;
        if(rand_uniform(0.0, 1.0) < m->jump_prob){
            m->jumps[i] = rand_uniform(-m->jump_max, m->jump_max);
        }
        
        m->total_returns[i] = m->returns[i] + m->jumps[i];
        cumulative += m->total_returns[i];
        m->prices[i] = m->S0 * exp(cumulative);
        
        // Safeguard pour les prix invalides
        if(!isfinite(m->prices[i]) || m->prices[i] <= 0) {
            m->prices[i] = (i > 0) ? m->prices[i-1] : m->S0;
        }
    }
    return 0;
}

void market_free(Market *m) {
    if (m == NULL) return;
    free(m->returns);
    free(m->jumps);
    free(m->total_returns);
    free(m->prices);
    m->returns = NULL;
    m->jumps = NULL;
    m->total_returns = NULL;
    m->prices = NULL;
}

/* ORDERBOOK */
int orderbook_init(OrderBook *ob, int max_orders){
    if (ob == NULL || max_orders <= 0) return -1;
    
    ob->orders = (Order*) calloc(max_orders, sizeof(Order));
    ob->free_ids = (int*) calloc(max_orders, sizeof(int));
    
    if(!ob->orders || !ob->free_ids) {
        orderbook_free(ob);
        return -1;
    }
    
    ob->max_orders = max_orders;
    ob->orders_count = 0;
    ob->free_count = 0;
    
    // Initialiser tous les ordres comme inactifs
    for(int i = 0; i < max_orders; i++) {
        ob->orders[i].active = false;
        ob->orders[i].id = i;
    }
    return 0;
}

void orderbook_free(OrderBook *ob) {
    if (ob == NULL) return;
    free(ob->orders);
    free(ob->free_ids);
    ob->orders = NULL;
    ob->free_ids = NULL;
}

int orderbook_order(OrderBook *ob, bool is_buy, float size, float price){
    if (ob == NULL || size <= 0 || price <= 0) return -1;
    
    int idx;
    if(ob->free_count > 0){
        idx = ob->free_ids[--ob->free_count];
    } else if(ob->orders_count < ob->max_orders){
        idx = ob->orders_count++;
    } else {
        // Carnet saturé -> ne crée rien
        return -1;
    }
    
    ob->orders[idx].is_buy = is_buy;
    ob->orders[idx].size = size;
    ob->orders[idx].id = idx;
    ob->orders[idx].price = price;
    ob->orders[idx].active = true;
    return idx;
}

void orderbook_bulk_remove(OrderBook *ob, int *ids, int n_ids){
    if (ob == NULL || ids == NULL) return;
    
    for(int i = 0; i < n_ids; i++){
        int id = ids[i];
        if(id >= 0 && id < ob->max_orders && ob->orders[id].active){
            ob->orders[id].active = false;
            if(ob->free_count < ob->max_orders) {
                ob->free_ids[ob->free_count++] = id;
            }
        }
    }
}

/* PORTFOLIO */
int portfolio_init(Portfolio *p, double capital, int max_orders){
    if (p == NULL || capital <= 0 || max_orders <= 0) return -1;
    
    p->capital = capital;
    p->usd = capital;
    p->coin = 0.0;
    p->busd = 0.0;
    p->bcoin = 0.0;
    p->fees_limit = 0.0004;
    p->fees_market = 0.0007;
    p->min_order = 10.0;
    p->volume = 0.0;
    
    // Initialisation tracking PnL
    p->max_pnl_entries = N; // Maximum N entrées
    p->pnl_history = (double*) calloc(p->max_pnl_entries, sizeof(double));
    if(!p->pnl_history) return -1;
    p->pnl_count = 0;
    
    if(orderbook_init(&p->orderbook, max_orders) != 0) {
        free(p->pnl_history);
        return -1;
    }
    return 0;
}

void portfolio_free(Portfolio *p) {
    if (p == NULL) return;
    orderbook_free(&p->orderbook);
    free(p->pnl_history);
    p->pnl_history = NULL;
}

void portfolio_update_pnl(Portfolio *p, double current_price) {
    if (p == NULL || current_price <= 0) return;
    
    if(p->pnl_count < p->max_pnl_entries) {
        double current_value = p->usd + (p->coin * current_price);
        double pnl = ((current_value - p->capital) / p->capital) * 100.0; // PnL en %
        p->pnl_history[p->pnl_count] = pnl;
        p->pnl_count++;
    }
}

int portfolio_limit_order(Portfolio *p, bool is_buy, double size, double price){
    if (p == NULL || size <= 0 || price <= 0) return -1;
    
    double size_usd = size * price;
    if(size_usd < p->min_order) return -1;
    
    if(is_buy){
        if(size_usd <= (p->usd - p->busd)){
            int id = orderbook_order(&p->orderbook, is_buy, (float)size, (float)price);
            if(id != -1) p->busd += size_usd;
            return id;
        }
    } else {
        if(size <= (p->coin - p->bcoin)){
            int id = orderbook_order(&p->orderbook, is_buy, (float)size, (float)price);
            if(id != -1) p->bcoin += size;
            return id;
        }
    }
    return -1;
}

void portfolio_fill(Portfolio *p, int order_id){
    if (p == NULL || order_id < 0 || order_id >= p->orderbook.max_orders) return;
    
    Order *order = &p->orderbook.orders[order_id];
    if(!order->active) return;
    
    order->active = false;
    if(p->orderbook.free_count < p->orderbook.max_orders) {
        p->orderbook.free_ids[p->orderbook.free_count++] = order_id;
    }
    
    double size_usd = (double)order->size * (double)order->price;
    double fee = size_usd * p->fees_limit;
    
    if(order->is_buy){
        p->usd -= size_usd + fee;
        p->coin += order->size;
        p->busd -= size_usd;
    } else {
        p->usd += size_usd - fee;
        p->coin -= order->size;
        p->bcoin -= order->size;
    }
    p->volume += size_usd;
}

double portfolio_value(Portfolio *p, double price){
    if (p == NULL || price <= 0) return 0.0;
    return p->usd + (p->coin * price);
}

/* TESTER FUNCTIONS */
int tester_init(Tester *t, double capital, int n_steps){
    if (t == NULL || capital <= 0 || n_steps <= 0) return -1;
    
    // Initialiser les portfolios
    if(portfolio_init(&t->portfolio_simple, capital, MAX_ORDERS) != 0) return -1;
    if(portfolio_init(&t->portfolio_avellaneda, capital, MAX_ORDERS) != 0) {
        portfolio_free(&t->portfolio_simple);
        return -1;
    }
    if(portfolio_init(&t->portfolio_envelope, capital, MAX_ORDERS) != 0) {
        portfolio_free(&t->portfolio_simple);
        portfolio_free(&t->portfolio_avellaneda);
        return -1;
    }
    
    // Initialiser le marché
    if(market_init(&t->market, n_steps) != 0) {
        portfolio_free(&t->portfolio_simple);
        portfolio_free(&t->portfolio_avellaneda);
        portfolio_free(&t->portfolio_envelope);
        return -1;
    }
    
    return 0;
}

void tester_free(Tester *t) {
    if (t == NULL) return;
    portfolio_free(&t->portfolio_simple);
    portfolio_free(&t->portfolio_avellaneda);
    portfolio_free(&t->portfolio_envelope);
    market_free(&t->market);
}

void Tester_fill_clear(Portfolio *p, double min, double max, double spread_max){
    if (p == NULL || min <= 0 || max <= 0) return;
    
    double mid = (min + max)/2.0;
    double limit_min = mid * (1.0 - (spread_max/2.0));
    double limit_max = mid * (1.0 + (spread_max/2.0));
    int to_cancel[10000];
    int cancel_count = 0;

    for(int i = 0; i < p->orderbook.orders_count && cancel_count < 10000; i++){
        Order *o = &p->orderbook.orders[i];
        if(o->active){
            if(o->is_buy){
                if(o->price >= min){
                    portfolio_fill(p, o->id);
                } else if(o->price < limit_min){
                    to_cancel[cancel_count++] = o->id;
                }
            } else {
                if(o->price <= max){
                    portfolio_fill(p, o->id);
                } else if(o->price > limit_max){
                    to_cancel[cancel_count++] = o->id;
                }
            }
        }
    }
    if(cancel_count > 0) {
        orderbook_bulk_remove(&p->orderbook, to_cancel, cancel_count);
    }
}

void simple_mm(Market *m, Portfolio *p, double spread){
    if (m == NULL || p == NULL || spread <= 0) return;
    
    for(int i = 0; i < m->n; i++){
        double price = m->prices[i];
        if (price <= 0) continue;
        
        Tester_fill_clear(p, price, price, spread*4);
        portfolio_limit_order(p, false, 0.1, price * (1.0 + spread/2.0)); // sell
        portfolio_limit_order(p, true, 0.1, price * (1.0 - spread/2.0)); // buy
        
        // Mettre à jour le PnL
        portfolio_update_pnl(p, price);
    }
}

void avellaneda(Market *m, Portfolio *p, double gamma, double kappa){
    if (m == NULL || p == NULL || gamma <= 0 || kappa <= 0) return;
    
    double sigma = m->volatility;
    double total_time = m->n * m->dt;
    
    for(int i = 0; i < m->n; i++){
        double mid = m->prices[i];
        if (mid <= 0) continue;
        
        double q = p->coin;
        double T = total_time - i * m->dt; // Temps restant

        if(T <= 0) T = m->dt; // Éviter division par zéro

        double s = gamma * sigma * sigma * T + (2.0/gamma) * log(1.0 + gamma/kappa);
        double r = mid - q * gamma * sigma * sigma * T;

        double bid = r - s/2.0;
        double ask = r + s/2.0;
        
        // Assurer que les prix sont positifs
        if (bid <= 0) bid = mid * 0.99;
        if (ask <= 0) ask = mid * 1.01;

        Tester_fill_clear(p, mid, mid, s*2.0);
        portfolio_limit_order(p, true, 0.1, bid);   // buy
        portfolio_limit_order(p, false, 0.1, ask);  // sell
        
        // Mettre à jour le PnL
        portfolio_update_pnl(p, mid);
    }
}

// Calcul de la SMA (Simple Moving Average)
double calculate_sma(double *prices, int current_idx, int period) {
    if (prices == NULL || current_idx < 0 || period <= 0) return 0.0;
    if (current_idx < period - 1) return prices[current_idx];
    
    double sum = 0.0;
    for (int i = current_idx - period + 1; i <= current_idx; i++) {
        sum += prices[i];
    }
    return sum / period;
}

void envelope_strategy(Market *m, Portfolio *p, int levels, double min_spread, double max_spread, int sma_period) {
    if (m == NULL || p == NULL || levels < 2 || min_spread <= 0 || max_spread <= 0 || sma_period <= 0) return;
    
    for (int i = 0; i < m->n; i++) {
        double current_price = m->prices[i];
        if (current_price <= 0) continue;
        
        double sma = calculate_sma(m->prices, i, sma_period);
        if (sma <= 0) continue;
        
        // Nettoyer les anciens ordres
        Tester_fill_clear(p, current_price, current_price, max_spread * 2.0);
        
        // Calculer les écarts entre niveaux
        double spread_increment = (max_spread - min_spread) / (levels - 1);
        
        int orders_placed = 0;
        
        // Placer les ordres buy et sell à chaque niveau
        for (int level = 0; level < levels; level++) {
            double spread = min_spread + level * spread_increment;
            
            double buy_price = sma * (1.0 - spread / 2.0);
            double sell_price = sma * (1.0 + spread / 2.0);
            
            // Assurer que les prix sont positifs
            if (buy_price <= 0) buy_price = current_price * 0.99;
            if (sell_price <= 0) sell_price = current_price * 1.01;
            
            // Ne placer l'ordre buy que si le prix actuel est au-dessus
            if (current_price > buy_price) {
                if (portfolio_limit_order(p, true, 0.1, buy_price) != -1) {
                    orders_placed++;
                }
            }
            
            // Ne placer l'ordre sell que si le prix actuel est en-dessous
            if (current_price < sell_price) {
                if (portfolio_limit_order(p, false, 0.1, sell_price) != -1) {
                    orders_placed++;
                }
            }
        }
        
        // S'assurer qu'au moins 4 ordres sont placés si possible
        if (orders_placed < 4 && levels >= 2) {
            // Ajuster légèrement les prix pour forcer plus d'ordres
            double emergency_spread = min_spread * 0.5;
            
            double emergency_buy = sma * (1.0 - emergency_spread / 2.0);
            double emergency_sell = sma * (1.0 + emergency_spread / 2.0);
            
            if (emergency_buy <= 0) emergency_buy = current_price * 0.99;
            if (emergency_sell <= 0) emergency_sell = current_price * 1.01;
            
            if (current_price > emergency_buy && orders_placed < 4) {
                portfolio_limit_order(p, true, 0.05, emergency_buy);
                orders_placed++;
            }
            
            if (current_price < emergency_sell && orders_placed < 4) {
                portfolio_limit_order(p, false, 0.05, emergency_sell);
                orders_placed++;
            }
        }
        
        // Mettre à jour le PnL
        portfolio_update_pnl(p, current_price);
    }
}

// Fonction pour calculer les statistiques de performance
void calculate_performance_stats(Portfolio *p, const char* strategy_name) {
    if (p == NULL || strategy_name == NULL || p->pnl_count == 0) return;
    
    double max_pnl = p->pnl_history[0];
    double min_pnl = p->pnl_history[0];
    double total_return = p->pnl_history[p->pnl_count - 1];
    
    // Calcul du max drawdown
    double peak = p->pnl_history[0];
    double max_drawdown = 0.0;
    
    for (int i = 0; i < p->pnl_count; i++) {
        if (p->pnl_history[i] > peak) {
            peak = p->pnl_history[i];
        }
        double drawdown = peak - p->pnl_history[i];
        if (drawdown > max_drawdown) {
            max_drawdown = drawdown;
        }
        
        if (p->pnl_history[i] > max_pnl) max_pnl = p->pnl_history[i];
        if (p->pnl_history[i] < min_pnl) min_pnl = p->pnl_history[i];
    }
    
    // Calcul de la volatilité (écart-type des rendements)
    double sum = 0.0;
    for (int i = 0; i < p->pnl_count; i++) {
        sum += p->pnl_history[i];
    }
    double mean = sum / p->pnl_count;
    
    double variance = 0.0;
    for (int i = 0; i < p->pnl_count; i++) {
        variance += (p->pnl_history[i] - mean) * (p->pnl_history[i] - mean);
    }
    double volatility = sqrt(variance / p->pnl_count);
    
    // Ratio de Sharpe approximatif (supposant un taux sans risque de 0)
    double sharpe_ratio = (volatility > 0) ? mean / volatility : 0.0;
    
    printf("\n=== STATISTIQUES %s ===\n", strategy_name);
    printf("Rendement total: %.2f%%\n", total_return);
    printf("PnL maximum: %.2f%%\n", max_pnl);
    printf("PnL minimum: %.2f%%\n", min_pnl);
    printf("Max Drawdown: %.2f%%\n", max_drawdown);
    printf("Volatilité: %.2f%%\n", volatility);
    printf("Ratio de Sharpe: %.2f\n", sharpe_ratio);
    printf("Volume total: %.2f\n", p->volume);
}

// Sauvegarde des données et génération des graphiques
void generate_plots(Market *m, Portfolio *p_simple, Portfolio *p_avellaneda, Portfolio *p_envelope) {
    if (m == NULL || p_simple == NULL || p_avellaneda == NULL || p_envelope == NULL) return;
    
    // Sauvegarder les données
    FILE *data_file = fopen("trading_data.dat", "w");
    if (!data_file) {
        printf("Erreur: Impossible de créer trading_data.dat\n");
        return;
    }
    
    fprintf(data_file, "# Step Price Price_Change_Pct Simple_PnL Avellaneda_PnL Envelope_PnL\n");
    
    double initial_price = m->S0;
    int max_entries = (p_simple->pnl_count < p_avellaneda->pnl_count) ? p_simple->pnl_count : p_avellaneda->pnl_count;
    max_entries = (max_entries < p_envelope->pnl_count) ? max_entries : p_envelope->pnl_count;
    
    for (int i = 0; i < max_entries; i++) {
        double price_change_pct = ((m->prices[i] - initial_price) / initial_price) * 100.0;
        fprintf(data_file, "%d %.6f %.2f %.2f %.2f %.2f\n",
                i, m->prices[i], price_change_pct,
                p_simple->pnl_history[i],
                p_avellaneda->pnl_history[i],
                p_envelope->pnl_history[i]);
    }
    fclose(data_file);
    
    // Créer le script gnuplot
    // Créer le script gnuplot
    FILE *gnuplot_script = fopen("plot_trading.gp", "w");
    if (!gnuplot_script) {
        printf("Erreur: Impossible de créer plot_trading.gp\n");
        return;
    }

    fprintf(gnuplot_script, 
        "set encoding utf8\n"
        "set terminal pngcairo size 1400,1000 font 'Arial,12'\n"
        "set output 'trading_performance.png'\n"
        "set multiplot layout 2,2 title 'Analyse des Strategies de Trading' font ',16'\n\n"
        
        "# Graphique 1: Prix vs Temps\n"
        "set title 'Evolution du Prix'\n"
        "set xlabel 'Temps (pas)'\n"
        "set ylabel 'Prix'\n"
        "set grid\n"
        "plot 'trading_data.dat' using 1:2 with lines title 'Prix' lc rgb '#f39c12'\n\n"
        
        "# Graphique 2: PnL des Strategies\n"
        "set title 'Performance des Strategies (PnL %)'\n"
        "set xlabel 'Temps (pas)'\n"
        "set ylabel 'PnL (%)'\n"
        "set grid\n"
        "plot 'trading_data.dat' using 1:4 with lines title 'Simple MM' lc rgb '#3498db', \\\n"
        "     'trading_data.dat' using 1:5 with lines title 'Avellaneda' lc rgb '#e74c3c', \\\n"
        "     'trading_data.dat' using 1:6 with lines title 'Envelope' lc rgb '#2ecc71'\n\n"
        
        "# Graphique 3: Comparaison Prix vs Strategies\n"
        "set title 'Prix vs Strategies (%)'\n"
        "set xlabel 'Temps (pas)'\n"
        "set ylabel 'Performance (%)'\n"
        "set grid\n"
        "plot 'trading_data.dat' using 1:3 with lines title 'Prix (%)' lc rgb '#f39c12', \\\n"
        "     'trading_data.dat' using 1:4 with lines title 'Simple MM' lc rgb '#3498db', \\\n"
        "     'trading_data.dat' using 1:5 with lines title 'Avellaneda' lc rgb '#e74c3c', \\\n"
        "     'trading_data.dat' using 1:6 with lines title 'Envelope' lc rgb '#2ecc71'\n\n"
        
        "# Graphique 4: Drawdown Analysis\n"
        "set title 'Analyse des Drawdowns'\n"
        "set xlabel 'Temps (pas)'\n"
        "set ylabel 'PnL Cumule (%)'\n"
        "set grid\n"
        "plot 'trading_data.dat' using 1:4 with filledcurves y1=0 fillstyle solid 0.3 lc rgb '#3498db' title 'Simple MM', \\\n"
        "     'trading_data.dat' using 1:5 with filledcurves y1=0 fillstyle solid 0.3 lc rgb '#e74c3c' title 'Avellaneda', \\\n"
        "     'trading_data.dat' using 1:6 with filledcurves y1=0 fillstyle solid 0.3 lc rgb '#2ecc71' title 'Envelope'\n\n"
        
        "unset multiplot\n"
    );
    fclose(gnuplot_script);

    
    // Exécuter gnuplot
    printf("\n📊 Génération des graphiques...\n");
    int result = system("gnuplot plot_trading.gp");
    
    if (result == 0) {
        printf("✅ Graphiques générés avec succès!\n");
        printf("📁 Fichiers créés:\n");
        printf("   - trading_data.dat (données)\n");
        printf("   - plot_trading.gp (script gnuplot)\n");
        printf("   - trading_performance.png (graphiques)\n");
        
        #ifdef _WIN32
            system("start trading_performance.png");
        #elif __APPLE__
            system("open trading_performance.png");
        #else
            system("xdg-open trading_performance.png");
        #endif
    } else {
        printf("❌ Erreur lors de la génération des graphiques.\n");
        printf("💡 Assurez-vous que gnuplot est installé:\n");
        printf("   - Ubuntu/Debian: sudo apt install gnuplot\n");
        printf("   - MacOS: brew install gnuplot\n");
        printf("   - Windows: télécharger depuis http://www.gnuplot.info/\n");
    }
}

// Fonction utilitaire pour afficher une ligne de séparation
void print_separator(int length) {
    for(int i = 0; i < length; i++) {
        printf("=");
    }
    printf("\n");
}

int main(){
    srand((unsigned int)time(NULL)); // Initialiser le générateur aléatoire
    
    Tester t;
    if(tester_init(&t, 10000.0, N) != 0) {
        printf("Erreur d'initialisation\n");
        return -1;
    }

    printf("🚀 Démarrage de la simulation de trading...\n");
    printf("📊 Nombre de pas de temps: %d\n", t.market.n);
    printf("💰 Capital initial: %.0f\n", t.portfolio_simple.capital);
    printf("📈 Prix initial: %.2f\n", t.market.S0);
    printf("📉 Volatilité: %.1f%%\n", t.market.volatility * 100);
    
    printf("\n⏳ Exécution des stratégies...\n");
    simple_mm(&t.market, &t.portfolio_simple, 0.002);
    printf("✅ Simple Market Making terminé\n");
    
    avellaneda(&t.market, &t.portfolio_avellaneda, 0.1, 1.5);
    printf("✅ Avellaneda-Stoikov terminé\n");
    
    envelope_strategy(&t.market, &t.portfolio_envelope, 4, 0.001, 0.005, 20);
    printf("✅ Envelope Strategy terminé\n");

    double final_price = t.market.prices[t.market.n-1];
    double price_change_pct = ((final_price - t.market.S0) / t.market.S0) * 100.0;
    
    double final_simple = portfolio_value(&t.portfolio_simple, final_price);
    double final_avellaneda = portfolio_value(&t.portfolio_avellaneda, final_price);
    double final_envelope = portfolio_value(&t.portfolio_envelope, final_price);

    printf("\n");
    print_separator(60);
    printf("🎯 RÉSULTATS FINAUX\n");
    print_separator(60);
    printf("📈 Prix final: %.2f (variation: %.2f%%)\n", final_price, price_change_pct);
    printf("💼 Simple MM: %.2f (PnL: %.2f%%)\n", final_simple, 
           ((final_simple - t.portfolio_simple.capital) / t.portfolio_simple.capital) * 100);
    printf("🎯 Avellaneda: %.2f (PnL: %.2f%%)\n", final_avellaneda,
           ((final_avellaneda - t.portfolio_avellaneda.capital) / t.portfolio_avellaneda.capital) * 100);
    printf("📊 Envelope: %.2f (PnL: %.2f%%)\n", final_envelope,
           ((final_envelope - t.portfolio_envelope.capital) / t.portfolio_envelope.capital) * 100);
    
    // Calcul des statistiques détaillées
    calculate_performance_stats(&t.portfolio_simple, "SIMPLE MM");
    calculate_performance_stats(&t.portfolio_avellaneda, "AVELLANEDA");
    calculate_performance_stats(&t.portfolio_envelope, "ENVELOPE");
    
    printf("\n");
    print_separator(60);
    printf("📊 COMPARAISON VS BUY & HOLD\n");
    print_separator(60);
    printf("🏪 Buy & Hold: %.2f%%\n", price_change_pct);
    
    if(t.portfolio_simple.pnl_count > 0) {
        printf("💼 Simple MM: %.2f%% ", t.portfolio_simple.pnl_history[t.portfolio_simple.pnl_count-1]);
        printf("(%s%.2f%%)\n", 
               t.portfolio_simple.pnl_history[t.portfolio_simple.pnl_count-1] > price_change_pct ? "+" : "",
               t.portfolio_simple.pnl_history[t.portfolio_simple.pnl_count-1] - price_change_pct);
    }
    
    if(t.portfolio_avellaneda.pnl_count > 0) {
        printf("🎯 Avellaneda: %.2f%% ", t.portfolio_avellaneda.pnl_history[t.portfolio_avellaneda.pnl_count-1]);
        printf("(%s%.2f%%)\n",
               t.portfolio_avellaneda.pnl_history[t.portfolio_avellaneda.pnl_count-1] > price_change_pct ? "+" : "",
               t.portfolio_avellaneda.pnl_history[t.portfolio_avellaneda.pnl_count-1] - price_change_pct);
    }
    
    if(t.portfolio_envelope.pnl_count > 0) {
        printf("📊 Envelope: %.2f%% ", t.portfolio_envelope.pnl_history[t.portfolio_envelope.pnl_count-1]);
        printf("(%s%.2f%%)\n",
               t.portfolio_envelope.pnl_history[t.portfolio_envelope.pnl_count-1] > price_change_pct ? "+" : "",
               t.portfolio_envelope.pnl_history[t.portfolio_envelope.pnl_count-1] - price_change_pct);
    }
    
    // Génération des graphiques
    generate_plots(&t.market, &t.portfolio_simple, &t.portfolio_avellaneda, &t.portfolio_envelope);
    
    tester_free(&t);
    return 0;
}
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>

#define N 120960
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
    Market market;
} Tester;

/* utilitaires aleatoires */
static double rand_uniform(double a, double b){
    return a + (b - a) * ((double) rand() / (double) RAND_MAX);
}
static double rand_normal(){
    /* Box-Muller */
    double u1 = ((double) rand() + 1.0) / ((double) RAND_MAX + 1.0);
    double u2 = ((double) rand() + 1.0) / ((double) RAND_MAX + 1.0);
    return sqrt(-2.0*log(u1)) * cos(2.0*M_PI*u2);
}

/* MARKET */
int market_init(Market *m, int n){
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

    m->returns = (double*) malloc(sizeof(double) * n);
    m->jumps = (double*) malloc(sizeof(double) * n);
    m->total_returns = (double*) malloc(sizeof(double) * n);
    m->prices = (double*) malloc(sizeof(double) * n);
    if(!m->returns || !m->jumps || !m->total_returns || !m->prices) return -1;

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
        if(!isfinite(m->prices[i])) m->prices[i] = INFINITY; /* safeguard */
    }
    return 0;
}

void market_free(Market *m) {
    free(m->returns);
    free(m->jumps);
    free(m->total_returns);
    free(m->prices);
}

/* ORDERBOOK */
int orderbook_init(OrderBook *ob, int max_orders){
    ob->orders = (Order*) malloc(sizeof(Order) * max_orders);
    ob->free_ids = (int*) malloc(sizeof(int) * max_orders);
    if(!ob->orders || !ob->free_ids) return -1;
    ob->max_orders = max_orders;
    ob->orders_count = 0;
    ob->free_count = 0;
    /* optional: init active false */
    for(int i=0;i<max_orders;i++) ob->orders[i].active = false;
    return 0;
}

void orderbook_free(OrderBook *ob) {
    free(ob->orders);
    free(ob->free_ids);
}

int orderbook_order(OrderBook *ob, bool is_buy, float size, float price){
    int idx;
    if(ob->free_count > 0){
        idx = ob->free_ids[--ob->free_count];
    } else if(ob->orders_count < ob->max_orders){
        idx = ob->orders_count++;
    } else {
        /* carnet saturé -> ne crée rien */
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
    for(int i=0;i<n_ids;i++){
        int id = ids[i];
        if(id >= 0 && id < ob->max_orders && ob->orders[id].active){
            ob->orders[id].active = false;
            ob->free_ids[ob->free_count++] = id;
        }
    }
}

/* PORTFOLIO */
int portfolio_init(Portfolio *p, double capital, int max_orders){
    p->capital = capital;
    p->usd = capital;
    p->coin = 0.0;
    p->busd = 0.0;
    p->bcoin = 0.0;
    p->fees_limit = 0.0004;
    p->fees_market = 0.0007;
    p->min_order = 10.0;
    p->volume = 0.0;
    if(orderbook_init(&p->orderbook, max_orders) != 0) return -1;
    return 0;
}

void portfolio_free(Portfolio *p) {
    orderbook_free(&p->orderbook);
}

int portfolio_limit_order(Portfolio *p, bool is_buy, double size, double price){
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
    if(order_id < 0 || order_id >= p->orderbook.max_orders) return;
    Order *order = &p->orderbook.orders[order_id];
    if(!order->active) return;
    order->active = false;
    p->orderbook.free_ids[p->orderbook.free_count++] = order_id;
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
    return p->usd + (p->coin * price);
}

/* TESTER FUNCTIONS */
int tester_init(Tester *t, double capital, int n_steps){
    // Initialiser les portfolios
    if(portfolio_init(&t->portfolio_simple, capital, MAX_ORDERS) != 0) return -1;
    if(portfolio_init(&t->portfolio_avellaneda, capital, MAX_ORDERS) != 0) return -1;
    
    // Initialiser le marché
    if(market_init(&t->market, n_steps) != 0) return -1;
    
    return 0;
}

void tester_free(Tester *t) {
    portfolio_free(&t->portfolio_simple);
    portfolio_free(&t->portfolio_avellaneda);
    market_free(&t->market);
}

void Tester_fill_clear(Portfolio *p, double min, double max, double spread_max){
    double mid = (min + max)/2.0;
    double limit_min = mid * (1.0 - (spread_max/2.0));
    double limit_max = mid * (1.0 + (spread_max/2.0));
    int to_cancel[10000];
    int cancel_count = 0;

    for(int i=0; i<p->orderbook.orders_count; i++){
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
    orderbook_bulk_remove(&p->orderbook, to_cancel, cancel_count);
}

void simple_mm(Market *m, Portfolio *p, double spread){
    for(int i=0; i<m->n; i++){
        double price = m->prices[i];
        Tester_fill_clear(p, price, price, spread*4);
        portfolio_limit_order(p, false, 0.1, price * (1.0 + spread/2.0)); // sell
        portfolio_limit_order(p, true, 0.1, price * (1.0 - spread/2.0)); // buy
    }
}

void avellaneda(Market *m, Portfolio *p, double gamma, double kappa){
    double sigma = m->volatility;
    double total_time = m->n * m->dt;
    
    for(int i=0; i<m->n; i++){
        double mid = m->prices[i];
        double q = p->coin;
        double T = total_time - i * m->dt; // Temps restant

        if(T <= 0) T = m->dt; // Éviter division par zéro

        double s = gamma * sigma * sigma * T + (2.0/gamma) * log(1.0 + gamma/kappa);
        double r = mid - q * gamma * sigma * sigma * T;

        double bid = r - s/2.0;
        double ask = r + s/2.0;

        Tester_fill_clear(p, mid, mid, s*2.0);
        portfolio_limit_order(p, true, 0.1, bid);   // buy
        portfolio_limit_order(p, false, 0.1, ask);  // sell
    }
}

int main(){
    srand(time(NULL)); // Initialiser le générateur aléatoire
    
    Tester t;
    if(tester_init(&t, 10000.0, N) != 0) {
        printf("Erreur d'initialisation\n");
        return -1;
    }

    simple_mm(&t.market, &t.portfolio_simple, 0.002);
    avellaneda(&t.market, &t.portfolio_avellaneda, 0.1, 1.5);

    double final_simple = portfolio_value(&t.portfolio_simple, t.market.prices[t.market.n-1]);
    double final_avellaneda = portfolio_value(&t.portfolio_avellaneda, t.market.prices[t.market.n-1]);

    printf("Simple MM: %.2f (volume %.2f)\n", final_simple, t.portfolio_simple.volume);
    printf("Avellaneda: %.2f (volume %.2f)\n", final_avellaneda, t.portfolio_avellaneda.volume);
    
    tester_free(&t);
    return 0;
}
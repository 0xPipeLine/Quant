#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <windows.h>
#include <limits.h>
#include <string.h>

#define N 120960
#define MAX_ORDERS 300000
#define NUM_THREADS 100
#define MAX_STRATEGIES 10
#define MAX_STRATEGY_NAME 50

/* --- STRUCTS --- */
typedef struct { bool is_buy; float size; int id; float price; bool active; } Order;
typedef struct { Order *orders; int max_orders, orders_count, *free_ids, free_count; } OrderBook;
typedef struct { double capital, usd, coin, busd, bcoin, fees_limit, fees_market, min_order, volume; OrderBook orderbook; } Portfolio;
typedef struct { int n; double dt, S0, volatility, min_drift, max_drift, jump_min, jump_max, jump_prob, mu; double *returns, *jumps, *total_returns, *prices; } Market;

/* --- STRATEGY FUNCTION POINTER --- */
typedef void (*StrategyFunction)(Portfolio *p, Market *m, void *params);

/* --- STRATEGY DEFINITIONS --- */
typedef struct {
    char name[MAX_STRATEGY_NAME];
    StrategyFunction function;
    void *params;
} Strategy;

/* --- STRATEGY PARAMETERS --- */
typedef struct { double spread; } SimpleMMParams;
typedef struct { double spread; double grid_levels; double grid_spacing; } GridTradingParams;
typedef struct { double momentum_threshold; double order_size; } MomentumParams;
typedef struct { double ma_short; double ma_long; double order_size; } MeanReversionParams;

/* --- STRATEGY RESULTS --- */
typedef struct {
    char strategy_name[MAX_STRATEGY_NAME];
    double final_val;
    double volume;
    double max_drawdown;
    int total_trades;
} StrategyResult;

/* --- THREAD DATA --- */
typedef struct {
    int thread_id;
    Strategy strategies[MAX_STRATEGIES];
    int num_strategies;
    StrategyResult results[MAX_STRATEGIES];
} ThreadData;

/* --- THREAD-SAFE RANDOM (LCG) --- */
static unsigned int lcg_rand(unsigned int *seed){
    *seed = 1664525 * (*seed) + 1013904223;
    return *seed;
}
static double rand_uniform_seed(unsigned int *seed, double a, double b){
    return a + (b - a) * ((double)lcg_rand(seed) / (double)UINT_MAX);
}
static double rand_normal_seed(unsigned int *seed){
    double u1 = ((double)lcg_rand(seed)+1.0)/((double)UINT_MAX+1.0);
    double u2 = ((double)lcg_rand(seed)+1.0)/((double)UINT_MAX+1.0);
    return sqrt(-2.0*log(u1)) * cos(2.0*M_PI*u2);
}

/* --- MARKET INIT --- */
int market_init_seed(Market *m, int n, unsigned int *seed){
    m->n = n;
    m->dt = 1.0/(365.0*24.0*60.0);
    m->S0 = 180.0;
    m->volatility = 0.14;
    m->min_drift = -0.11;
    m->max_drift = 0.11;
    m->jump_min = 0.005;
    m->jump_max = 0.01;
    m->jump_prob = 0.0000006;
    m->mu = rand_uniform_seed(seed, m->min_drift, m->max_drift);

    m->returns = (double*) malloc(sizeof(double)*n);
    m->jumps = (double*) malloc(sizeof(double)*n);
    m->total_returns = (double*) malloc(sizeof(double)*n);
    m->prices = (double*) malloc(sizeof(double)*n);
    if(!m->returns || !m->jumps || !m->total_returns || !m->prices) return -1;

    double cumulative = 0.0;
    for(int i=0;i<n;i++){
        m->returns[i] = (m->mu - 0.5*m->volatility*m->volatility)*m->dt
                        + m->volatility*sqrt(m->dt)*rand_normal_seed(seed);
        m->jumps[i] = 0.0;
        if(rand_uniform_seed(seed,0.0,1.0)<m->jump_prob){
            m->jumps[i] = rand_uniform_seed(seed,-m->jump_max,m->jump_max);
        }
        m->total_returns[i] = m->returns[i] + m->jumps[i];
        cumulative += m->total_returns[i];
        m->prices[i] = m->S0 * exp(cumulative);
        if(!isfinite(m->prices[i])) m->prices[i] = INFINITY;
    }
    return 0;
}

/* --- ORDERBOOK --- */
int orderbook_init(OrderBook *ob,int max_orders){
    ob->orders=(Order*)malloc(sizeof(Order)*max_orders);
    ob->free_ids=(int*)malloc(sizeof(int)*max_orders);
    if(!ob->orders||!ob->free_ids)return -1;
    ob->max_orders=max_orders;
    ob->orders_count=0;
    ob->free_count=0;
    for(int i=0;i<max_orders;i++) ob->orders[i].active=false;
    return 0;
}
int orderbook_order(OrderBook *ob,bool is_buy,float size,float price){
    int idx;
    if(ob->free_count>0) idx=ob->free_ids[--ob->free_count];
    else if(ob->orders_count<ob->max_orders) idx=ob->orders_count++;
    else return -1;
    ob->orders[idx].is_buy=is_buy;
    ob->orders[idx].size=size;
    ob->orders[idx].id=idx;
    ob->orders[idx].price=price;
    ob->orders[idx].active=true;
    return idx;
}
void orderbook_bulk_remove(OrderBook *ob,int *ids,int n_ids){
    for(int i=0;i<n_ids;i++){
        int id=ids[i];
        if(id>=0 && id<ob->max_orders && ob->orders[id].active){
            ob->orders[id].active=false;
            ob->free_ids[ob->free_count++]=id;
        }
    }
}
void orderbook_clear_all(OrderBook *ob){
    for(int i=0;i<ob->orders_count;i++){
        if(ob->orders[i].active){
            ob->orders[i].active=false;
            ob->free_ids[ob->free_count++]=i;
        }
    }
}

/* --- PORTFOLIO --- */
int portfolio_init(Portfolio *p,double capital,int max_orders){
    p->capital=capital; p->usd=capital; p->coin=0; p->busd=0; p->bcoin=0;
    p->fees_limit=0.0004; p->fees_market=0.0007; p->min_order=10.0; p->volume=0;
    return orderbook_init(&p->orderbook,max_orders);
}
void portfolio_reset(Portfolio *p, double capital){
    p->capital=capital; p->usd=capital; p->coin=0; p->busd=0; p->bcoin=0; p->volume=0;
    orderbook_clear_all(&p->orderbook);
    p->orderbook.orders_count=0;
    p->orderbook.free_count=0;
}
int portfolio_limit_order(Portfolio *p,bool is_buy,double size,double price){
    double size_usd=size*price;
    if(size_usd<p->min_order) return -1;
    if(is_buy){
        if(size_usd<(p->usd-p->busd)){
            int id=orderbook_order(&p->orderbook,is_buy,(float)size,(float)price);
            if(id!=-1)p->busd+=size_usd;
            return id;
        }
    } else {
        if(size<(p->coin-p->bcoin)){
            int id=orderbook_order(&p->orderbook,is_buy,(float)size,(float)price);
            if(id!=-1)p->bcoin+=size;
            return id;
        }
    }
    return -1;
}
void portfolio_fill(Portfolio *p,int order_id){
    if(order_id<0 || order_id>=p->orderbook.max_orders) return;
    Order *order=&p->orderbook.orders[order_id];
    if(!order->active) return;
    order->active=false;
    p->orderbook.free_ids[p->orderbook.free_count++]=order_id;
    double size_usd=order->size*order->price;
    double fee=size_usd*p->fees_limit;
    if(order->is_buy){ p->usd-=size_usd+fee; p->coin+=order->size; p->busd-=size_usd; }
    else { p->usd+=size_usd-fee; p->coin-=order->size; p->bcoin-=order->size; }
    p->volume+=size_usd;
}
double portfolio_value(Portfolio *p,double price){ return p->usd+p->coin*price; }

/* --- UTILITY FUNCTIONS --- */
void fill_clear(Portfolio *p,double min,double max,double spread_max){
    double mid=(min+max)/2.0;
    double limit_min=mid*(1.0-(spread_max/2.0));
    double limit_max=mid*(1.0+(spread_max/2.0));
    int *to_cancel=(int*)malloc(sizeof(int)*1024);
    int cancel_count=0;
    if(!to_cancel) return;
    for(int i=0;i<p->orderbook.orders_count;i++){
        Order *order=&p->orderbook.orders[i];
        if(order->active){
            if(order->is_buy){
                if(order->price>min) portfolio_fill(p,order->id);
                else if(order->price<limit_min && cancel_count<1024) to_cancel[cancel_count++]=order->id;
            } else {
                if(order->price<max) portfolio_fill(p,order->id);
                else if(order->price>limit_max && cancel_count<1024) to_cancel[cancel_count++]=order->id;
            }
        }
    }
    if(cancel_count>0) orderbook_bulk_remove(&p->orderbook,to_cancel,cancel_count);
    free(to_cancel);
}

double calculate_sma(double *prices, int start, int period, int max_index) {
    if (start < period - 1) return prices[start];
    double sum = 0.0;
    for (int i = 0; i < period; i++) {
        sum += prices[start - i];
    }
    return sum / period;
}

/* --- STRATEGY IMPLEMENTATIONS --- */

// 1. Simple Market Making
void strategy_simple_mm(Portfolio *p, Market *m, void *params) {
    SimpleMMParams *mm_params = (SimpleMMParams*)params;
    double spread = mm_params->spread;
    
    for(int i=0;i<m->n;i++){
        double price=m->prices[i];
        fill_clear(p,price,price,spread*4.0);
        portfolio_limit_order(p,false,0.1,price*(1.0+spread/2.0));
        portfolio_limit_order(p,true,0.1,price*(1.0-spread/2.0));
    }
}

// 2. Grid Trading
void strategy_grid_trading(Portfolio *p, Market *m, void *params) {
    GridTradingParams *grid_params = (GridTradingParams*)params;
    double spread = grid_params->spread;
    double levels = grid_params->grid_levels;
    double spacing = grid_params->grid_spacing;
    
    for(int i=0;i<m->n;i++){
        double price=m->prices[i];
        fill_clear(p,price,price,spread*4.0);
        
        // Place grid orders
        for(int level=1; level<=levels; level++){
            double buy_price = price * (1.0 - spacing * level);
            double sell_price = price * (1.0 + spacing * level);
            portfolio_limit_order(p,true,0.05,buy_price);
            portfolio_limit_order(p,false,0.05,sell_price);
        }
    }
}

// 3. Momentum Strategy
void strategy_momentum(Portfolio *p, Market *m, void *params) {
    MomentumParams *mom_params = (MomentumParams*)params;
    double threshold = mom_params->momentum_threshold;
    double order_size = mom_params->order_size;
    
    for(int i=1;i<m->n;i++){
        double price=m->prices[i];
        double prev_price=m->prices[i-1];
        double momentum = (price - prev_price) / prev_price;
        
        fill_clear(p,price,price,0.01);
        
        if(momentum > threshold){
            // Buy on positive momentum
            portfolio_limit_order(p,true,order_size,price*0.999);
        } else if(momentum < -threshold){
            // Sell on negative momentum
            if(p->coin > order_size) portfolio_limit_order(p,false,order_size,price*1.001);
        }
    }
}

// 4. Mean Reversion Strategy
void strategy_mean_reversion(Portfolio *p, Market *m, void *params) {
    MeanReversionParams *mr_params = (MeanReversionParams*)params;
    int ma_short = (int)mr_params->ma_short;
    int ma_long = (int)mr_params->ma_long;
    double order_size = mr_params->order_size;
    
    for(int i=ma_long;i<m->n;i++){
        double price=m->prices[i];
        double sma_short = calculate_sma(m->prices, i, ma_short, m->n);
        double sma_long = calculate_sma(m->prices, i, ma_long, m->n);
        
        fill_clear(p,price,price,0.01);
        
        if(price < sma_long * 0.98){
            // Price below long MA - buy
            portfolio_limit_order(p,true,order_size,price*0.999);
        } else if(price > sma_long * 1.02){
            // Price above long MA - sell
            if(p->coin > order_size) portfolio_limit_order(p,false,order_size,price*1.001);
        }
    }
}

/* --- STRATEGY REGISTRATION SYSTEM --- */
void register_strategy(ThreadData *data, const char *name, StrategyFunction func, void *params) {
    if (data->num_strategies >= MAX_STRATEGIES) return;
    
    strcpy(data->strategies[data->num_strategies].name, name);
    data->strategies[data->num_strategies].function = func;
    data->strategies[data->num_strategies].params = params;
    data->num_strategies++;
}

void setup_strategies(ThreadData *data) {
    // Initialize strategy parameters
    static SimpleMMParams mm_tight = {0.001};
    static SimpleMMParams mm_normal = {0.002};
    static SimpleMMParams mm_wide = {0.005};
    
    static GridTradingParams grid_small = {0.002, 3, 0.005}; // Reduced spacing
    static GridTradingParams grid_large = {0.002, 5, 0.01}; // Reduced spacing
    
    static MomentumParams momentum_fast = {0.0005, 0.1}; // Lower threshold
    static MomentumParams momentum_slow = {0.002, 0.15}; // Lower threshold
    
    static MeanReversionParams mean_rev = {10, 50, 0.1};
    
    // Register strategies
    data->num_strategies = 0;
    register_strategy(data, "MM_Tight", strategy_simple_mm, &mm_tight);
    register_strategy(data, "MM_Normal", strategy_simple_mm, &mm_normal);
    register_strategy(data, "MM_Wide", strategy_simple_mm, &mm_wide);
    register_strategy(data, "Grid_Small", strategy_grid_trading, &grid_small);
    register_strategy(data, "Grid_Large", strategy_grid_trading, &grid_large);
    register_strategy(data, "Momentum_Fast", strategy_momentum, &momentum_fast);
    register_strategy(data, "Momentum_Slow", strategy_momentum, &momentum_slow);
    register_strategy(data, "Mean_Reversion", strategy_mean_reversion, &mean_rev);
}

/* --- PERFORMANCE CALCULATION --- */
void calculate_performance_metrics(Portfolio *p, Market *m, StrategyResult *result) {
    result->final_val = portfolio_value(p, m->prices[m->n-1]);
    result->volume = p->volume;
    result->max_drawdown = 0.0; // Simplified - could implement proper drawdown calculation
    result->total_trades = (int)(p->volume / 20.0); // Rough estimate
}

/* --- THREAD FUNCTION --- */
DWORD WINAPI run_multi_strategy_test(LPVOID arg){
    ThreadData *data = (ThreadData*)arg;
    unsigned int seed = (unsigned int)time(NULL) + data->thread_id * 12345;
    
    // Generate market once per thread
    Market market;
    if(market_init_seed(&market, N, &seed) != 0) {
        printf("Error initializing market for thread %d\n", data->thread_id);
        return 1;
    }
    
    // Test each strategy on the same market
    for(int s = 0; s < data->num_strategies; s++) {
        Portfolio pf;
        portfolio_init(&pf, 1000.0, MAX_ORDERS);
        
        // Run strategy
        printf("Thread %d testing strategy: %s\n", data->thread_id, data->strategies[s].name);
        fflush(stdout);
        
        data->strategies[s].function(&pf, &market, data->strategies[s].params);
        
        // Store results
        strcpy(data->results[s].strategy_name, data->strategies[s].name);
        calculate_performance_metrics(&pf, &market, &data->results[s]);
        
        // Cleanup portfolio
        free(pf.orderbook.orders);
        free(pf.orderbook.free_ids);
    }
    
    // Cleanup market
    free(market.returns);
    free(market.jumps);
    free(market.total_returns);
    free(market.prices);
    
    return 0;
}

/* --- MAIN --- */
int main(void){
    HANDLE threads[NUM_THREADS];
    ThreadData data[NUM_THREADS];
    clock_t start = clock();
    
    // Setup strategies for each thread
    for(int i = 0; i < NUM_THREADS; i++){
        data[i].thread_id = i + 1;
        setup_strategies(&data[i]);
        
        threads[i] = CreateThread(NULL, 0, run_multi_strategy_test, &data[i], 0, NULL);
        if(threads[i] == NULL){
            fprintf(stderr, "Erreur creation thread %d\n", i + 1);
            return 1;
        }
    }
    
    WaitForMultipleObjects(NUM_THREADS, threads, TRUE, INFINITE);
    
    // Collect and display results
    printf("\n=== RESULTS SUMMARY ===\n");
    
    // Calculate averages per strategy
    double strategy_totals[MAX_STRATEGIES] = {0};
    double strategy_volumes[MAX_STRATEGIES] = {0};
    int strategy_counts[MAX_STRATEGIES] = {0};
    char strategy_names[MAX_STRATEGIES][MAX_STRATEGY_NAME];
    
    for(int t = 0; t < NUM_THREADS; t++){
        printf("\n--- Thread %d Results ---\n", t + 1);
        for(int s = 0; s < data[t].num_strategies; s++){
            StrategyResult *result = &data[t].results[s];
            printf("  %s: Final=%.2f, Volume=%.2f, ROI=%.2f%%\n", 
                   result->strategy_name, result->final_val, result->volume,
                   (result->final_val - 1000.0) / 1000.0 * 100.0);
            
            strategy_totals[s] += result->final_val;
            strategy_volumes[s] += result->volume;
            strategy_counts[s]++;
            strcpy(strategy_names[s], result->strategy_name);
        }
        CloseHandle(threads[t]);
    }
    
    printf("\n=== STRATEGY AVERAGES ===\n");
    for(int s = 0; s < MAX_STRATEGIES && strategy_counts[s] > 0; s++){
        double avg_final = strategy_totals[s] / strategy_counts[s];
        double avg_volume = strategy_volumes[s] / strategy_counts[s];
        double avg_roi = (avg_final - 1000.0) / 1000.0 * 100.0;
        
        printf("%s: Avg Final=%.2f, Avg Volume=%.2f, Avg ROI=%.2f%% (n=%d)\n",
               strategy_names[s], avg_final, avg_volume, avg_roi, strategy_counts[s]);
    }
    
    clock_t end = clock();
    printf("\nTemps total pour %d threads x %d strategies : %.3f s\n", 
           NUM_THREADS, data[0].num_strategies, (double)(end-start)/CLOCKS_PER_SEC);
    
    return 0;
}
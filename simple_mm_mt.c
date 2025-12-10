#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <windows.h>
#include <limits.h>

#define N 120960
#define MAX_ORDERS 300000
#define NUM_THREADS 20

/* --- STRUCTS --- */
typedef struct { bool is_buy; float size; int id; float price; bool active; } Order;
typedef struct { Order *orders; int max_orders, orders_count, *free_ids, free_count; } OrderBook;
typedef struct { double capital, usd, coin, busd, bcoin, fees_limit, fees_market, min_order, volume; OrderBook orderbook; } Portfolio;
typedef struct { int n; double dt, S0, volatility, min_drift, max_drift, jump_min, jump_max, jump_prob, mu; double *returns, *jumps, *total_returns, *prices; } Market;
typedef struct { int id; double final_val; double volume; } ThreadData;

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

/* --- PORTFOLIO --- */
int portfolio_init(Portfolio *p,double capital,int max_orders){
    p->capital=capital; p->usd=capital; p->coin=0; p->busd=0; p->bcoin=0;
    p->fees_limit=0.0004; p->fees_market=0.0007; p->min_order=10.0; p->volume=0;
    return orderbook_init(&p->orderbook,max_orders);
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

/* --- TESTER --- */
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
void simple_mm(Portfolio *p,Market *m,double spread){
    for(int i=0;i<m->n;i++){
        double price=m->prices[i];
        fill_clear(p,price,price,spread*4.0);
        portfolio_limit_order(p,false,0.1,price*(1.0+spread/2.0));
        portfolio_limit_order(p,true,0.1,price*(1.0-spread/2.0));
        if(i%40000==0){ printf("progress bougie %d/%d, prix=%.2f\n",i,m->n,price); fflush(stdout); }
    }
}

/* --- THREAD --- */
DWORD WINAPI run_test(LPVOID arg){
    ThreadData *data=(ThreadData*)arg;
    unsigned int seed=(unsigned int)time(NULL)+data->id*12345;

    Portfolio pf; portfolio_init(&pf,1000.0,MAX_ORDERS);
    Market *m=(Market*)malloc(sizeof(Market));
    market_init_seed(m,N,&seed);
    simple_mm(&pf,m,0.002);

    data->final_val=portfolio_value(&pf,m->prices[m->n-1]);
    data->volume=pf.volume;

    free(m->returns); free(m->jumps); free(m->total_returns); free(m->prices); free(m);
    free(pf.orderbook.orders); free(pf.orderbook.free_ids);
    return 0;
}

/* --- MAIN --- */
int main(void){
    HANDLE threads[NUM_THREADS];
    ThreadData data[NUM_THREADS];
    clock_t start=clock();

    for(int i=0;i<NUM_THREADS;i++){
        data[i].id=i+1;
        threads[i]=CreateThread(NULL,0,run_test,&data[i],0,NULL);
        if(threads[i]==NULL){ fprintf(stderr,"Erreur creation thread %d\n",i+1); return 1; }
    }

    WaitForMultipleObjects(NUM_THREADS,threads,TRUE,INFINITE);

    for(int i=0;i<NUM_THREADS;i++){
        printf("Thread %d : Valeur finale = %.2f, Volume = %.2f\n",i+1,data[i].final_val,data[i].volume);
        CloseHandle(threads[i]);
    }

    clock_t end=clock();
    printf("Temps total pour %d executions en parallele : %.3f s\n",NUM_THREADS,(double)(end-start)/CLOCKS_PER_SEC);

    return 0;
}

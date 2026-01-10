import json
import pandas as pd
from datetime import datetime
import numpy as np

# Paramètres
ALPHA = 0.0005  # 0.2% de décalage pour entrer en position
LEVERAGE = 10.0  # Levier (1x = sans levier)
FEES_PCT = 0.000082 # 0.1% de frais par trade (entry + exit = 0.2% total)

def load_data(xyz_path, flx_path):
    """Charge les deux fichiers JSON"""
    with open(xyz_path, 'r') as f:
        xyz_data = json.load(f)
    with open(flx_path, 'r') as f:
        flx_data = json.load(f)
    
    df_xyz = pd.DataFrame(xyz_data)
    df_flx = pd.DataFrame(flx_data)
    
    # Convertir les prix en float
    for col in ['o', 'c', 'h', 'l']:
        df_xyz[col] = df_xyz[col].astype(float)
        df_flx[col] = df_flx[col].astype(float)
    
    return df_xyz, df_flx

def calculate_spread(df_xyz, df_flx):
    """Calcule le spread entre les deux sources"""
    # Merge sur le timestamp
    merged = pd.merge(df_xyz[['t', 'c']], df_flx[['t', 'c']], on='t', suffixes=('_xyz', '_flx'))
    
    # Calcul du spread relatif
    merged['spread'] = (merged['c_xyz'] - merged['c_flx']) / merged['c_flx']
    merged['price_avg'] = (merged['c_xyz'] + merged['c_flx']) / 2
    
    return merged

def find_arbitrage_opportunities(merged_df, alpha, leverage, fees_pct):
    """Identifie les opportunités d'arbitrage"""
    opportunities = []
    in_position = False
    entry_spread = 0
    entry_price = 0
    entry_time = 0
    entry_idx = 0
    
    for idx, row in merged_df.iterrows():
        spread = row['spread']
        
        if not in_position:
            # Cherche une entrée : spread > alpha ou spread < -alpha
            if abs(spread) > alpha:
                in_position = True
                entry_spread = spread
                entry_price = row['price_avg']
                entry_time = row['t']
                entry_idx = idx
                
        else:
            # Cherche une sortie : retour à l'équilibre (spread proche de 0)
            # ou inversion du spread
            if (entry_spread > 0 and spread <= 0) or (entry_spread < 0 and spread >= 0):
                # Calcul du profit
                spread_change = abs(entry_spread) - abs(spread)
                
                # Profit brut (on gagne sur le spread / 2 car long et short)
                profit_pct = (spread_change / 2) * leverage
                
                # Profit net après frais (entry + exit)
                profit_net_pct = profit_pct - (fees_pct * 2)
                
                # Durée en minutes
                duration_ms = row['t'] - entry_time
                duration_min = duration_ms / (1000 * 60)
                
                opportunities.append({
                    'entry_time': datetime.fromtimestamp(entry_time/1000),
                    'exit_time': datetime.fromtimestamp(row['t']/1000),
                    'duration_min': duration_min,
                    'entry_spread': entry_spread * 100,  # en %
                    'exit_spread': spread * 100,  # en %
                    'spread_change': spread_change * 100,  # en %
                    'profit_pct': profit_pct * 100,  # en %
                    'profit_net_pct': profit_net_pct * 100,  # en %
                    'entry_price': entry_price,
                })
                
                in_position = False
    
    return pd.DataFrame(opportunities)

def calculate_statistics(opportunities_df):
    """Calcule les statistiques sur les opportunités"""
    if len(opportunities_df) == 0:
        return {
            'total_trades': 0,
            'profitable_trades': 0,
            'win_rate': 0,
            'total_profit_pct': 0,
            'avg_profit_pct': 0,
            'max_profit_pct': 0,
            'min_profit_pct': 0,
            'avg_duration_min': 0,
            'annualized_return_pct': 0,
            'sharpe_ratio': 0,
        }
    
    total_trades = len(opportunities_df)
    profitable_trades = len(opportunities_df[opportunities_df['profit_net_pct'] > 0])
    win_rate = profitable_trades / total_trades * 100
    
    total_profit = opportunities_df['profit_net_pct'].sum()
    avg_profit = opportunities_df['profit_net_pct'].mean()
    max_profit = opportunities_df['profit_net_pct'].max()
    min_profit = opportunities_df['profit_net_pct'].min()
    avg_duration = opportunities_df['duration_min'].mean()
    
    # Rendement annualisé (simplifié)
    total_duration_days = opportunities_df['duration_min'].sum() / (60 * 24)
    if total_duration_days > 0:
        annualized_return = (total_profit / 100) * (365 / total_duration_days) * 100
    else:
        annualized_return = 0
    
    # Sharpe ratio (simplifié, avec taux sans risque = 0)
    if len(opportunities_df) > 1:
        sharpe_ratio = avg_profit / opportunities_df['profit_net_pct'].std() if opportunities_df['profit_net_pct'].std() > 0 else 0
    else:
        sharpe_ratio = 0
    
    return {
        'total_trades': total_trades,
        'profitable_trades': profitable_trades,
        'win_rate': win_rate,
        'total_profit_pct': total_profit,
        'avg_profit_pct': avg_profit,
        'max_profit_pct': max_profit,
        'min_profit_pct': min_profit,
        'avg_duration_min': avg_duration,
        'annualized_return_pct': annualized_return,
        'sharpe_ratio': sharpe_ratio,
    }

def main():
    # Charger les données
    print("📊 Chargement des données...")
    df_xyz, df_flx = load_data('xyz.json', 'flx.json')
    print(f"   XYZ: {len(df_xyz)} candles")
    print(f"   FLX: {len(df_flx)} candles")
    
    # Calculer le spread
    print("\n📈 Calcul du spread...")
    merged = calculate_spread(df_xyz, df_flx)
    print(f"   Spread moyen: {merged['spread'].mean()*100:.3f}%")
    print(f"   Spread max: {merged['spread'].max()*100:.3f}%")
    print(f"   Spread min: {merged['spread'].min()*100:.3f}%")
    
    # Trouver les opportunités
    print(f"\n🎯 Recherche d'opportunités (alpha={ALPHA*100}%, leverage={LEVERAGE}x)...")
    opportunities = find_arbitrage_opportunities(merged, ALPHA, LEVERAGE, FEES_PCT)
    
    # Afficher les résultats
    print(f"\n💰 RÉSULTATS")
    print("=" * 60)
    
    stats = calculate_statistics(opportunities)
    
    print(f"Nombre de trades: {stats['total_trades']}")
    print(f"Trades profitables: {stats['profitable_trades']} ({stats['win_rate']:.1f}%)")
    print(f"\nProfit total: {stats['total_profit_pct']:.3f}%")
    print(f"Profit moyen par trade: {stats['avg_profit_pct']:.3f}%")
    print(f"Meilleur trade: {stats['max_profit_pct']:.3f}%")
    print(f"Pire trade: {stats['min_profit_pct']:.3f}%")
    print(f"\nDurée moyenne: {stats['avg_duration_min']:.1f} minutes")
    print(f"Rendement annualisé: {stats['annualized_return_pct']:.1f}%")
    print(f"Sharpe ratio: {stats['sharpe_ratio']:.2f}")
    
    # Afficher les 10 meilleurs trades
    if len(opportunities) > 0:
        print("\n TOP 10 TRADES")
        print("=" * 60)
        top_trades = opportunities.nlargest(10, 'profit_net_pct')
        for idx, trade in top_trades.iterrows():
            print(f"{trade['entry_time'].strftime('%Y-%m-%d %H:%M')} → {trade['exit_time'].strftime('%H:%M')} "
                  f"| Spread: {trade['entry_spread']:+.3f}% → {trade['exit_spread']:+.3f}% "
                  f"| Profit: {trade['profit_net_pct']:+.3f}% | {trade['duration_min']:.0f}min")
    
    # Sauvegarder les résultats
    if len(opportunities) > 0:
        opportunities.to_csv('arbitrage_results.csv', index=False)
        print(f"\n💾 Résultats sauvegardés dans 'arbitrage_results.csv'")

if __name__ == "__main__":
    main()
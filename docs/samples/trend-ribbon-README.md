# GhostBlock Trend Ribbon (Pine Script v6)

An overlay indicator for TradingView by Brandon S. · GhostBlock.

- **Adaptive EMA ribbon**: 5 moving averages from the fastest to the slowest length. Each one speeds up when price moves
  in a clean line and slows down in chop (Kaufman-style efficiency ratio). Green ribbon = fastest line above slowest.
- **ATR trailing stop**: sits 2.5 x ATR below price in an uptrend (above in a downtrend), only moves in the trade's
  direction, and flips side when a bar closes through it.
- **BUY / SELL labels** when the stop flips, optionally only when the ribbon and the higher-timeframe trend agree.
- **Info table** (top right): ribbon trend, HTF trend, stop side, distance to the stop, bars since the last signal.

## No repainting
- Signals need a closed bar (`barstate.isconfirmed`), so a label never appears and then vanishes.
- The higher-timeframe trend reads the **last closed** HTF bar: `request.security(..., [close[1], ta.ema(close, len)[1]],
  lookahead = barmerge.lookahead_on)`. The `[1]` is what makes this safe: no future data, and the value does not
  change while the HTF bar is still forming. (Plain `lookahead_off` without `[1]` changes on live bars, which is a
  form of repainting.)

## Inputs
| Group | Input | Default | What it does |
|---|---|---|---|
| Ribbon | Fastest length | 8 | Length of the fastest ribbon line |
| Ribbon | Slowest length | 34 | Length of the slowest line (the 3 middle lines are spaced evenly) |
| Ribbon | Source | close | Price the ribbon follows |
| ATR trailing stop | ATR length | 14 | ATR period (Wilder smoothing) |
| ATR trailing stop | ATR multiplier | 2.5 | Stop distance in ATRs; higher = fewer, longer trades |
| Signal filters | Only signal in ribbon direction | on | BUY only on a green ribbon, SELL only on a red one |
| Signal filters | Use higher-timeframe trend filter | on | BUY only when the HTF close is above its EMA (SELL below) |
| Signal filters | Higher timeframe | 240 (4h) | Timeframe for the trend filter |
| Signal filters | HTF EMA length | 50 | EMA length on the higher timeframe |
| Display | Show BUY / SELL labels, Show info table, Bull / Bear color | | Looks only |

## Alerts
- **GB Ribbon Long**: a filtered BUY signal on a closed bar.
- **GB Ribbon Short**: a filtered SELL signal on a closed bar.

Create one with *Alerts -> Create alert -> Condition: GhostBlock Trend Ribbon -> GB Ribbon Long* (or Short),
trigger *Once per bar close*. The message includes the ticker and the close price.

## How to add it to TradingView
1. Open a chart and click **Pine Editor** at the bottom.
2. Paste the contents of `ghostblock_trend_ribbon.pine`, replacing the template.
3. Click **Save**, then **Add to chart**.
4. Open the indicator's settings (gear icon) to change the inputs.

## Preview (`preview.png`)
TradingView can't be run here, so `preview.py` is a Python (pandas + matplotlib) port of the same logic,
using the same formulas and Pine's own EMA / RMA / ATR seeding, run on **synthetic** 1-hour data (a random walk
with up, down and choppy stretches; not a real market). Run: `python preview.py` (needs pandas, matplotlib).
It is a picture of the logic only, not a backtest or trading advice.

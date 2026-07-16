#pragma once
#include <stdint.h>

// Data model for the standalone stock screen. Populated by stock_source from
// the Yahoo Finance chart API.
#define STOCK_HISTORY_MAX     70   // ~3 months of daily closes
#define STOCK_MONTH_MARKS_MAX 5    // a 3-month window can touch up to ~4-5 calendar months

struct StockData {
    char    name[24];                     // display name, e.g. "삼성전자"
    long    price;                        // current price, KRW
    long    avg_buy_price;                // user's average buy price, KRW
    int32_t history[STOCK_HISTORY_MAX];   // recent daily closes, oldest first
    int     history_count;

    // Month-boundary markers for the x-axis: month_mark_idx[i] is the
    // history[] index where month_mark_val[i] (1-12) first appears.
    int     month_mark_idx[STOCK_MONTH_MARKS_MAX];
    int     month_mark_val[STOCK_MONTH_MARKS_MAX];
    int     month_mark_count;

    bool    valid;
};

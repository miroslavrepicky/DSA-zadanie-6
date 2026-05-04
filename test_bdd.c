/*
 * test_bdd.c
 *
 * Testovaci program pre BDD implementaciu.
 * Generuje nahodne DNF funkcie, overi spravnost BDD
 * porovnanim BDD_use() s eval_expression() pre vsetky vstupy.
 *
 * Kompilacia:
 *   gcc -O2 -o test_bdd test_bdd.c bdd.c
 *
 * Spustenie:
 *   ./test_bdd
 */

//#include "bdd_viz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#  include <windows.h>
static double get_time_ms(void) {
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart / freq.QuadPart * 1000.0;
}
#else
#  include <time.h>
static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
#endif

#include "bdd.h"

/* -- Nastavenia ----------------------------------------------------- */
#define SEED          42
#define FUNCS_PER_N   100
#define MAX_VARS      20        /* maximalne podporovane n */

static const int VAR_COUNTS[] = {3, 5, 7, 9, 11, 13, 15};
static const int N_GROUPS     = 7;


/* -- Generovanie nahodneho DNF vyrazu ------------------------------ */

/*
 * Zapise nahodny DNF vyraz do `buf` pre `n` premennych (A, B, C, ...).
 * Format: "A.!B.C+!A.B" atd.
 *
 * Pocet termov: n/2 az 2*n (zaistuje dostatocne pokrytie premennych).
 * Kazda premenna sa do kazdeho termu zaradi s pravdepodobnostou 50%.
 * Zaruci sa ze vsetky n premennych sa vyskytnu aspon raz v celom vyraze.
 */
static void random_dnf(int n, char *buf)
{
    int min_terms = n / 2 < 1 ? 1 : n / 2;
    int max_terms = 2 * n;
    int num_terms = min_terms + rand() % (max_terms - min_terms + 1);
 
    int used[MAX_VARS] = {0};   /* sledujeme ktore premenne su uz pouzite */
    int pos = 0;
 
    for (int t = 0; t < num_terms; t++) {
        if (t > 0) buf[pos++] = '+';
 
        int first_lit = 1;
        for (int i = 0; i < n; i++) {
            if (rand() % 2 == 0) continue;   /* premennu vynechame */
            if (!first_lit) buf[pos++] = '.';
            first_lit = 0;
            if (rand() % 2 == 0) buf[pos++] = '!';
            buf[pos++] = 'A' + i;
            used[i] = 1;
        }
        /* ak sme nevybrali ziaden literal, pridame aspon jeden */
        if (first_lit) {
            int i = rand() % n;
            if (rand() % 2 == 0) buf[pos++] = '!';
            buf[pos++] = 'A' + i;
            used[i] = 1;
        }
    }
 
    /* Zaruci ze kazda premenna sa vyskytne aspon raz v celom vyraze */
    for (int i = 0; i < n; i++) {
        if (!used[i]) {
            buf[pos++] = '+';
            if (rand() % 2 == 0) buf[pos++] = '!';
            buf[pos++] = 'A' + i;
        }
    }
 
    buf[pos] = '\0';
}

/* -- Overenie BDD pre vsetky vstupy -------------------------------- */

static int verify_bdd(BDD *bdd, const char *expr,
                      const char *var_order, int n)
{
    int errors  = 0;
    int total   = 1 << n;
    char canonical[MAX_VARS + 1];
    char bdd_inputs[MAX_VARS + 1];
    canonical[n] = '\0';
    bdd_inputs[n] = '\0';

    for (int mask = 0; mask < total; mask++) {
        /* canonical vstupy v poradí var_order = "ABCD..." */
        for (int i = 0; i < n; i++)
            canonical[i] = ((mask >> (n - 1 - i)) & 1) ? '1' : '0';

        /* premapuj premenné */
        for (int j = 0; j < n; j++) {
            char var = bdd->varOrder[j];
            bdd_inputs[j] = canonical[var - 'A'];
        }

        char expected = eval_expression(expr, var_order, canonical) ? '1' : '0';
        char got      = BDD_use(bdd, bdd_inputs);

        if (got != expected) {
            if (errors < 3)
                printf("    CHYBA: inputs=%s got=%c expected=%c\n",
                       canonical, got, expected);
            errors++;
        }
    }
    return errors;
}

/* -- Statistika pre jednu skupinu premennych ----------------------- */
typedef struct {
    int    n, total, passed, passed_best;
    double avg_nodes_create, avg_nodes_best;
    double avg_reduction_pct, avg_extra_reduction;
    double min_extra, max_extra;
    int    best_beats_create;
    double time_create_ms;   /* celkovy cas BDD_create pre skupinu  */
    double time_best_ms;     /* celkovy cas BDD_best_order          */
} GroupStats;

/* -- Hlavna funkcia ------------------------------------------------ */
int main(void)
{
    srand(SEED);

    printf("|==============================================|\n");
    printf("|           BDD tester                         |\n");
    printf("|==============================================|\n\n");

    GroupStats stats[N_GROUPS];
    memset(stats, 0, sizeof(stats));
    for (int g = 0; g < N_GROUPS; g++) {
        stats[g].n         = VAR_COUNTS[g];
        stats[g].min_extra =  1e9;
        stats[g].max_extra = -1e9;
    }

    int grand_errors = 0;

    char expr[4096];
    char var_order[MAX_VARS + 1];

    for (int g = 0; g < N_GROUPS; g++) {
        int n = VAR_COUNTS[g];
        GroupStats *gs = &stats[g];

        /* Zostavime var_order = "ABCD..." */
        for (int i = 0; i < n; i++) var_order[i] = 'A' + i;
        var_order[n] = '\0';

        int full = (1 << n) - 1;   /* pocet uzlov plneho stromu */

        printf("Generujem testy pre n=%d...\n", n);

        while (gs->total < FUNCS_PER_N) {
            random_dnf(n, expr);
            gs->total++;

            /* -- BDD_create -- */
            double t0, t1;
            t0 = get_time_ms();
            BDD *bdd = BDD_create(expr, var_order);
            // char filename[256];
            // snprintf(filename, sizeof(filename), "bdd_%s.html", expr);
            // BDD_export_html(bdd, filename);

            t1 = get_time_ms();
            gs->time_create_ms += t1 - t0;
            int nodes_c = bdd->numNodes;
            int err_c   = verify_bdd(bdd, expr, var_order, n);
            BDD_free(bdd);
            if (err_c == 0) gs->passed++;
            else { grand_errors += err_c;
                   printf("[n=%d #%d] BDD_create CHYBY=%d expr=%s\n",
                          n, gs->total, err_c, expr); }

            /* -- BDD_create_with_best_order -- */
            t0 = get_time_ms();
            BDD *bdd_best = BDD_create_with_best_order(expr);
            // char filename2[256];
            // snprintf(filename2, sizeof(filename2), "bdd_best_%s.html", expr);
            // BDD_export_html(bdd_best, filename2);
            t1 = get_time_ms();
            gs->time_best_ms += t1 - t0;
            int nodes_b   = bdd_best->numNodes;
            int err_b     = verify_bdd(bdd_best, expr, var_order, n);
            BDD_free(bdd_best);

            if (err_b == 0) gs->passed_best++;
            else { grand_errors += err_b;
                   printf("[n=%d #%d] BDD_best CHYBY=%d expr=%s\n",
                          n, gs->total, err_b, expr); }

            /* -- Statistiky -- */
            gs->avg_nodes_create += nodes_c;
            gs->avg_nodes_best   += nodes_b;

            double red   = full > 0 ? 100.0*(full - nodes_c)/full : 0.0;
            double extra = nodes_c > 0 ? 100.0*(nodes_c - nodes_b)/nodes_c : 0.0;
            gs->avg_reduction_pct  += red;
            gs->avg_extra_reduction += extra;
            if (extra < gs->min_extra) gs->min_extra = extra;
            if (extra > gs->max_extra) gs->max_extra = extra;
            if (nodes_b < nodes_c) gs->best_beats_create++;
        }

        /* Priemery */
        if (gs->total > 0) {
            gs->avg_nodes_create    /= gs->total;
            gs->avg_nodes_best      /= gs->total;
            gs->avg_reduction_pct   /= gs->total;
            gs->avg_extra_reduction /= gs->total;
        }
    }

    /* -- Výpis výsledkov -------------------------------------------- */
    printf("\n");
    printf("|=======================================================================================================|\n");
    printf("|  n |Testov|Sprav |Spr.B |Uzly avg  |UzlyB avg |Reduk.%%   |ExtraRed.%%|Best<Crea |t_crt ms  |t_best ms  |\n");
    printf("|====|======|======|======|==========|==========|==========|==========|==========|==========|=========== |\n");
    for (int g = 0; g < N_GROUPS; g++) {
        GroupStats *gs = &stats[g];
        printf("| %2d | %4d | %4d | %4d | %8.2f | %8.2f | %7.2f%% | %7.2f%% | %4d/%3d | %8.2f | %8.2f |\n",
               gs->n, gs->total, gs->passed, gs->passed_best,
               gs->avg_nodes_create, gs->avg_nodes_best,
               gs->avg_reduction_pct, gs->avg_extra_reduction,
               gs->best_beats_create, gs->total,
               gs->time_create_ms, gs->time_best_ms);
    }
    printf("|====|======|======|======|==========|==========|==========|==========|==========|==========|=========== |\n");

    printf("\n-- Detailna statistika extra redukcie (best vs. create) --\n");
    for (int g = 0; g < N_GROUPS; g++) {
        GroupStats *gs = &stats[g];
        printf("  n=%d: min=%.2f%%  avg=%.2f%%  max=%.2f%%\n",
               gs->n, gs->min_extra, gs->avg_extra_reduction, gs->max_extra);
    }

    int total_all = 0, passed_all = 0, passed_best_all = 0;
    for (int g = 0; g < N_GROUPS; g++) {
        total_all       += stats[g].total;
        passed_all      += stats[g].passed;
        passed_best_all += stats[g].passed_best;
    }
    printf("\n-- Celkove vysledky --\n");
    printf("  Celkovo testov    : %d\n", total_all);
    printf("  BDD_create OK     : %d / %d  (%.1f%%)\n",
           passed_all, total_all, total_all ? 100.0*passed_all/total_all : 0.0);
    printf("  BDD_best_order OK : %d / %d  (%.1f%%)\n",
           passed_best_all, total_all, total_all ? 100.0*passed_best_all/total_all : 0.0);
    printf("  Celkovy pocet chyb: %d\n", grand_errors);

    if (grand_errors == 0)
        printf("\n Vsetky testy presli bez chyb!\n\n");
    else
        printf("\n Niektore testy ZLYHALI.\n\n");

    return grand_errors > 0 ? 1 : 0;
}
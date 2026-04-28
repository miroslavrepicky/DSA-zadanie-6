/*
 * test_bdd_advanced.c
 *
 * Pokročilý testovací program pre BDD implementáciu.
 *
 * Načíta testy z tests.json (vygenerované skriptom generate_tests.py),
 * pre každý test:
 *   1. Vytvorí BDD pomocou BDD_create (s daným poradím premenných)
 *   2. Overí správnosť pomocou BDD_use pre všetky kombinácie vstupov
 *      (porovnanie s truth table z JSON)
 *   3. Zmerá percentuálnu mieru redukcie BDD vs. plný strom
 *   4. Vytvorí BDD pomocou BDD_create_with_best_order
 *   5. Overí správnosť aj pre toto BDD
 *   6. Porovná počet uzlov: create vs. best_order
 *
 * Na konci vypíše súhrnnú štatistiku pre každý počet premenných.
 *
 * Kompilácia:
 *   gcc -O2 -o test_bdd_advanced test_bdd_advanced.c bdd.c -lm
 *
 * Spustenie:
 *   ./test_bdd_advanced            (hľadá tests.json v aktuálnom adresári)
 *   ./test_bdd_advanced myfile.json
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "bdd.h"

/* ==================================================================
   Minimalistický JSON parser – potrebujeme iba základné hodnoty.
   Plná knižnica by bola závislostí navyše; parsujeme ručne.
   ================================================================== */

/* Maximálne dĺžky reťazcov */
#define MAX_EXPR    4096
#define MAX_ORDER   64
#define MAX_TT      (1 << 20)   /* 2^20 = 1048576 kombinácií pre n=20 */
#define MAX_TESTS   600         /* 5 veľkostí × 100 + rezerva       */

typedef struct {
    int  num_vars;
    char var_order[MAX_ORDER];
    char expression[MAX_EXPR];
    char truth_table[MAX_TT];   /* truth_table["000"] = '0' alebo '1' */
    int  tt_size;               /* 2^num_vars */
} TestCase;

/* -- Pomocné: preskočí biele znaky -------------------------------- */
static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* -- Prečíta reťazec v úvodzovkách, vracia pointer za záverečnú " -- */
static const char *read_string(const char *p, char *buf, int bufsize)
{
    if (*p != '"') return p;
    p++;
    int i = 0;
    while (*p && *p != '"') {
        if (*p == '\\') { p++; }  /* escape – jednoduché ignorovanie */
        if (i < bufsize - 1) buf[i++] = *p;
        p++;
    }
    buf[i] = '\0';
    if (*p == '"') p++;
    return p;
}

/* -- Prečíta celé číslo ------------------------------------------- */
static const char *read_int(const char *p, int *out)
{
    *out = 0;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    while (*p >= '0' && *p <= '9') { *out = *out * 10 + (*p - '0'); p++; }
    if (neg) *out = -(*out);
    return p;
}

/*
 * Parsuje tests.json.
 * Vracia počet načítaných testov, alebo -1 pri chybe.
 */
static int parse_json(const char *filename, TestCase *tests, int max_tests)
{
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Chyba: Nemožno otvoriť %s\n", filename);
        return -1;
    }

    /* Načítame celý súbor do pamäte */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(fsize + 1);
    if (!buf) { fclose(f); return -1; }
    fread(buf, 1, fsize, f);
    buf[fsize] = '\0';
    fclose(f);

    int count = 0;
    const char *p = buf;

    /* Hľadáme "tests": [ */
    p = strstr(p, "\"tests\"");
    if (!p) { free(buf); return -1; }
    p = strchr(p, '[');
    if (!p) { free(buf); return -1; }
    p++;

    while (count < max_tests) {
        p = skip_ws(p);
        if (*p == ']') break;
        if (*p == ',') { p++; continue; }
        if (*p != '{') { p++; continue; }
        p++; /* preskočíme '{' */

        TestCase *tc = &tests[count];
        memset(tc, 0, sizeof(TestCase));

        /* Parsujeme kľúče objektu */
        while (1) {
            p = skip_ws(p);
            if (*p == '}') { p++; break; }
            if (*p == ',') { p++; continue; }
            if (*p != '"') { p++; continue; }

            char key[64];
            p = read_string(p, key, sizeof(key));
            p = skip_ws(p);
            if (*p == ':') p++;
            p = skip_ws(p);

            if (strcmp(key, "num_vars") == 0) {
                p = read_int(p, &tc->num_vars);
            } else if (strcmp(key, "var_order") == 0) {
                p = read_string(p, tc->var_order, sizeof(tc->var_order));
            } else if (strcmp(key, "expression") == 0) {
                p = read_string(p, tc->expression, sizeof(tc->expression));
            } else if (strcmp(key, "truth_table") == 0) {
                /* Parsujeme objekt { "000": 0, "001": 1, ... } */
                if (*p == '{') {
                    p++;
                    tc->tt_size = 1 << tc->num_vars;
                    while (1) {
                        p = skip_ws(p);
                        if (*p == '}') { p++; break; }
                        if (*p == ',') { p++; continue; }
                        if (*p != '"') { p++; continue; }

                        char tt_key[MAX_ORDER];
                        p = read_string(p, tt_key, sizeof(tt_key));
                        p = skip_ws(p);
                        if (*p == ':') p++;
                        p = skip_ws(p);
                        int val;
                        p = read_int(p, &val);

                        /* tt_key je bitový reťazec, napr. "010"
                           Prevedieme na index */
                        int n = tc->num_vars;
                        int idx = 0;
                        for (int i = 0; i < n; i++)
                            idx = idx * 2 + (tt_key[i] - '0');
                        if (idx < MAX_TT)
                            tc->truth_table[idx] = (char)val;
                    }
                }
            } else {
                /* Preskočíme neznámu hodnotu */
                if (*p == '"') {
                    char tmp[MAX_EXPR];
                    p = read_string(p, tmp, sizeof(tmp));
                } else if (*p >= '0' && *p <= '9') {
                    int tmp; p = read_int(p, &tmp);
                }
            }
        }

        if (tc->num_vars > 0 && tc->var_order[0] != '\0' && tc->expression[0] != '\0')
            count++;
    }

    free(buf);
    return count;
}

/* ==================================================================
   Výpočet počtu uzlov v plnom binárnom rozhodovacím strome (bez
   redukcie). Pre n premenných a DNF výraz je horná hranica plného
   stromu 2^(n+1) - 1 interných uzlov (kompletný binárny strom
   hĺbky n). Používame tento teoretický maximum pre výpočet redukcie.
   ================================================================== */
static int full_tree_nodes(int n)
{
    /* Kompletný binárny strom hĺbky n má 2^n - 1 interných uzlov */
    return (1 << n) - 1;
}

/* ==================================================================
   Overenie správnosti BDD pre všetky kombinácie vstupov.
   Vráti počet chýb.
   ================================================================== */
static int verify_bdd(BDD *bdd, const TestCase *tc)
{
    int n      = tc->num_vars;
    int total  = 1 << n;
    int errors = 0;
    /* canonical_inputs: bits v poradí tc->var_order (napr. "ABC")
       bdd_inputs:       bits v poradí bdd->varOrder  (môže byť "BCA") */
    char canonical_inputs[MAX_ORDER];
    char bdd_inputs[MAX_ORDER];
    canonical_inputs[n] = '\0';
    bdd_inputs[n] = '\0';

    for (int mask = 0; mask < total; mask++) {
        /* Generujeme canonical_inputs v poradí tc->var_order */
        for (int i = 0; i < n; i++)
            canonical_inputs[i] = ((mask >> (n - 1 - i)) & 1) ? '1' : '0';

        /* Premapujeme na poradie bdd->varOrder */
        for (int j = 0; j < n; j++) {
            char var = bdd->varOrder[j];
            /* nájdi pozíciu tejto premennej v tc->var_order */
            const char *pos = strchr(tc->var_order, var);
            if (!pos) { bdd_inputs[j] = '0'; continue; }
            int src_idx = (int)(pos - tc->var_order);
            bdd_inputs[j] = canonical_inputs[src_idx];
        }

        char got      = BDD_use(bdd, bdd_inputs);
        char expected = tc->truth_table[mask] ? '1' : '0';

        if (got != expected) {
            if (errors < 3)
                printf("    CHYBA: inputs=%s got=%c expected=%c\n",
                       canonical_inputs, got, expected);
            errors++;
        }
    }
    return errors;
}

/* ==================================================================
   Štatistika pre jednu skupinu premenných
   ================================================================== */
typedef struct {
    int    n;
    int    total;
    int    passed;
    int    passed_best;
    double avg_nodes_create;
    double avg_nodes_best;
    double avg_reduction_pct;     /* vs. plný strom */
    double avg_extra_reduction;   /* best_order vs. create */
    double min_extra_reduction;
    double max_extra_reduction;
    int    best_beats_create;     /* koľkokrát best_order < create */
} GroupStats;

/* ==================================================================
   HLAVNÁ FUNKCIA
   ================================================================== */
int main(int argc, char *argv[])
{
    const char *json_file = (argc > 1) ? argv[1] : "tests.json";

    printf("|==========================================================|\n");
    printf("|         Pokrocily BDD tester - nacitanie z JSON          |\n");
    printf("|==========================================================|\n\n");
    printf("Nacitavam testy z: %s\n", json_file);

    TestCase *tests = malloc(MAX_TESTS * sizeof(TestCase));
    if (!tests) { fprintf(stderr, "malloc zlyhalo\n"); return 1; }

    int ntests = parse_json(json_file, tests, MAX_TESTS);
    if (ntests < 0) { free(tests); return 1; }
    printf("Nacitanych testov: %d\n\n", ntests);

    /* Nájdeme unikátne počty premenných */
    int var_counts[16];
    int nvc = 0;
    for (int i = 0; i < ntests; i++) {
        int n = tests[i].num_vars;
        int found = 0;
        for (int j = 0; j < nvc; j++) if (var_counts[j] == n) { found = 1; break; }
        if (!found && nvc < 16) var_counts[nvc++] = n;
    }
    /* Zoradíme */
    for (int i = 0; i < nvc - 1; i++)
        for (int j = i + 1; j < nvc; j++)
            if (var_counts[i] > var_counts[j]) {
                int t = var_counts[i]; var_counts[i] = var_counts[j]; var_counts[j] = t;
            }

    GroupStats *stats = calloc(nvc, sizeof(GroupStats));
    for (int gi = 0; gi < nvc; gi++) {
        stats[gi].n = var_counts[gi];
        stats[gi].min_extra_reduction = 1e9;
        stats[gi].max_extra_reduction = -1e9;
    }

    int grand_errors = 0;

    /* -- Hlavná slučka testov ------------------------------------ */
    for (int ti = 0; ti < ntests; ti++) {
        const TestCase *tc = &tests[ti];
        int n = tc->num_vars;

        /* Nájdi skupinu */
        int gi = 0;
        for (int g = 0; g < nvc; g++) if (var_counts[g] == n) { gi = g; break; }
        GroupStats *gs = &stats[gi];
        gs->total++;

        int full = full_tree_nodes(n);

        /* -- 1. BDD_create -------------------------------------- */
        BDD *bdd = BDD_create(tc->expression, tc->var_order);
        int nodes_create = bdd->numNodes;
        int err_create   = verify_bdd(bdd, tc);

        if (err_create == 0) {
            gs->passed++;
        } else {
            grand_errors += err_create;
            printf("[n=%d #%d] BDD_create CHYBY=%d  expr=%s\n",
                   n, gs->total, err_create, tc->expression);
        }
        BDD_free(bdd);

        /* -- 2. BDD_create_with_best_order ---------------------- */
        BDD *bdd_best = BDD_create_with_best_order(tc->expression);
        int nodes_best  = bdd_best->numNodes;
        int err_best    = verify_bdd(bdd_best, tc);

        if (err_best == 0) {
            gs->passed_best++;
        } else {
            grand_errors += err_best;
            printf("[n=%d #%d] BDD_best CHYBY=%d  expr=%s\n",
                   n, gs->total, err_best, tc->expression);
        }
        BDD_free(bdd_best);

        /* -- Štatistiky ----------------------------------------- */
        gs->avg_nodes_create += nodes_create;
        gs->avg_nodes_best   += nodes_best;

        /* Redukcia vs. plný strom (v %) */
        double red = 0.0;
        if (full > 0)
            red = 100.0 * (full - nodes_create) / full;
        gs->avg_reduction_pct += red;

        /* Dodatočná redukcia best_order vs. create (v %) */
        double extra = 0.0;
        if (nodes_create > 0)
            extra = 100.0 * (nodes_create - nodes_best) / nodes_create;
        else if (nodes_best == 0)
            extra = 0.0;
        gs->avg_extra_reduction += extra;
        if (extra < gs->min_extra_reduction) gs->min_extra_reduction = extra;
        if (extra > gs->max_extra_reduction) gs->max_extra_reduction = extra;
        if (nodes_best < nodes_create) gs->best_beats_create++;
    }

    /* -- Finalizácia priemerov ----------------------------------- */
    for (int gi = 0; gi < nvc; gi++) {
        GroupStats *gs = &stats[gi];
        if (gs->total > 0) {
            gs->avg_nodes_create   /= gs->total;
            gs->avg_nodes_best     /= gs->total;
            gs->avg_reduction_pct  /= gs->total;
            gs->avg_extra_reduction /= gs->total;
        }
    }

    /* ==============================================================
       VÝSTUP VÝSLEDKOV
       ============================================================== */
    printf("\n");
    printf("|==================================================================================|\n");
    printf("|                           SUHRNNE VYSLEDKY                                      |\n");
    printf("|=================================================================================|\n");
    printf("|  n |Testov|Sprav |Spr.B |Uzly avg  |UzlyB avg |Reduk.%%   |ExtraRed.%%|Best<Crea |\n");
    printf("|    |      |(crt) |(best)|(create)  |(best)    |vs.plny   |best/crea |          |\n");
    printf("|====|======|======|======|==========|==========|==========|==========|===========|\n");

    for (int gi = 0; gi < nvc; gi++) {
        GroupStats *gs = &stats[gi];
        printf("| %2d | %4d | %4d | %4d | %8.2f | %8.2f | %7.2f%% | %7.2f%% | %4d/%4d |\n",
               gs->n, gs->total,
               gs->passed, gs->passed_best,
               gs->avg_nodes_create, gs->avg_nodes_best,
               gs->avg_reduction_pct, gs->avg_extra_reduction,
               gs->best_beats_create, gs->total);
    }

    printf("|====|======|======|======|==========|==========|==========|==========|===========|\n");

    /* Detailná štatistika extra redukcie */
    printf("\n-- Detailna statistika extra redukcie (best_order vs. create) --\n");
    for (int gi = 0; gi < nvc; gi++) {
        GroupStats *gs = &stats[gi];
        printf("  n=%d: min=%.2f%%  avg=%.2f%%  max=%.2f%%\n",
               gs->n,
               gs->min_extra_reduction,
               gs->avg_extra_reduction,
               gs->max_extra_reduction);
    }

    printf("\n-- Celkove vysledky --\n");
    int total_all = 0, passed_all = 0, passed_best_all = 0;
    for (int gi = 0; gi < nvc; gi++) {
        total_all       += stats[gi].total;
        passed_all      += stats[gi].passed;
        passed_best_all += stats[gi].passed_best;
    }
    printf("  Celkovo testov    : %d\n", total_all);
    printf("  BDD_create OK     : %d / %d  (%.1f%%)\n",
           passed_all, total_all,
           total_all ? 100.0 * passed_all / total_all : 0.0);
    printf("  BDD_best_order OK : %d / %d  (%.1f%%)\n",
           passed_best_all, total_all,
           total_all ? 100.0 * passed_best_all / total_all : 0.0);
    printf("  Celkovy pocet chyb BDD_use: %d\n", grand_errors);

    if (grand_errors == 0)
        printf("\n Vsetky testy presli bez chyb!\n\n");
    else
        printf("\n Niektore testy ZLYHALI. Skontrolujte vystup vyssie.\n\n");

    free(stats);
    free(tests);
    return grand_errors > 0 ? 1 : 0;
}

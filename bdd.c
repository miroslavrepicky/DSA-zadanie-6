#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "bdd.h"

/* ==================================================================
   UNIQUE TABLE  (hash map: (variable, high*, low*) -> BDDNode*)
   kľúč je trojica, nie single int.
   ================================================================== */

static long long ut_hash(UniqueTable *ut, char variable,
                         BDDNode *high, BDDNode *low)
{
    /* Kombinujeme tri hodnoty do jedného hashu (FNV-inšpirované).
       Pointery pretypujeme na uintptr_t aby sme mali číslo. */
    long long h = (unsigned char)variable;
    h = h * 1000003LL ^ (long long)(size_t)high;
    h = h * 1000003LL ^ (long long)(size_t)low;
    return ((h % ut->size) + ut->size) % ut->size;
}

static UniqueTable *ut_create(long long size)
{
    UniqueTable *ut = malloc(sizeof(UniqueTable));
    ut->size    = size;
    ut->count   = 0;
    ut->buckets = calloc(size, sizeof(UTNode *));
    return ut;
}

/* Vloží uzol do tabuľky BEZ kontroly duplikátov (interné použitie). */
static void ut_insert_node(UniqueTable *ut, char variable,
                           BDDNode *high, BDDNode *low, BDDNode *node)
{
    long long idx = ut_hash(ut, variable, high, low);
    UTNode *entry = malloc(sizeof(UTNode));
    entry->variable = variable;
    entry->high     = high;
    entry->low      = low;
    entry->node     = node;
    entry->next     = ut->buckets[idx];
    ut->buckets[idx] = entry;
    ut->count++;
}

static void ut_rehash(UniqueTable *ut, double factor);

/* Hľadá existujúci uzol. Vráti ho, alebo NULL. */
static BDDNode *ut_lookup(UniqueTable *ut, char variable,
                          BDDNode *high, BDDNode *low)
{
    long long idx = ut_hash(ut, variable, high, low);
    UTNode *cur = ut->buckets[idx];
    while (cur) {
        if (cur->variable == variable &&
            cur->high     == high     &&
            cur->low      == low)
            return cur->node;
        cur = cur->next;
    }
    return NULL;
}

/* Vloží uzol, ak ešte neexistuje. Vráti existujúci alebo nový uzol. */
static BDDNode *ut_get_or_insert(UniqueTable *ut, char variable,
                                 BDDNode *high, BDDNode *low,
                                 int *node_counter)
{
    BDDNode *existing = ut_lookup(ut, variable, high, low);
    if (existing) return existing;

    BDDNode *n  = malloc(sizeof(BDDNode));
    n->variable = variable;
    n->value    = -1;          /* nie je terminál */
    n->high     = high;
    n->low      = low;
    n->id       = (*node_counter)++;

    ut_insert_node(ut, variable, high, low, n);

    if ((double)ut->count / ut->size > UT_ALPHA_HIGH)
        ut_rehash(ut, 2.0);

    return n;
}

static void ut_rehash(UniqueTable *ut, double factor)
{
    long long old_size    = ut->size;
    UTNode  **old_buckets = ut->buckets;

    long long new_size = (long long)(old_size * factor);
    if (new_size < 4) new_size = 4;

    ut->size    = new_size;
    ut->count   = 0;
    ut->buckets = calloc(new_size, sizeof(UTNode *));

    for (long long i = 0; i < old_size; i++) {
        UTNode *cur = old_buckets[i];
        while (cur) {
            UTNode *nxt = cur->next;
            /* Prepočítame index v novej tabuľke */
            long long idx = ut_hash(ut, cur->variable, cur->high, cur->low);
            cur->next = ut->buckets[idx];
            ut->buckets[idx] = cur;
            ut->count++;
            cur = nxt;
        }
    }
    free(old_buckets);
}

static void ut_free(UniqueTable *ut)
{
    if (!ut) return;
    for (long long i = 0; i < ut->size; i++) {
        UTNode *cur = ut->buckets[i];
        while (cur) {
            UTNode *tmp = cur;
            cur = cur->next;
            free(tmp->node);   /* uvoľníme BDDNode */
            free(tmp);
        }
    }
    free(ut->buckets);
    free(ut);
}

/* ==================================================================
   VÝRAZ – PARSER / EVALUÁTOR / SIMPLIFIER
   Formát DNF: termy oddelené '+', literály sú 'A', '!A', '1', '0'
   Oddeľovač literálov v terme: '.' (voliteľný).
   Príklady: "A.!B.C+!A.B", "AB+C", "A.B.!C+!A.!B.C"
   ================================================================== */

/* Vyhodnotí jeden term (sekvenciu literálov).
   Vráti 0 alebo 1. Ak narazí na neznámu premennú (ešte nezdosadenú),
   vráti -1 (neúplné). */
static int eval_term(const char *t, const char *varOrder, const char *vals)
{
    int i = 0;
    while (t[i] != '\0' && t[i] != '+') {
        int neg = 0;
        if (t[i] == '!') { neg = 1; i++; }
        if (t[i] == '.' ) { i++; continue; }

        int lit_val;
        if (t[i] == '0') { lit_val = 0; i++; }
        else if (t[i] == '1') { lit_val = 1; i++; }
        else {
            /* premenná */
            char var = t[i]; i++;
            /* nájdi index premennej v poradí */
            const char *pos = strchr(varOrder, var);
            if (!pos) return -1;  /* neznáma premenná */
            int idx = (int)(pos - varOrder);
            lit_val = (vals[idx] == '1') ? 1 : 0;
        }
        if (neg) lit_val = 1 - lit_val;
        if (lit_val == 0) return 0;  /* term je 0 */
    }
    return 1;
}

/* Vyhodnotí celý DNF výraz pre dané hodnoty premenných.
   varOrder = "ABC", vals = "101" -> A=1, B=0, C=1 */
int eval_expression(const char *expr, const char *varOrder, const char *vals)
{
    const char *p = expr;
    while (*p) {
        int r = eval_term(p, varOrder, vals);
        if (r == 1) return 1;   /* aspoň jeden term je 1 */
        /* preskočíme na ďalší term */
        while (*p && *p != '+') p++;
        if (*p == '+') p++;
    }
    return 0;
}

/* -- Zjednodušenie výrazu po dosadení jednej premennej ------------ *
 * Algoritmus:
 *  1. Pre každý term: dosad var=val, vyhodnoť literály s hodnotou.
 *     – ak term = 0 -> vypusti ho
 *     – ak term = 1 (všetky literály splnené) -> celý výraz = "1"
 *     – inak -> ponechaj zostatok literálov
 *  2. Ak žiadny term neostal -> výsledok = "0"
 * ------------------------------------------------------------------ */
static char *simplify(const char *expr, char var, int val)
{
    /* Výstupný buffer – nemôže byť dlhší ako vstup */
    int   len   = (int)strlen(expr);
    char *out   = malloc(len + 2);
    int   opos  = 0;
    int   terms = 0;          /* počet termov v outpute */

    const char *p = expr;
    while (*p) {
        /* ---- spracuj jeden term ---- */
        char term_buf[256];
        int  tpos     = 0;
        int  term_val = 1;    /* predpokladáme, že term je 1 */

        while (*p && *p != '+') {
            int neg = 0;
            if (*p == '!') { neg = 1; p++; } // literal je negovany
            if (*p == '.')  { p++; continue; } // bodku preskocime

            int lit_val;
            if (*p == '0') { lit_val = 0; p++; } // literal je konstanta
            else if (*p == '1') { lit_val = 1; p++; }
            else { 
                char v = *p; p++;
                if (v == var) { // literal je premenna za ktoru dosadzujeme
                    /* dosadíme */
                    lit_val = val;
                } else {
                    /* premenná ešte nie je dosadená – nechaj v terme */
                    if (tpos > 0) term_buf[tpos++] = '.';
                    if (neg)      term_buf[tpos++] = '!';
                    term_buf[tpos++] = v;
                    lit_val = 2;   /* sentinela – "neznáma" este sme ho nespracovali*/
                }
            }

            if (lit_val != 2) {
                if (neg) lit_val = 1 - lit_val;
                if (lit_val == 0) { term_val = 0; break; }
                /* lit_val == 1 -> tento literál je splnený, nič nepridáme */
            }
        }
        /* preskočíme zvyšok termu ak sme ho opustili predčasne */
        while (*p && *p != '+') p++;
        if (*p == '+') p++;

        if (term_val == 0) continue;  /* term vypadáva */

        term_buf[tpos] = '\0';

        /* Ak term_buf je prázdny (žiadny literál nezostal nevyhodnotený), term je čistá 1 -> celý výraz = 1 */
        if (tpos == 0) {
            free(out);
            out = malloc(2);
            out[0] = '1'; out[1] = '\0';
            return out;
        }

        /* Pridaj term do výstupu */
        if (terms > 0) out[opos++] = '+';
        memcpy(out + opos, term_buf, tpos);
        opos += tpos;
        terms++;
    }

    if (terms == 0) {
        free(out);
        out = malloc(2);
        out[0] = '0'; out[1] = '\0';
        return out;
    }
    out[opos] = '\0';
    return out;
}

/* ==================================================================
   REKURZÍVNE BUDOVANIE BDD  (Shannon expansion + priebežná redukcia)
   ================================================================== */

static BDDNode *build(BDD *bdd, const char *expr,
                      const char *order, int depth)
{
    /* Terminálne podmienky */
    if (strcmp(expr, "0") == 0) return bdd->terminal0;
    if (strcmp(expr, "1") == 0) return bdd->terminal1;
    if (order[depth] == '\0')   return bdd->terminal0; /* nemali by sme tu skončiť */

    char var = order[depth];

    /* Shannon: rozvetvi na var=1 (high) a var=0 (low) */
    char *expr_high = simplify(expr, var, 1);
    char *expr_low  = simplify(expr, var, 0);

    BDDNode *high = build(bdd, expr_high, order, depth + 1);
    BDDNode *low  = build(bdd, expr_low,  order, depth + 1);

    free(expr_high);
    free(expr_low);

    /* -- Redukcia pravidlo 1: Elimination --------------------------
       Ak high == low, uzol nič nerozhoduje -> vráť priamo child.    */
    if (high == low) return high;

    /* -- Redukcia pravidlo 2: Merging (unique table) ---------------
       Ak existuje uzol s rovnakým (var, high, low), zdieľaj ho.    */
    BDDNode *node = ut_get_or_insert(bdd->ut, var, high, low,
                                     &bdd->numNodes);
    return node;
}

/* ==================================================================
   VEREJNÉ FUNKCIE
   ================================================================== */

BDD *BDD_create(const char *expression, const char *variable_order)
{
    BDD *bdd = malloc(sizeof(BDD));

    bdd->varOrder      = malloc(strlen(variable_order) + 1);
    strcpy(bdd->varOrder, variable_order);
    bdd->numVariables  = (int)strlen(variable_order);
    bdd->numNodes      = 0;
    bdd->ut            = ut_create(UT_INIT_SIZE);

    /* Terminálne uzly – uložíme ich priamo, nie do unique tabuľky */
    bdd->terminal0           = malloc(sizeof(BDDNode));
    bdd->terminal0->variable = '\0';
    bdd->terminal0->value    = 0;
    bdd->terminal0->high     = NULL;
    bdd->terminal0->low      = NULL;
    bdd->terminal0->id       = -1;

    bdd->terminal1           = malloc(sizeof(BDDNode));
    bdd->terminal1->variable = '\0';
    bdd->terminal1->value    = 1;
    bdd->terminal1->high     = NULL;
    bdd->terminal1->low      = NULL;
    bdd->terminal1->id       = -2;

    bdd->root = build(bdd, expression, variable_order, 0);

    return bdd;
}

BDD *BDD_create_with_best_order(const char *expression, char *best_order_out)
{
    /* Zistíme premenné z výrazu */
    char vars[64];
    int  nv = 0;
    for (const char *p = expression; *p; p++) {
        if (isupper((unsigned char)*p)) {
            /* pridaj ak ešte nie je */
            int found = 0;
            for (int i = 0; i < nv; i++)
                if (vars[i] == *p) { found = 1; break; }
            if (!found && nv < 63) vars[nv++] = *p;
        }
    }
    vars[nv] = '\0';

    /* Skúšame cyklické rotácie: ABC, BCA, CAB, ...  (≥ N poradí) */
    BDD  *best       = NULL;
    char  best_order[64];
    char  order[64];
 
    for (int rot = 0; rot < nv; rot++) {
        for (int i = 0; i < nv; i++)
            order[i] = vars[(i + rot) % nv];
        order[nv] = '\0';
 
        BDD *candidate = BDD_create(expression, order);
        if (!best || candidate->numNodes < best->numNodes) {
            BDD_free(best);
            best = candidate;
            memcpy(best_order, order, nv + 1);
        } else {
            BDD_free(candidate);
        }
    }
 
    /* Zapíšeme nájdené poradie do výstupného parametra (ak nie je NULL). */
    if (best_order_out)
        memcpy(best_order_out, best_order, nv + 1);
 
    return best;
}

char BDD_use(BDD *bdd, const char *inputs)
{
    if (!bdd || !inputs) return (char)-1;
    if ((int)strlen(inputs) != bdd->numVariables) return (char)-1;

    BDDNode *cur = bdd->root;
    while (cur && cur->variable != '\0') {
        /* Nájdi index premennej v poradí */
        const char *pos = strchr(bdd->varOrder, cur->variable);
        if (!pos) return (char)-1;
        int idx = (int)(pos - bdd->varOrder);
        if (idx >= (int)strlen(inputs)) return (char)-1;

        cur = (inputs[idx] == '1') ? cur->high : cur->low;
    }
    if (!cur) return (char)-1;
    return cur->value ? '1' : '0';
}

void BDD_free(BDD *bdd)
{
    if (!bdd) return;
    ut_free(bdd->ut);        /* uvoľní aj všetky BDDNode-y cez unique table */
    free(bdd->terminal0);
    free(bdd->terminal1);
    free(bdd->varOrder);
    free(bdd);
}
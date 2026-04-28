#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bdd.h"

/* ── Pomocná: vygeneruj všetky kombinácie vstupov a over BDD ────── */
static int test_bdd(BDD *bdd, const char *expr)
{
    int n      = bdd->numVariables;
    int errors = 0;
    int total  = 1 << n;

    char inputs[64];
    inputs[n] = '\0';

    for (int mask = 0; mask < total; mask++) {
        for (int i = 0; i < n; i++)
            inputs[i] = ((mask >> (n - 1 - i)) & 1) ? '1' : '0';

        char got      = BDD_use(bdd, inputs);
        int  expected = eval_expression(expr, bdd->varOrder, inputs);
        char exp_c    = expected ? '1' : '0';

        if (got != exp_c) {
            printf("  CHYBA: inputs=%s  got=%c  expected=%c\n",
                   inputs, got, exp_c);
            errors++;
        }
    }
    return errors;
}

int main(void)
{
    /* ── Test 1: z dokumentácie ─────────────────────────────────── */
    printf("=== Test 1: AB+C ===\n");
    {
        BDD *bdd = BDD_create("A.B+C", "ABC");
        printf("Uzlov: %d\n", bdd->numNodes);
        int e = test_bdd(bdd, "A.B+C");
        printf("Chyby: %d / 8\n\n", e);
        BDD_free(bdd);
    }

    /* ── Test 2: váš príklad z pôvodného kódu ──────────────────── */
    printf("=== Test 2: A.!B.!C+A.B.C+!A.B.!C+!A.!B.C ===\n");
    {
        const char *expr  = "A.!B.!C+A.B.C+!A.B.!C+!A.!B.C";
        const char *order = "ABC";
        BDD *bdd = BDD_create(expr, order);
        printf("Uzlov (ABC): %d\n", bdd->numNodes);
        int e = test_bdd(bdd, expr);
        printf("Chyby: %d / 8\n\n", e);
        BDD_free(bdd);
    }
    /* ── Test 3: BDD_create_with_best_order ─────────────────────── */
    printf("=== Test 3: best order pre A.!B.!C+A.B.C+!A.B.!C+!A.!B.C ===\n");
    {
        const char *expr = "A.!B.!C+A.B.C+!A.B.!C+!A.!B.C";
        BDD *bdd = BDD_create_with_best_order(expr);
        printf("Najlepsie poradie: %s, uzlov: %d\n", bdd->varOrder, bdd->numNodes);
        int e = test_bdd(bdd, expr);
        printf("Chyby: %d / 8\n\n", e);
        BDD_free(bdd);
    }

    /* ── Test 4: väčší výraz (5 premenných) ─────────────────────── */
    printf("=== Test 4: 5 premenných ===\n");
    {
        const char *expr  = "A.B.C+!A.D.E+B.!C.!D+A.!B.E";
        const char *order = "ABCDE";
        BDD *bdd = BDD_create(expr, order);
        printf("Uzlov (ABCDE): %d\n", bdd->numNodes);
        int e = test_bdd(bdd, expr);
        printf("Chyby: %d / 32\n\n", e);
        BDD_free(bdd);
    }

    printf("Hotovo.\n");
    return 0;
}

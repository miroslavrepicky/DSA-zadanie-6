#ifndef BDD_H
#define BDD_H

#include <stddef.h>

/* ──────────────────────────────────────────
   Unique table – hash map (variable, high, low) -> BDDNode*
   ────────────────────────────────────────── */
#define UT_ALPHA_HIGH 0.75
#define UT_ALPHA_LOW  0.20
#define UT_INIT_SIZE  256

typedef struct BDDNode {
    char            variable;   /* premenná tohto uzla, '\0' = terminál */
    int             value;      /* 0 alebo 1, platné iba pre terminál   */
    struct BDDNode *high;       /* hrana pre variable = 1               */
    struct BDDNode *low;        /* hrana pre variable = 0               */
    int             id;         /* unikátne ID (pre ladenie)            */
} BDDNode;

/* jeden bucket v unique tabuľke */
typedef struct UTNode {
    char            variable;
    BDDNode        *high;
    BDDNode        *low;
    BDDNode        *node;       /* uložený uzol                         */
    struct UTNode  *next;
} UTNode;

typedef struct UniqueTable {
    UTNode        **buckets;
    long long       size;
    long long       count;
} UniqueTable;

/* ──────────────────────────────────────────
   BDD štruktúra
   ────────────────────────────────────────── */
typedef struct BDD {
    BDDNode      *root;
    BDDNode      *terminal0;    /* zdieľaný terminálny uzol 0           */
    BDDNode      *terminal1;    /* zdieľaný terminálny uzol 1           */
    int           numVariables;
    int           numNodes;     /* aktuálny počet ne-terminálnych uzlov */
    UniqueTable  *ut;
    char         *varOrder;     /* poradie premenných, napr. "BAC"      */
} BDD;

/* ──────────────────────────────────────────
   Verejné API
   ────────────────────────────────────────── */
BDD  *BDD_create(const char *expression, const char *variable_order);
BDD  *BDD_create_with_best_order(const char *expression, char *best_order_out);
char  BDD_use(BDD *bdd, const char *inputs);
void  BDD_free(BDD *bdd);

/* Vyhodnotenie výrazu priamym dosadením (pre testovanie) */
int   eval_expression(const char *expr, const char *varOrder, const char *inputs);

#endif /* BDD_H */

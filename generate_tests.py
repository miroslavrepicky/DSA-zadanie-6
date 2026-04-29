#!/usr/bin/env python3
"""
Generátor náhodných booleovských funkcií pre testovanie BDD.

Výstup: tests.json
Formát:
{
  "tests": [
    {
      "num_vars": 3,
      "var_order": "ABC",
      "expression": "A.!B+C",
      "truth_table": {"000": 0, "001": 1, ...}   // všetky 2^n kombinácií
    },
    ...
  ]
}

Pre každý počet premenných n ∈ {3, 6, 9, 12, 14} generujeme 100 unikátnych funkcií.
"""

import json
import random
import itertools
from typing import List, Dict, Tuple

SEED = 42
random.seed(SEED)

# Počty premenných, ktoré chceme otestovať
VAR_COUNTS = [3, 6, 9, 12, 14]
FUNCS_PER_SIZE = 100

VARNAMES = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"


# ── Evaluácia výrazu ─────────────────────────────────────────────────────────

def eval_literal(lit: str, assignment: Dict[str, int]) -> int:
    """Vyhodnotí jeden literál ('A', '!B', '1', '0')."""
    if lit == '1':
        return 1
    if lit == '0':
        return 0
    neg = lit.startswith('!')
    var = lit.lstrip('!')
    v = assignment[var]
    return (1 - v) if neg else v


def eval_term(term: str, assignment: Dict[str, int]) -> int:
    """Vyhodnotí jeden term (konjunkcia literálov oddelených '.')."""
    literals = term.split('.')
    for lit in literals:
        if lit == '':
            continue
        if eval_literal(lit, assignment) == 0:
            return 0
    return 1


def eval_expression(expr: str, assignment: Dict[str, int]) -> int:
    """Vyhodnotí celý DNF výraz (termy oddelené '+')."""
    terms = expr.split('+')
    for term in terms:
        if eval_term(term, assignment) == 1:
            return 1
    return 0


# ── Generovanie truth table ──────────────────────────────────────────────────

def compute_truth_table(expr: str, var_order: str) -> Dict[str, int]:
    """Vypočíta truth table pre daný výraz a poradie premenných."""
    n = len(var_order)
    table = {}
    for mask in range(1 << n):
        bits = ""
        assignment = {}
        for i, var in enumerate(var_order):
            bit = (mask >> (n - 1 - i)) & 1
            bits += str(bit)
            assignment[var] = bit
        table[bits] = eval_expression(expr, assignment)
    return table


# ── Generovanie náhodných DNF výrazov ───────────────────────────────────────

def random_literal(var: str, negate_prob: float = 0.4) -> str:
    """Vygeneruje náhodný literál (premenná alebo jej negácia)."""
    if random.random() < negate_prob:
        return f"!{var}"
    return var


def random_term(vars_: List[str], min_lits: int = 1, max_lits: int = None) -> str:
    """Vygeneruje náhodný term (konjunkcia niektorých literálov)."""
    if max_lits is None:
        max_lits = len(vars_)
    # Vyber podmnožinu premenných pre tento term
    k = random.randint(min_lits, max(min_lits, min(max_lits, len(vars_))))
    chosen = random.sample(vars_, k)
    lits = [random_literal(v) for v in sorted(chosen)]  # sort pre konzistentnosť
    return ".".join(lits)


def random_dnf(vars_: List[str], min_terms: int = 1, max_terms: int = None) -> str:
    """Vygeneruje náhodný DNF výraz."""
    if max_terms is None:
        max_terms = max(2, len(vars_))
    t = random.randint(min_terms, max_terms)
    terms = [random_term(vars_, min_lits=1, max_lits=len(vars_)) for _ in range(t)]
    return "+".join(terms)


def truth_table_to_signature(tt: Dict[str, int]) -> str:
    """Prevedie truth table na reťazec pre rýchle porovnanie unikátnosti."""
    keys = sorted(tt.keys())
    return "".join(str(tt[k]) for k in keys)


def is_trivial(tt: Dict[str, int]) -> bool:
    """Vráti True ak je funkcia konštantná (vždy 0 alebo vždy 1)."""
    vals = list(tt.values())
    return all(v == 0 for v in vals) or all(v == 1 for v in vals)


def generate_functions(n: int, count: int) -> List[Dict]:
    """Vygeneruje `count` unikátnych, netriviálnych booleovských funkcií pre n premenných."""
    vars_ = list(VARNAMES[:n])
    var_order = "".join(vars_)

    seen_signatures = set()
    results = []

    attempts = 0
    max_attempts = count * 500  # bezpečnostná poistka

    while len(results) < count and attempts < max_attempts:
        attempts += 1

        # Generujeme DNF s rôznymi parametrami pre rozmanitosť
        max_t = max(2, n)
        expr = random_dnf(vars_, min_terms=1, max_terms=max_t)

        tt = compute_truth_table(expr, var_order)
        sig = truth_table_to_signature(tt)

        if sig in seen_signatures:
            continue  # duplikát funkcie (iný výraz, ale rovnaká pravdivostná tabuľka)
        if is_trivial(tt):
            continue  # konštantná funkcia nie je zaujímavá

        seen_signatures.add(sig)
        results.append({
            "num_vars": n,
            "var_order": var_order,
            "expression": expr,
            "truth_table": tt
        })

    if len(results) < count:
        print(f"  VAROVANIE: Pre n={n} sa podarilo vygenerovať iba {len(results)}/{count} unikátnych funkcií po {attempts} pokusoch.")

    return results


# ── Hlavný blok ──────────────────────────────────────────────────────────────

def main():
    all_tests = []

    for n in VAR_COUNTS:
        print(f"Generujem {FUNCS_PER_SIZE} funkcií pre n={n} premenných...")
        funcs = generate_functions(n, FUNCS_PER_SIZE)
        all_tests.extend(funcs)
        print(f"  -> Vygenerovaných: {len(funcs)}")

    output = {"tests": all_tests}
    with open("tests.json", "w") as f:
        json.dump(output, f, indent=2)

    print(f"\nHotovo. Celkovo {len(all_tests)} testov uložených do tests.json")

    # Rýchla štatistika
    for n in VAR_COUNTS:
        subset = [t for t in all_tests if t["num_vars"] == n]
        print(f"  n={n}: {len(subset)} testov, príklad: {subset[0]['expression']}")


if __name__ == "__main__":
    main()

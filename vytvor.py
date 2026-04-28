"""
Generátor testovacích prípadov pre BDD.

Pre každý počet premenných vygeneruje COUNT unikátnych DNF výrazov,
vyhodnotí ich pre všetky kombinácie vstupov (ground truth),
a uloží do súboru tests.txt.

Formát súboru:
  # komentár
  n_vars=3 expr=A.!B+B.C order=ABC
  000:0 001:1 010:0 011:1 100:0 101:0 110:1 111:1
  (prázdny riadok)
  ...
"""

import itertools
import random
import sys

# ── Konfigurácia ────────────────────────────────────────────────────
VAR_COUNTS = [3, 5, 7, 10, 13, 15]
COUNT      = 100          # unikátnych výrazov na každý počet premenných
MIN_TERMS  = 2
MAX_TERMS  = 4
OUTPUT     = "tests.txt"
SEED       = 42
# ────────────────────────────────────────────────────────────────────

random.seed(SEED)


def gen_dnf(vars_list: list, num_terms: int, lits_per: int) -> str:
    """
    Vygeneruje náhodný DNF výraz.
    vars_list: zoznam premenných, napr. ['A','B','C']
    num_terms:  počet termov
    lits_per:   počet literálov v každom terme
    """
    terms = []
    for _ in range(num_terms):
        chosen = random.sample(vars_list, min(lits_per, len(vars_list)))
        lits = []
        for v in chosen:
            neg = random.randint(0, 1)
            lits.append(("!" if neg else "") + v)
        terms.append(".".join(lits))
    return "+".join(terms)


def eval_literal(lit: str, assignment: dict) -> bool:
    """Vyhodnotí jeden literál (napr. '!A' alebo 'B')."""
    if lit.startswith("!"):
        return not assignment[lit[1:]]
    return assignment[lit]


def eval_term(term: str, assignment: dict) -> bool:
    """Vyhodnotí jeden term (AND literálov)."""
    lits = term.split(".")
    return all(eval_literal(l, assignment) for l in lits)


def eval_expr(expr: str, assignment: dict) -> bool:
    """Vyhodnotí celý DNF výraz (OR termov)."""
    terms = expr.split("+")
    return any(eval_term(t, assignment) for t in terms)


def compute_truth_table(expr: str, vars_list: list) -> dict:
    """
    Vráti slovník { '000': '0', '001': '1', ... }
    pre všetky 2^n kombinácií.
    """
    n = len(vars_list)
    table = {}
    for bits in itertools.product([0, 1], repeat=n):
        assignment = {vars_list[i]: bool(bits[i]) for i in range(n)}
        key = "".join(str(b) for b in bits)
        val = "1" if eval_expr(expr, assignment) else "0"
        table[key] = val
    return table


def is_trivial(table: dict) -> bool:
    """Vráti True ak výraz je konštantne 0 alebo konštantne 1."""
    vals = set(table.values())
    return len(vals) == 1


def main():
    total_generated = 0
    total_skipped   = 0

    with open(OUTPUT, "w") as f:
        f.write("# Testovaci subor pre BDD\n")
        f.write("# Format:\n")
        f.write("#   n_vars=N expr=VYRAZ order=PORADIE\n")
        f.write("#   VSTUPY:VYSLEDKY  (napr. 000:0 001:1 ...)\n")
        f.write("#\n\n")

        for n in VAR_COUNTS:
            vars_list = [chr(ord('A') + i) for i in range(n)]
            order     = "".join(vars_list)
            lits_per  = max(1, n // 2)

            seen_exprs = set()   # unikátnosť zaručená cez Python set
            count      = 0
            attempts   = 0
            max_attempts = COUNT * 200

            print(f"Generujem n={n} ...", end=" ", flush=True)

            while count < COUNT and attempts < max_attempts:
                attempts += 1
                num_terms = random.randint(MIN_TERMS, MAX_TERMS)
                expr      = gen_dnf(vars_list, num_terms, lits_per)

                # Unikátnosť – Python set zaručuje 100% bez kolízií
                if expr in seen_exprs:
                    total_skipped += 1
                    continue

                table = compute_truth_table(expr, vars_list)

                # Preskočíme triviálne výrazy (konštantne 0 alebo 1)
                if is_trivial(table):
                    total_skipped += 1
                    continue

                seen_exprs.add(expr)
                count += 1
                total_generated += 1

                # Zapíšeme hlavičku
                f.write(f"n_vars={n} expr={expr} order={order}\n")

                # Zapíšeme pravdivostnú tabuľku na jeden riadok
                entries = " ".join(f"{k}:{v}" for k, v in sorted(table.items()))
                f.write(entries + "\n")
                f.write("\n")

            if count < COUNT:
                print(f"VAROVANIE: vygenerovaných iba {count}/{COUNT} "
                      f"unikátnych výrazov (priestor vyčerpaný)")
            else:
                print(f"OK  ({count} výrazov, {attempts} pokusov, "
                      f"{attempts - count} preskočených)")

    print(f"\nCelkom vygenerovaných: {total_generated}")
    print(f"Celkom preskočených:   {total_skipped}")
    print(f"Uložené do: {OUTPUT}")


if __name__ == "__main__":
    main()
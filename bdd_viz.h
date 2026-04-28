#ifndef BDD_VIZ_H
#define BDD_VIZ_H

#include "bdd.h"

/*
 * BDD_export_dot
 * --------------
 * Zapíše Graphviz DOT súbor do `filename`.
 * Skompiluj diagram príkazom:
 *   dot -Tpng diagram.dot -o diagram.png
 *   dot -Tsvg diagram.dot -o diagram.svg
 *
 * Vracia 0 pri úspechu, -1 pri chybe súboru.
 */
int BDD_export_dot(const BDD *bdd, const char *filename);

/*
 * BDD_export_html
 * ---------------
 * Vygeneruje samostatný HTML súbor s interaktívnym SVG diagramom.
 * Nevyžaduje žiadne externé knižnice – všetko je inline JS + SVG.
 * Otvor výsledný súbor v ľubovoľnom prehliadači.
 *
 * Vracia 0 pri úspechu, -1 pri chybe súboru.
 */
int BDD_export_html(const BDD *bdd, const char *filename);

#endif /* BDD_VIZ_H */

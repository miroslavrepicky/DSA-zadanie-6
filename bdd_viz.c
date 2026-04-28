#include "bdd_viz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ══════════════════════════════════════════════════════════════════
   Pomocné: rekurzívny priechod cez BDD (každý uzol max raz)
   Používame jednoduchý dynamický zoznam navštívených pointerov.
   ══════════════════════════════════════════════════════════════════ */

typedef struct {
    const BDDNode **data;
    int             size;
    int             cap;
} NodeSet;

static void ns_init(NodeSet *s) {
    s->cap  = 64;
    s->size = 0;
    s->data = malloc(s->cap * sizeof(BDDNode *));
}

static void ns_free(NodeSet *s) { free(s->data); }

static int ns_contains(const NodeSet *s, const BDDNode *n) {
    printf("DNU NS CONTAINS\n");
    for (int i = 0; i < s->size; i++){
        printf("DNU NS IF\n");
        if (s->data[i] == n){
            printf("vraciam1\n");
            return 1;
        } 
    }
    printf("vraciam0");
    return 0;
}

static void ns_add(NodeSet *s, const BDDNode *n) {
    if (s->size == s->cap) {
        s->cap *= 2;
        s->data = realloc(s->data, s->cap * sizeof(BDDNode *));
    }
    s->data[s->size++] = n;
}

/* Zbierame uzly do poľa v BFS poradí (level po level). */
typedef struct {
    const BDDNode **data;
    int             size;
    int             cap;
} NodeList;

static void nl_init(NodeList *l) {
    l->cap  = 64;
    l->size = 0;
    l->data = malloc(l->cap * sizeof(BDDNode *));
}

static void nl_free(NodeList *l) { free(l->data); }

static void nl_push(NodeList *l, const BDDNode *n) {
    if (l->size == l->cap) {
        l->cap *= 2;
        l->data = realloc(l->data, l->cap * sizeof(BDDNode *));
    }
    l->data[l->size++] = n;
}

/* BFS zbieranie všetkých uzlov (vrátane terminálov) */
static void collect_nodes(const BDD *bdd, NodeList *out) {
    NodeSet visited;
    ns_init(&visited);

    /* BFS fronta */
    NodeList queue;
    nl_init(&queue);

    int head = 0;
    nl_push(&queue, bdd->root);
    ns_add(&visited, bdd->root);
    while (head < queue.size) {
        const BDDNode *cur = queue.data[head++];
        nl_push(out, cur);
        if (cur->variable != '\0') {
            /* interný uzol – pridaj deti ak ešte neboli */
            if (!ns_contains(&visited, cur->high)) {
                ns_add(&visited, cur->high);
                nl_push(&queue, cur->high);
            }
            if (!ns_contains(&visited, cur->low)) {
                ns_add(&visited, cur->low);
                nl_push(&queue, cur->low);

            }
        }
    };

    nl_free(&queue);
    ns_free(&visited);
}

/* Vráti stabilné číslo uzla (index v BFS zozname). */
static int node_index(const NodeList *l, const BDDNode *n) {
    for (int i = 0; i < l->size; i++)
        if (l->data[i] == n) return i;
    return -1;
}

/* ══════════════════════════════════════════════════════════════════
   DOT EXPORT
   ══════════════════════════════════════════════════════════════════ */

int BDD_export_dot(const BDD *bdd, const char *filename)
{
    if (!bdd || !filename) return -1;

    FILE *f = fopen(filename, "w");


    if (!f) { perror("BDD_export_dot: fopen"); return -1; }

    NodeList nodes;

    nl_init(&nodes);

    collect_nodes(bdd, &nodes);


    fprintf(f, "digraph BDD {\n");
    fprintf(f, "    graph [rankdir=TB, fontname=\"Helvetica\", splines=true];\n");
    fprintf(f, "    node  [fontname=\"Helvetica\", fontsize=12];\n");
    fprintf(f, "    edge  [fontname=\"Helvetica\", fontsize=10];\n\n");
    /* ── Rank grouping: uzly rovnakej premennej na rovnakej úrovni ── */
    for (int d = 0; d < bdd->numVariables; d++) {
        char var = bdd->varOrder[d];
        int found = 0;
        for (int i = 0; i < nodes.size; i++) {
            if (nodes.data[i]->variable == var) {
                if (!found) { fprintf(f, "    { rank=same;"); found = 1; }
                fprintf(f, " n%d;", i);
            }
        }
        if (found) fprintf(f, " }\n");
    }
    /* terminály na posledný rank */
    fprintf(f, "    { rank=same;");
    for (int i = 0; i < nodes.size; i++)
        if (nodes.data[i]->variable == '\0') fprintf(f, " n%d;", i);
    fprintf(f, " }\n\n");

    /* ── Definície uzlov ── */
    for (int i = 0; i < nodes.size; i++) {
        const BDDNode *n = nodes.data[i];
        if (n->variable == '\0') {
            /* terminál */
            fprintf(f, "    n%d [label=\"%d\", shape=square,"
                       " style=filled, fillcolor=\"%s\","
                       " width=0.4, height=0.4];\n",
                    i,
                    n->value,
                    n->value ? "#90ee90" : "#ff9999");
        } else {
            /* koreň = dvojitý kruh */
            const char *shape = (n == bdd->root) ? "doublecircle" : "circle";
            fprintf(f, "    n%d [label=\"%c\", shape=%s,"
                       " style=filled, fillcolor=\"#d0e8ff\"];\n",
                    i, n->variable, shape);
        }
    }
    fprintf(f, "\n");
    

    /* ── Hrany ── */
    for (int i = 0; i < nodes.size; i++) {
        const BDDNode *n = nodes.data[i];
        if (n->variable == '\0') continue;

        int hi = node_index(&nodes, n->high);
        int lo = node_index(&nodes, n->low);

        /* high hrana – plná čiara, label "1" */
        fprintf(f, "    n%d -> n%d [label=\"1\", style=solid];\n",   i, hi);
        /* low  hrana – prerušovaná čiara, label "0" */
        fprintf(f, "    n%d -> n%d [label=\"0\", style=dashed];\n",  i, lo);
    }
    

    fprintf(f, "}\n");
    fclose(f);
    nl_free(&nodes);
    
    return 0;
}

/* ══════════════════════════════════════════════════════════════════
   HTML EXPORT  (inline JavaScript Sugiyama-style layout + SVG)
   ══════════════════════════════════════════════════════════════════ */

/* Každý uzol sa serializuje do JSON poľa, JavaScript ho na strane
   prehliadača rozloží do vrstiev a vykreslí ako SVG.               */

int BDD_export_html(const BDD *bdd, const char *filename)
{
    if (!bdd || !filename) return -1;

    FILE *f = fopen(filename, "w");
    if (!f) { perror("BDD_export_html: fopen"); return -1; }

    NodeList nodes;
    nl_init(&nodes);
    collect_nodes(bdd, &nodes);

    /* ── JSON dáta ── */
    /* nodes: [{id, label, terminal, value, high, low, isRoot}] */

    fprintf(f,
"<!DOCTYPE html>\n"
"<html lang=\"sk\">\n"
"<head>\n"
"<meta charset=\"UTF-8\">\n"
"<title>BDD diagram</title>\n"
"<style>\n"
"  * { box-sizing: border-box; margin: 0; padding: 0; }\n"
"  body { font-family: 'Segoe UI', Helvetica, Arial, sans-serif;\n"
"         background: #f5f7fa; display: flex; flex-direction: column;\n"
"         align-items: center; padding: 24px 16px; min-height: 100vh; }\n"
"  h1   { font-size: 18px; font-weight: 600; color: #1a1a2e;\n"
"         margin-bottom: 6px; }\n"
"  .sub { font-size: 13px; color: #666; margin-bottom: 20px; }\n"
"  #canvas-wrap { background: #fff; border: 1px solid #dde3ee;\n"
"                 border-radius: 12px; overflow: auto;\n"
"                 box-shadow: 0 2px 12px rgba(0,0,0,.08); }\n"
"  svg  { display: block; }\n"
"  .legend { display: flex; gap: 20px; margin-top: 16px;\n"
"            font-size: 12px; color: #555; align-items: center; }\n"
"  .leg-item { display: flex; align-items: center; gap: 6px; }\n"
"  .leg-dot  { width: 14px; height: 14px; border-radius: 3px;\n"
"              border: 1.5px solid #999; }\n"
"  .info { margin-top: 14px; font-size: 12px; color: #888; }\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<h1>BDD diagram</h1>\n"
"<p class=\"sub\">Poradie premenných: <strong>%s</strong>"
"  &nbsp;|&nbsp; Uzlov (bez terminálov): <strong>%d</strong></p>\n"
"<div id=\"canvas-wrap\"><svg id=\"svg\"></svg></div>\n"
"<div class=\"legend\">\n"
"  <div class=\"leg-item\"><div class=\"leg-dot\" style=\"background:#d0e8ff\"></div> interný uzol</div>\n"
"  <div class=\"leg-item\"><div class=\"leg-dot\" style=\"background:#90ee90\"></div> terminál 1</div>\n"
"  <div class=\"leg-item\"><div class=\"leg-dot\" style=\"background:#ff9999\"></div> terminál 0</div>\n"
"  <div class=\"leg-item\"><svg width=\"32\" height=\"14\"><line x1=\"0\" y1=\"7\" x2=\"32\" y2=\"7\" stroke=\"#333\" stroke-width=\"1.5\"/><polygon points=\"28,4 32,7 28,10\" fill=\"#333\"/></svg> hrana 1</div>\n"
"  <div class=\"leg-item\"><svg width=\"32\" height=\"14\"><line x1=\"0\" y1=\"7\" x2=\"32\" y2=\"7\" stroke=\"#333\" stroke-width=\"1.5\" stroke-dasharray=\"4,3\"/><polygon points=\"28,4 32,7 28,10\" fill=\"#333\"/></svg> hrana 0</div>\n"
"</div>\n"
"<p class=\"info\">Plná hrana = premenná je 1 &nbsp;|&nbsp; Prerušovaná hrana = premenná je 0</p>\n"
"\n"
"<script>\n"
"const NODES = [\n",
        bdd->varOrder,
        bdd->numNodes);

    for (int i = 0; i < nodes.size; i++) {
        const BDDNode *n = nodes.data[i];
        int isTerminal = (n->variable == '\0');
        int hi = isTerminal ? -1 : node_index(&nodes, n->high);
        int lo = isTerminal ? -1 : node_index(&nodes, n->low);
        int isRoot = (n == bdd->root) ? 1 : 0;

        fprintf(f,
            "  {id:%d, label:\"%s\", terminal:%s, value:%d,"
            " high:%d, low:%d, isRoot:%d}%s\n",
            i,
            isTerminal ? (n->value ? "1" : "0")
                       : (char[]){n->variable, '\0'},
            isTerminal ? "true" : "false",
            isTerminal ? n->value : -1,
            hi, lo,
            isRoot,
            (i < nodes.size - 1) ? "," : "");
    }

    fprintf(f,
"];\n"
"\n"
"/* ── Rozloženie do vrstiev (level = hĺbka v BFS) ── */\n"
"(function layout() {\n"
"  const N = NODES.length;\n"
"\n"
"  /* priradíme vrstvu každému uzlu */\n"
"  const level = new Array(N).fill(-1);\n"
"  const root  = NODES.findIndex(n => n.isRoot);\n"
"  const queue = [root];\n"
"  level[root] = 0;\n"
"  while (queue.length) {\n"
"    const cur = queue.shift();\n"
"    const n   = NODES[cur];\n"
"    if (n.terminal) continue;\n"
"    for (const child of [n.high, n.low]) {\n"
"      if (child >= 0 && level[child] < 0) {\n"
"        level[child] = level[cur] + 1;\n"
"        queue.push(child);\n"
"      }\n"
"    }\n"
"  }\n"
"  /* terminály vždy na najnižšiu vrstvu */\n"
"  const maxLvl = Math.max(...level.filter(l => l >= 0));\n"
"  NODES.forEach((n, i) => { if (n.terminal) level[i] = maxLvl + 1; });\n"
"\n"
"  /* zoskupíme podľa vrstiev */\n"
"  const layers = [];\n"
"  for (let i = 0; i <= maxLvl + 1; i++) layers.push([]);\n"
"  NODES.forEach((_, i) => { if (level[i] >= 0) layers[level[i]].push(i); });\n"
"\n"
"  /* ── Geometria ── */\n"
"  const R_INT  = 26;   /* polomer interného uzla          */\n"
"  const R_TERM = 20;   /* polomer terminálu               */\n"
"  const HGAP   = 80;   /* horizontálna medzera medzi uzlami */\n"
"  const VGAP   = 100;  /* vertikálna medzera medzi vrstvami */\n"
"  const PAD    = 50;\n"
"\n"
"  /* šírka vrstvy */\n"
"  const layerW = layers.map(l => {\n"
"    const r = l.some(i => !NODES[i].terminal) ? R_INT : R_TERM;\n"
"    return l.length * (2 * r) + (l.length - 1) * HGAP;\n"
"  });\n"
"  const totalW  = Math.max(...layerW) + 2 * PAD;\n"
"  const totalH  = (layers.length) * (2 * R_INT + VGAP) + PAD;\n"
"\n"
"  /* súradnice uzlov */\n"
"  const x = new Array(N), y = new Array(N);\n"
"  layers.forEach((layer, li) => {\n"
"    const r  = layer.some(i => !NODES[i].terminal) ? R_INT : R_TERM;\n"
"    const lw = layerW[li];\n"
"    const ox = (totalW - lw) / 2;\n"
"    const cy = PAD + li * (2 * R_INT + VGAP) + r;\n"
"    layer.forEach((ni, pos) => {\n"
"      x[ni] = ox + pos * (2 * r + HGAP) + r;\n"
"      y[ni] = cy;\n"
"    });\n"
"  });\n"
"\n"
"  /* ── SVG rendering ── */\n"
"  const svg = document.getElementById('svg');\n"
"  svg.setAttribute('width',  totalW);\n"
"  svg.setAttribute('height', totalH);\n"
"  svg.setAttribute('viewBox', `0 0 ${totalW} ${totalH}`);\n"
"\n"
"  const ns = 'http://www.w3.org/2000/svg';\n"
"  function el(tag, attrs, txt) {\n"
"    const e = document.createElementNS(ns, tag);\n"
"    Object.entries(attrs).forEach(([k,v]) => e.setAttribute(k, v));\n"
"    if (txt !== undefined) e.textContent = txt;\n"
"    return e;\n"
"  }\n"
"\n"
"  /* defs – markery pre hrany */\n"
"  const defs = el('defs', {});\n"
"  function arrowMarker(id, color, dash) {\n"
"    const m = el('marker', {id, viewBox:'0 0 10 10', refX:'9', refY:'5',\n"
"                 markerWidth:'7', markerHeight:'7', orient:'auto-start-reverse'});\n"
"    m.appendChild(el('path', {d:'M1 1L9 5L1 9', fill:'none',\n"
"                              stroke: color, 'stroke-width':'1.5',\n"
"                              'stroke-linecap':'round','stroke-linejoin':'round'}));\n"
"    return m;\n"
"  }\n"
"  defs.appendChild(arrowMarker('ah',  '#3a6ea5', false));\n"
"  defs.appendChild(arrowMarker('ahd', '#888',    true));\n"
"  svg.appendChild(defs);\n"
"\n"
"  /* vrstva hrán (pod uzlami) */\n"
"  const edgeLayer = el('g', {});\n"
"  svg.appendChild(edgeLayer);\n"
"  /* vrstva uzlov */\n"
"  const nodeLayer = el('g', {});\n"
"  svg.appendChild(nodeLayer);\n"
"\n"
"  /* hrany */\n"
"  NODES.forEach((n, i) => {\n"
"    if (n.terminal) return;\n"
"    [[n.high, true], [n.low, false]].forEach(([ci, isHigh]) => {\n"
"      if (ci < 0) return;\n"
"      const r1 = R_INT, r2 = NODES[ci].terminal ? R_TERM : R_INT;\n"
"      /* vypočíta bod na obvode uzla smerom k cieľu */\n"
"      const dx = x[ci] - x[i], dy = y[ci] - y[i];\n"
"      const dist = Math.sqrt(dx*dx + dy*dy) || 1;\n"
"      const sx = x[i]  + dx / dist * r1;\n"
"      const sy = y[i]  + dy / dist * r1;\n"
"      const ex = x[ci] - dx / dist * r2;\n"
"      const ey = y[ci] - dy / dist * r2;\n"
"\n"
"      /* Bezier ohyb pre prehľadnosť ak sú uzly na rovnakej úrovni */\n"
"      let pathD;\n"
"      if (Math.abs(sy - ey) < 10) {\n"
"        const mx = (sx + ex) / 2, my = sy - 60;\n"
"        pathD = `M${sx},${sy} Q${mx},${my} ${ex},${ey}`;\n"
"      } else {\n"
"        pathD = `M${sx},${sy} L${ex},${ey}`;\n"
"      }\n"
"\n"
"      const color  = isHigh ? '#3a6ea5' : '#888';\n"
"      const marker = isHigh ? 'url(#ah)' : 'url(#ahd)';\n"
"      const dash   = isHigh ? '' : '6,4';\n"
"      const p = el('path', {\n"
"        d: pathD, fill: 'none', stroke: color,\n"
"        'stroke-width': '1.6',\n"
"        'stroke-dasharray': dash,\n"
"        'marker-end': marker\n"
"      });\n"
"      edgeLayer.appendChild(p);\n"
"\n"
"      /* label hrany – pri strede hrany */\n"
"      const lx = (sx + ex) / 2, ly = (sy + ey) / 2 - 6;\n"
"      const lt = el('text', {\n"
"        x: lx, y: ly, 'text-anchor':'middle',\n"
"        'font-size':'11', fill: color, 'font-weight':'600'\n"
"      }, isHigh ? '1' : '0');\n"
"      edgeLayer.appendChild(lt);\n"
"    });\n"
"  });\n"
"\n"
"  /* uzly */\n"
"  NODES.forEach((n, i) => {\n"
"    const g   = el('g', {class:'bdd-node', style:'cursor:default'});\n"
"    const r   = n.terminal ? R_TERM : R_INT;\n"
"    const fill = n.terminal\n"
"                 ? (n.value ? '#90ee90' : '#ff9999')\n"
"                 : '#d0e8ff';\n"
"    const stroke = n.isRoot ? '#1a6fb5' : '#6699cc';\n"
"    const sw     = n.isRoot ? '2.5' : '1.5';\n"
"\n"
"    if (n.terminal) {\n"
"      /* terminál = zaoblený štvorec */\n"
"      g.appendChild(el('rect', {\n"
"        x: x[i]-r, y: y[i]-r, width:2*r, height:2*r,\n"
"        rx:'6', fill, stroke, 'stroke-width': sw\n"
"      }));\n"
"    } else {\n"
"      g.appendChild(el('circle', {\n"
"        cx: x[i], cy: y[i], r,\n"
"        fill, stroke, 'stroke-width': sw\n"
"      }));\n"
"      if (n.isRoot) {\n"
"        /* dvojitý kruh pre koreň */\n"
"        g.appendChild(el('circle', {\n"
"          cx: x[i], cy: y[i], r: r - 5,\n"
"          fill:'none', stroke: '#1a6fb5', 'stroke-width':'1'\n"
"        }));\n"
"      }\n"
"    }\n"
"\n"
"    g.appendChild(el('text', {\n"
"      x: x[i], y: y[i],\n"
"      'text-anchor':'middle', 'dominant-baseline':'central',\n"
"      'font-size': n.terminal ? '14' : '15',\n"
"      'font-weight':'600', fill:'#1a1a2e'\n"
"    }, n.label));\n"
"\n"
"    nodeLayer.appendChild(g);\n"
"  });\n"
"\n"
"  document.getElementById('canvas-wrap').style.maxWidth = totalW + 'px';\n"
"})();\n"
"</script>\n"
"</body>\n"
"</html>\n");

    fclose(f);
    nl_free(&nodes);
    return 0;
}

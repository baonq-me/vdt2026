#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

/* Generate a random int across the full [INT_MIN, INT_MAX] range */
int rand_int(void) {
    /* Combine two rand() calls to fill ~32 bits, then cast to int.
     * The sign bit gets random 0/1, covering negative values too. */
    return (int)(((unsigned int)rand() << 16) | (unsigned int)rand());
}

/* ---------- multiply C = A x B ----------
 * The loop order changes only HOW the same multiply-accumulate operations
 * are scheduled, not the result. It strongly affects memory access patterns
 * and therefore CPU cache behaviour:
 *   - inner loop over j  -> sequential access of a[i][k] (scalar), b[k][j] and
 *                           c[i][j] along rows  => cache-friendly (e.g. ikj, kij)
 *   - inner loop over i  -> strided access of a[i][k] and c[i][j] down columns
 *                           => many cache misses (e.g. jki, kji)
 *
 * Pulled out of main() into its own function so that `perf` samples it as a
 * separate frame stacked on top of main in the flame graph.
 */
static void multiply(int **a, int **b, int **c,
                     int n, int m, int q, const char *order) {
    /* The MAC operation is identical in every permutation. */
#define MAC() c[i][j] += a[i][k] * b[k][j]

    if (strcmp(order, "ijk") == 0) {
        for (int i = 0; i < n; i++)
            for (int j = 0; j < q; j++)
                for (int k = 0; k < m; k++)
                    MAC();
    } else if (strcmp(order, "ikj") == 0) {
        for (int i = 0; i < n; i++)
            for (int k = 0; k < m; k++)
                for (int j = 0; j < q; j++)
                    MAC();
    } else if (strcmp(order, "jik") == 0) {
        for (int j = 0; j < q; j++)
            for (int i = 0; i < n; i++)
                for (int k = 0; k < m; k++)
                    MAC();
    } else if (strcmp(order, "jki") == 0) {
        for (int j = 0; j < q; j++)
            for (int k = 0; k < m; k++)
                for (int i = 0; i < n; i++)
                    MAC();
    } else if (strcmp(order, "kij") == 0) {
        for (int k = 0; k < m; k++)
            for (int i = 0; i < n; i++)
                for (int j = 0; j < q; j++)
                    MAC();
    } else { /* kji */
        for (int k = 0; k < m; k++)
            for (int j = 0; j < q; j++)
                for (int i = 0; i < n; i++)
                    MAC();
    }

#undef MAC
}

int main(int argc, char *argv[]) {
    int silent = 0;
    const char *order = "ijk";  /* default loop order */
    int dims[3];
    int dim_idx = 0;

    /* Parse arguments: -s/--silent flag, -o/--order <order>, and 3 dimensions */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--silent") == 0) {
            silent = 1;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--order") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: option %s requires an argument.\n", argv[i]);
                return 1;
            }
            order = argv[++i];  /* consume the next argument as the loop order */
        } else {
            if (dim_idx < 3) {
                dims[dim_idx++] = atoi(argv[i]);
            }
        }
    }

    /* Validate the chosen loop order (one of the 6 permutations of i, j, k) */
    if (strcmp(order, "ijk") != 0 && strcmp(order, "ikj") != 0 &&
        strcmp(order, "jik") != 0 && strcmp(order, "jki") != 0 &&
        strcmp(order, "kij") != 0 && strcmp(order, "kji") != 0) {
        fprintf(stderr, "Error: invalid loop order '%s'.\n", order);
        fprintf(stderr, "  Valid orders: ijk, ikj, jik, jki, kij, kji\n");
        return 1;
    }

    if (dim_idx != 3) {
        fprintf(stderr, "Usage: %s [-s|--silent] [-o|--order <order>] <n> <m> <q>\n", argv[0]);
        fprintf(stderr, "  -s, --silent          suppress all output (use with `time` to benchmark)\n");
        fprintf(stderr, "  -o, --order <order>   loop order, one of: ijk, ikj, jik, jki, kij, kji (default: ijk)\n");
        fprintf(stderr, "                        try `ikj` (fast) vs `jki` (slow) to observe CPU cache effects\n");
        fprintf(stderr, "  n = rows of A, rows of C\n");
        fprintf(stderr, "  m = cols of A, rows of B\n");
        fprintf(stderr, "  q = cols of B, cols of C\n");
        return 1;
    }

    int n = dims[0];
    int m = dims[1];
    int q = dims[2];

    if (n <= 0 || m <= 0 || q <= 0) {
        fprintf(stderr, "Error: dimensions must be positive integers.\n");
        return 1;
    }

    srand(time(NULL));

    /* ---------- allocate matrix A (n x m) ---------- */
    int **a = malloc(n * sizeof(int *));
    if (!a) { fprintf(stderr, "Memory error\n"); return 1; }
    for (int i = 0; i < n; i++) {
        a[i] = malloc(m * sizeof(int));
        if (!a[i]) { fprintf(stderr, "Memory error\n"); return 1; }
    }

    /* ---------- allocate matrix B (m x q) ---------- */
    int **b = malloc(m * sizeof(int *));
    if (!b) { fprintf(stderr, "Memory error\n"); return 1; }
    for (int i = 0; i < m; i++) {
        b[i] = malloc(q * sizeof(int));
        if (!b[i]) { fprintf(stderr, "Memory error\n"); return 1; }
    }

    /* ---------- allocate matrix C (n x q) ---------- */
    int **c = malloc(n * sizeof(int *));
    if (!c) { fprintf(stderr, "Memory error\n"); return 1; }
    for (int i = 0; i < n; i++) {
        c[i] = calloc(q, sizeof(int));  /* zero-initialized */
        if (!c[i]) { fprintf(stderr, "Memory error\n"); return 1; }
    }

    /* ---------- fill & print A ---------- */
    if (!silent) printf("Matrix A (%d x %d):\n", n, m);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < m; j++) {
            a[i][j] = rand_int();
            if (!silent) printf("%12d ", a[i][j]);
        }
        if (!silent) printf("\n");
    }

    /* ---------- fill & print B ---------- */
    if (!silent) printf("\nMatrix B (%d x %d):\n", m, q);
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < q; j++) {
            b[i][j] = rand_int();
            if (!silent) printf("%12d ", b[i][j]);
        }
        if (!silent) printf("\n");
    }

    /* ---------- multiply C = A x B ---------- */
    if (!silent) printf("\nLoop order: %s\n", order);

    multiply(a, b, c, n, m, q, order);

    /* ---------- print C ---------- */
    if (!silent) printf("\nResult C = A x B (%d x %d):\n", n, q);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < q; j++) {
            if (!silent) printf("%12d ", c[i][j]);
        }
        if (!silent) printf("\n");
    }

    /* ---------- free memory ---------- */
    for (int i = 0; i < n; i++) free(a[i]);
    for (int i = 0; i < m; i++) free(b[i]);
    for (int i = 0; i < n; i++) free(c[i]);
    free(a);
    free(b);
    free(c);

    return 0;
}

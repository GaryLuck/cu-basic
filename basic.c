/*
 * Tiny BASIC Interpreter - C implementation for MSVC
 * Supports: PRINT, LET, GOTO, IF, END, DIM
 * Variables A-Z, integer arithmetic, LOAD, SAVE, RUN, LIST, NEW, QUIT
 */

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINES      1000
#define MAX_LINELEN    256
#define NUM_VARS       26

/* Program line: line number + source text */
typedef struct {
    int num;
    char text[MAX_LINELEN];
} Line;

static Line program[MAX_LINES];
static int num_program_lines = 0;

/* Variables A-Z (index 0 = A, 25 = Z) */
static int vars[NUM_VARS];

/* Arrays: ptr to data, size (0 = not dimensioned) */
static int* arrays[NUM_VARS];
static int array_sizes[NUM_VARS];

/* Runtime: current line index when running */
static int run_index = -1;
static int run_mode = 0;  /* 0 = idle, 1 = running */

/* Parser state for current line */
static const char* parse_ptr = NULL;

static void skip_spaces(void) {
    while (*parse_ptr == ' ' || *parse_ptr == '\t') parse_ptr++;
}

static int parse_number(int* out) {
    skip_spaces();
    if (!isdigit((unsigned char)*parse_ptr)) return -1;
    *out = 0;
    while (isdigit((unsigned char)*parse_ptr)) {
        *out = *out * 10 + (*parse_ptr - '0');
        parse_ptr++;
    }
    return 0;
}

/* Parse a variable name (single letter A-Z), return index 0-25 or -1 */
static int parse_var(void) {
    skip_spaces();
    if (*parse_ptr >= 'A' && *parse_ptr <= 'Z') {
        int i = *parse_ptr - 'A';
        parse_ptr++;
        return i;
    }
    return -1;
}

/* Evaluate expression: integers, variables, array refs, + - * / ( ) */
static int eval_expr(void);

static int eval_primary(void) {
    skip_spaces();
    if (*parse_ptr == '(') {
        parse_ptr++;
        int v = eval_expr();
        skip_spaces();
        if (*parse_ptr == ')') parse_ptr++;
        return v;
    }
    if (*parse_ptr == '-') {
        parse_ptr++;
        return -eval_primary();
    }
    if (isdigit((unsigned char)*parse_ptr)) {
        int n = 0;
        while (isdigit((unsigned char)*parse_ptr)) {
            n = n * 10 + (*parse_ptr - '0');
            parse_ptr++;
        }
        return n;
    }
    /* Variable or array */
    int vi = parse_var();
    if (vi < 0) return 0;
    skip_spaces();
    if (*parse_ptr == '(') {
        parse_ptr++;
        int idx = eval_expr();
        skip_spaces();
        if (*parse_ptr == ')') parse_ptr++;
        if (arrays[vi] && idx >= 0 && idx < array_sizes[vi])
            return arrays[vi][idx];
        return 0;
    }
    return vars[vi];
}

static int eval_term(void) {
    int v = eval_primary();
    skip_spaces();
    for (;;) {
        if (*parse_ptr == '*') {
            parse_ptr++;
            v *= eval_primary();
        } else if (*parse_ptr == '/') {
            parse_ptr++;
            int r = eval_primary();
            v = (r != 0) ? v / r : 0;
        } else break;
        skip_spaces();
    }
    return v;
}

static int eval_expr(void) {
    int v = eval_term();
    skip_spaces();
    for (;;) {
        if (*parse_ptr == '+') {
            parse_ptr++;
            v += eval_term();
        } else if (*parse_ptr == '-') {
            parse_ptr++;
            v -= eval_term();
        } else break;
        skip_spaces();
    }
    return v;
}

/* Compare: =, <>, <, >, <=, >= */
enum { CMP_EQ, CMP_NE, CMP_LT, CMP_GT, CMP_LE, CMP_GE };

static int parse_compare(int* cmp) {
    skip_spaces();
    if (parse_ptr[0] == '=' && parse_ptr[1] != '=') { *cmp = CMP_EQ; parse_ptr += 1; return 0; }
    if (parse_ptr[0] == '<' && parse_ptr[1] == '>') { *cmp = CMP_NE; parse_ptr += 2; return 0; }
    if (parse_ptr[0] == '<' && parse_ptr[1] == '=') { *cmp = CMP_LE; parse_ptr += 2; return 0; }
    if (parse_ptr[0] == '>' && parse_ptr[1] == '=') { *cmp = CMP_GE; parse_ptr += 2; return 0; }
    if (parse_ptr[0] == '<') { *cmp = CMP_LT; parse_ptr += 1; return 0; }
    if (parse_ptr[0] == '>') { *cmp = CMP_GT; parse_ptr += 1; return 0; }
    return -1;
}

static int eval_condition(void) {
    int left = eval_expr();
    int cmp;
    if (parse_compare(&cmp) != 0) return 0;
    int right = eval_expr();
    switch (cmp) {
        case CMP_EQ: return left == right;
        case CMP_NE: return left != right;
        case CMP_LT: return left < right;
        case CMP_GT: return left > right;
        case CMP_LE: return left <= right;
        case CMP_GE: return left >= right;
        default: return 0;
    }
}

/* Execute one program line; text = line source. Returns next line index or -1 to stop. */
static int execute_line_text(const char* text, int current_index, int total_lines, Line* lines) {
    parse_ptr = text;
    skip_spaces();

    /* PRINT [expr|"string"] [, ...] */
    if (strncmp(parse_ptr, "PRINT", 5) == 0 && (parse_ptr[5] == ' ' || parse_ptr[5] == '\t' || parse_ptr[5] == '\0')) {
        parse_ptr += 5;
        for (;;) {
            skip_spaces();
            if (!*parse_ptr || *parse_ptr == '\n') break;
            if (*parse_ptr == '"') {
                parse_ptr++;
                while (*parse_ptr && *parse_ptr != '"') { putchar(*parse_ptr); parse_ptr++; }
                if (*parse_ptr == '"') parse_ptr++;
            } else {
                printf("%d", eval_expr());
            }
            skip_spaces();
            if (*parse_ptr == ',') { parse_ptr++; putchar(' '); continue; }
            break;
        }
        putchar('\n');
        return current_index + 1;
    }

    /* LET var = expr  or  LET A(i) = expr */
    if (strncmp(parse_ptr, "LET", 3) == 0 && (parse_ptr[3] == ' ' || parse_ptr[3] == '\t')) {
        parse_ptr += 3;
        skip_spaces();
        int vi = parse_var();
        if (vi < 0) return current_index + 1;
        skip_spaces();
        if (*parse_ptr == '(') {
            parse_ptr++;
            int idx = eval_expr();
            skip_spaces();
            if (*parse_ptr == ')') parse_ptr++;
            skip_spaces();
            if (*parse_ptr == '=') parse_ptr++;
            skip_spaces();
            if (arrays[vi] && idx >= 0 && idx < array_sizes[vi])
                arrays[vi][idx] = eval_expr();
        } else {
            skip_spaces();
            if (*parse_ptr == '=') parse_ptr++;
            skip_spaces();
            vars[vi] = eval_expr();
        }
        return current_index + 1;
    }

    /* GOTO num */
    if (strncmp(parse_ptr, "GOTO", 4) == 0 && (parse_ptr[4] == ' ' || parse_ptr[4] == '\t')) {
        parse_ptr += 4;
        int target;
        if (parse_number(&target) != 0) return current_index + 1;
        for (int i = 0; i < total_lines; i++) {
            if (lines[i].num == target) return i;
        }
        return current_index + 1;
    }

    /* IF condition THEN GOTO num  or  IF condition THEN num */
    if (strncmp(parse_ptr, "IF", 2) == 0 && (parse_ptr[2] == ' ' || parse_ptr[2] == '\t')) {
        parse_ptr += 2;
        int cond = eval_condition();
        skip_spaces();
        if (strncmp(parse_ptr, "THEN", 4) == 0) parse_ptr += 4;
        skip_spaces();
        if (cond) {
            int target;
            if (parse_number(&target) != 0) return current_index + 1;
            for (int i = 0; i < total_lines; i++) {
                if (lines[i].num == target) return i;
            }
        }
        return current_index + 1;
    }

    /* END */
    if (strncmp(parse_ptr, "END", 3) == 0 && (parse_ptr[3] == ' ' || parse_ptr[3] == '\t' || parse_ptr[3] == '\0' || parse_ptr[3] == '\n')) {
        return -1;
    }

    /* DIM var(num) */
    if (strncmp(parse_ptr, "DIM", 3) == 0 && (parse_ptr[3] == ' ' || parse_ptr[3] == '\t')) {
        parse_ptr += 3;
        skip_spaces();
        int vi = parse_var();
        if (vi >= 0) {
            skip_spaces();
            if (*parse_ptr == '(') {
                parse_ptr++;
                int sz = eval_expr();
                skip_spaces();
                if (*parse_ptr == ')') parse_ptr++;
                if (sz > 0 && sz <= 65536) {
                    if (arrays[vi]) free(arrays[vi]);
                    arrays[vi] = (int*)calloc((size_t)sz, sizeof(int));
                    array_sizes[vi] = sz;
                }
            }
        }
        return current_index + 1;
    }

    return current_index + 1;
}

static int execute_line(int line_index) {
    return execute_line_text(program[line_index].text, line_index, num_program_lines, program);
}

/* Sort program lines by line number */
static void sort_program(void) {
    for (int i = 0; i < num_program_lines - 1; i++) {
        for (int j = i + 1; j < num_program_lines; j++) {
            if (program[j].num < program[i].num) {
                Line t = program[i];
                program[i] = program[j];
                program[j] = t;
            }
        }
    }
}

/* Add or replace a line by number */
static void add_line(int num, const char* text) {
    /* Remove existing line with same number */
    for (int i = 0; i < num_program_lines; i++) {
        if (program[i].num == num) {
            for (int k = i; k < num_program_lines - 1; k++)
                program[k] = program[k + 1];
            num_program_lines--;
            break;
        }
    }
    if (num_program_lines >= MAX_LINES) return;
    program[num_program_lines].num = num;
    strncpy(program[num_program_lines].text, text, MAX_LINELEN - 1);
    program[num_program_lines].text[MAX_LINELEN - 1] = '\0';
    num_program_lines++;
    sort_program();
}

static void clear_program(void) {
    num_program_lines = 0;
}

static void init_vars(void) {
    for (int i = 0; i < NUM_VARS; i++) {
        vars[i] = 0;
        if (arrays[i]) { free(arrays[i]); arrays[i] = NULL; }
        array_sizes[i] = 0;
    }
}

static void do_run(void) {
    if (num_program_lines == 0) {
        printf("No program.\n");
        return;
    }
    init_vars();
    run_index = 0;
    run_mode = 1;
    while (run_index >= 0 && run_index < num_program_lines) {
        run_index = execute_line(run_index);
    }
    run_mode = 0;
    run_index = -1;
}

static void do_list(void) {
    for (int i = 0; i < num_program_lines; i++) {
        printf("%d %s\n", program[i].num, program[i].text);
    }
}

static void do_new(void) {
    clear_program();
    printf("Program cleared.\n");
}

static int do_load(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        printf("Cannot open file: %s\n", filename);
        return -1;
    }
    clear_program();
    char buf[MAX_LINELEN];
    int line_num;
    while (fgets(buf, sizeof(buf), f)) {
        if (sscanf(buf, "%d", &line_num) == 1) {
            char* rest = buf;
            while (*rest && (*rest == ' ' || isdigit((unsigned char)*rest))) rest++;
            while (*rest == ' ' || *rest == '\t') rest++;
            if (num_program_lines < MAX_LINES) {
                program[num_program_lines].num = line_num;
                strncpy(program[num_program_lines].text, rest, MAX_LINELEN - 1);
                program[num_program_lines].text[MAX_LINELEN - 1] = '\0';
                /* trim trailing newline */
                size_t len = strlen(program[num_program_lines].text);
                if (len > 0 && program[num_program_lines].text[len - 1] == '\n')
                    program[num_program_lines].text[len - 1] = '\0';
                num_program_lines++;
            }
        }
    }
    fclose(f);
    sort_program();
    printf("Loaded %s\n", filename);
    return 0;
}

static void do_save(const char* filename) {
    FILE* f = fopen(filename, "w");
    if (!f) {
        printf("Cannot create file: %s\n", filename);
        return;
    }
    for (int i = 0; i < num_program_lines; i++) {
        fprintf(f, "%d %s\n", program[i].num, program[i].text);
    }
    fclose(f);
    printf("Saved %s\n", filename);
}

/* Process direct statement (no line number) or add program line */
static int process_input(char* buf) {
    /* Trim newline */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') { buf[len - 1] = '\0'; len--; }

    const char* p = buf;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return 0;

    /* Line number at start => program line */
    if (isdigit((unsigned char)*p)) {
        int line_num = 0;
        while (isdigit((unsigned char)*p)) { line_num = line_num * 10 + (*p - '0'); p++; }
        while (*p == ' ' || *p == '\t') p++;
        if (*p) add_line(line_num, p);
        else {
            /* Delete line */
            for (int i = 0; i < num_program_lines; i++) {
                if (program[i].num == line_num) {
                    for (int k = i; k < num_program_lines - 1; k++) program[k] = program[k + 1];
                    num_program_lines--;
                    break;
                }
            }
        }
        return 0;
    }

    /* Direct commands */
    if (strncmp(p, "RUN", 3) == 0 && (p[3] == '\0' || p[3] == ' ' || p[3] == '\t')) {
        do_run();
        return 0;
    }
    if (strncmp(p, "LIST", 4) == 0 && (p[4] == '\0' || p[4] == ' ' || p[4] == '\t')) {
        do_list();
        return 0;
    }
    if (strncmp(p, "NEW", 3) == 0 && (p[3] == '\0' || p[3] == ' ' || p[3] == '\t')) {
        do_new();
        return 0;
    }
    if (strncmp(p, "QUIT", 4) == 0 && (p[4] == '\0' || p[4] == ' ' || p[4] == '\t')) {
        return 1;
    }
    if (strncmp(p, "LOAD", 4) == 0 && (p[4] == ' ' || p[4] == '\t')) {
        p += 4;
        while (*p == ' ' || *p == '\t') p++;
        if (*p) do_load(p);
        else printf("Usage: LOAD filename\n");
        return 0;
    }
    if (strncmp(p, "SAVE", 4) == 0 && (p[4] == ' ' || p[4] == '\t')) {
        p += 4;
        while (*p == ' ' || *p == '\t') p++;
        if (*p) do_save(p);
        else printf("Usage: SAVE filename\n");
        return 0;
    }

    /* Direct statement: PRINT, LET, DIM, etc. (execute once, no GOTO/IF target) */
    {
        Line fake;
        fake.num = 0;
        strncpy(fake.text, p, MAX_LINELEN - 1);
        fake.text[MAX_LINELEN - 1] = '\0';
        execute_line_text(p, 0, 1, &fake);
    }
    return 0;
}

int main(void) {
    init_vars();
    printf("Tiny BASIC Interpreter\n");
    printf("Commands: LOAD, SAVE, RUN, LIST, NEW, QUIT\n");
    printf("Statements: PRINT, LET, GOTO, IF, END, DIM\n");
    printf("Variables: A-Z (integers). Type line number + statement to add a line.\n\n");

    char buf[MAX_LINELEN];
    for (;;) {
        printf("> ");
        fflush(stdout);
        if (!fgets(buf, sizeof(buf), stdin)) break;
        if (process_input(buf)) break;
    }
    printf("Goodbye.\n");

    for (int i = 0; i < NUM_VARS; i++) {
        if (arrays[i]) free(arrays[i]);
    }
    return 0;
}

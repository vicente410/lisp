#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>

#define HEAP_SIZE 1024 * 1024

typedef struct Obj Obj;

typedef struct {
    Obj *head;
    Obj *tail;
} Cons;

typedef enum {
    NIL,
    CONS,
    SYM,
    PRIM,
    CLOS,
    MACRO,
    NUM,
    CHAR,
} ObjKind;

struct Obj {
    union {
        Cons cons;
        Cons clos;
        Cons macro;
        char* sym;
        Obj* (*prim)(Obj* env, Obj* args);
        float num;
        char ch;
    } as;
    ObjKind kind;
    bool mark;
    bool free;
};

Obj heap[HEAP_SIZE];
Obj *next_free = NULL;
Obj *nil = &heap[0];
Obj *base_env = &heap[0];

Obj *read(FILE *f);
Obj *eval(Obj *env, Obj *obj);
void print_obj(Obj *obj);

char *get_kind_string(ObjKind kind) {
    switch (kind) {
    case NIL: return "nil";
    case CONS: return "cons";
    case SYM: return "sym";
    case PRIM: return "prim";
    case CLOS: return "clos";
    case MACRO: return "macro";
    case NUM: return "num";
    default: assert(0 && "Error: Invalid kind");
    }
}

void init_heap() {
    for (size_t i = 1; i < HEAP_SIZE; i++) {
        heap[i].mark = false;
        heap[i].free = true;
        heap[i].as.cons.head = &heap[i + 1];
    }
    heap[HEAP_SIZE - 1].as.cons.head = NULL;
    next_free = &heap[1];

    heap[0].kind = NIL;
    heap[0].free = false;
}

void mark(Obj *obj) {
    if (!obj->mark) {
        obj->mark = true;

        if (obj->kind == CONS || obj->kind == CLOS || obj->kind == MACRO) {
            mark(obj->as.cons.head);
            mark(obj->as.cons.tail);
        }
    }
}

void garbage_collect() {
    Obj *prev_free = NULL;

    mark(base_env);

    for (size_t i = 1; i < HEAP_SIZE; i++) {
        if (heap[i].mark) {
            heap[i].mark = false;
        } else {
            heap[i].free = true;
            if (prev_free) prev_free->as.cons.head = &heap[i];
            else next_free = &heap[i];
            prev_free = &heap[i];
        }
    }

    prev_free->as.cons.head = NULL;
}

void dump_heap() {
    for (size_t i = 0; i < 1000; i++) {
        printf("[%ld] ", i);
        //if (&heap[i].free) printf("free");
        /*else*/ print_obj(&heap[i]);
        printf("\n");
    }
}

Obj* alloc_obj() {
    if (!next_free) dump_heap();
    assert(next_free && "Out of memory");
    Obj *ret = next_free;
    ret->free = false;
    next_free = next_free->as.cons.head;
    return ret;
}

Obj *head(Obj *obj) {
    if (obj->kind != CONS) {
        fprintf(stderr, "Error: can't get head of %s", get_kind_string(obj->kind));
        exit(1);
    }
    return obj->as.cons.head;
}

Obj *tail(Obj *obj) {
    if (obj->kind != CONS) dump_heap();
    assert(obj->kind == CONS);
    return obj->as.cons.tail;
}

Obj *clos_args(Obj *obj) {
    assert(obj->kind == CLOS);
    return obj->as.clos.head;
}

Obj *clos_body(Obj *obj) {
    assert(obj->kind == CLOS);
    return obj->as.clos.tail->as.cons.head;
}

Obj *clos_env(Obj *obj) {
    assert(obj->kind == CLOS);
    return obj->as.clos.tail->as.cons.tail;
}

Obj *macro_args(Obj *obj) {
    assert(obj->kind == MACRO);
    return obj->as.cons.head;
}

Obj *macro_body(Obj *obj) {
    assert(obj->kind == MACRO);
    return obj->as.cons.tail;
}

Obj* new_cons(Obj *head, Obj *tail) {
    Obj *obj = alloc_obj();

    obj->kind = CONS;
    assert(head && tail);
    obj->as.cons.head = head;
    obj->as.cons.tail = tail;

    return obj;
}

Obj* new_sym(char *sym) {
    Obj *obj = alloc_obj();

    obj->kind = SYM;
    obj->as.sym = strdup(sym);

    return obj;
}

Obj* new_prim(Obj* (*prim)(Obj* env, Obj* args)) {
    Obj *obj = alloc_obj();

    obj->kind = PRIM;
    obj->as.prim = prim;

    return obj;
}

Obj* new_clos(Obj *env, Obj *args, Obj *body) {
    Obj *obj = new_cons(args, new_cons(body, env == base_env ? nil : env));

    obj->kind = CLOS;

    return obj;
}

Obj* new_macro(Obj *args, Obj *body) {
    Obj *obj = new_cons(args, body);

    obj->kind = MACRO;

    return obj;
}

Obj* new_num(float num) {
    Obj *obj = alloc_obj();

    obj->kind = NUM;
    obj->as.num = num;

    return obj;
}

Obj* new_char(char ch) {
    Obj *obj = alloc_obj();

    obj->kind = CHAR;
    obj->as.ch = ch;

    return obj;
}

void print_list(Obj* obj) {
    printf("(");
    print_obj(obj->as.cons.head);
    obj = obj->as.cons.tail;
    while (obj->kind == CONS) {
        printf(" ");
        print_obj(obj->as.cons.head);
        obj = obj->as.cons.tail;
    }
    if (obj->kind != NIL) {
        printf(" . ");
        print_obj(obj);
    }
    printf(")");
}

void print_obj(Obj* obj) {
    switch (obj->kind) {
    case NIL:   printf("()");              break;
    case CONS:  print_list(obj);           break;
    case SYM:   printf("%s", obj->as.sym); break;
    case PRIM:  printf("<primitive>");     break;
    case CLOS:  printf("<closure>");       break;
    case MACRO: printf("<macro>");         break;
    case NUM:   printf("%g", obj->as.num); break;
    case CHAR:  printf("%c", obj->as.ch);  break;
    }
}

Obj* get_env(Obj* env, char *sym) {
    while (env->kind == CONS) {
        if (strcmp(env->as.cons.head->as.cons.head->as.sym, sym) == 0) {
            return env->as.cons.head->as.cons.tail;
        } else {
            env = env->as.cons.tail;
        }
    }

    fprintf(stderr, "ERROR: '%s' not found in current environment\n", sym);
    exit(1);
}

Obj *acons(Obj *key, Obj *val, Obj *alist) {
    return new_cons(new_cons(key, val), alist);
}

void define(Obj *key, Obj *val) {
    base_env = acons(key, val, base_env);
}

void def_prim(char *sym, Obj *(prim)(Obj *env, Obj *args)) {
    define(new_sym(sym), new_prim(prim));
}

Obj *append(Obj *l1, Obj *l2) {
    if (l1->kind == CONS) {
        return new_cons(head(l1), append(tail(l1), l2));
    } else {
        return l2;
    }
}

Obj *f_def(Obj *env, Obj *args) {
    (void)env;
    Obj* x = eval(env, head(tail(args)));
    define(head(args), x);
    return x;
}

Obj *f_head(Obj *env, Obj *args) {
    return head(eval(env, head(args)));
}

Obj *f_tail(Obj *env, Obj *args) {
    return tail(eval(env, head(args)));
}

Obj *f_cons(Obj *env, Obj *args) {
    Obj *x = eval(env, head(args));
    Obj *y = eval(env, head(tail(args)));
    return new_cons(x, y);
}

Obj *f_add(Obj *env, Obj *args) {
    Obj *x = eval(env, head(args));
    Obj *y = eval(env, head(tail(args)));
    return new_num(x->as.num + y->as.num);
}

Obj *f_sub(Obj *env, Obj *args) {
    Obj *x = eval(env, head(args));
    Obj *y = eval(env, head(tail(args)));
    return new_num(x->as.num - y->as.num);
}

Obj *f_mul(Obj *env, Obj *args) {
    Obj *x = eval(env, head(args));
    Obj *y = eval(env, head(tail(args)));
    return new_num(x->as.num * y->as.num);
}

Obj *f_div(Obj *env, Obj *args) {
    Obj *x = eval(env, head(args));
    Obj *y = eval(env, head(tail(args)));
    return new_num(x->as.num / y->as.num);
}

Obj *f_mod(Obj *env, Obj *args) {
    Obj *x = eval(env, head(args));
    Obj *y = eval(env, head(tail(args)));
    return new_num((int)x->as.num % (int)y->as.num);
}

Obj *f_eq(Obj *env, Obj *args) {
    Obj *x = eval(env, head(args));
    Obj *y = eval(env, head(tail(args)));

    if (x->kind == NUM) return x->as.num == y->as.num ? new_sym("t") : nil;
    else if (x->kind == SYM) return strcmp(x->as.sym, y->as.sym) == 0 ? new_sym("t") : nil;
    else if (x->kind == CHAR) return x->as.ch == y->as.ch ? new_sym("t") : nil;
    else return nil;
}

Obj *f_less(Obj *env, Obj *args) {
    Obj *x = eval(env, head(args));
    Obj *y = eval(env, head(tail(args)));
    return x->as.num < y->as.num ? new_sym("t") : nil;
}

Obj *f_fn(Obj *env, Obj *args) {
    return new_clos(env, head(args), head(tail(args)));
}

Obj *f_macro(Obj *env, Obj *args) {
    (void)env;
    return new_macro(head(args), head(tail(args)));
}

Obj *f_if(Obj *env, Obj *args) {
    Obj *cond = eval(env, head(args));
    return cond->kind != NIL ? eval(env, head(tail(args))) : eval(env, head(tail(tail(args))));
}

Obj *f_lazy_if(Obj *env, Obj *args) {
    return head(args)->kind != NIL ? eval(env, head(tail(args))) : eval(env, head(tail(tail(args))));
}

Obj *f_quote(Obj *env, Obj *args) {
    (void)env;
    return head(args);
}

Obj *f_env(Obj *env, Obj *args) {
    (void)args;
    return env;
}

Obj *f_consp(Obj *env, Obj *args) {
    Obj *x = eval(env, head(args));
    return x->kind == CONS ? new_sym("t") : nil;
}

Obj *f_eval(Obj *env, Obj *args) {
    return eval(env, eval(env, head(args)));
}

Obj *f_print(Obj *env, Obj *args) {
    while (args->kind == CONS) {
        print_obj(eval(env, head(args)));
        printf(" ");
        args = tail(args);
    }
    printf("\n");

    return nil;
}

char fpeekc(FILE *f) {
    char c = fgetc(f);
    ungetc(c, f);
    return c;
}

Obj *read_list(FILE *f) {
    if (fpeekc(f) == ')') {
        fgetc(f);
        return nil;
    }

    Obj *head = read(f);
    return new_cons(head, read_list(f));
}

Obj *read_atom(FILE *f) {
    static char buffer[40];
    size_t i = 0;
    float num;

    while (!isspace(fpeekc(f)) && fpeekc(f) != '(' && fpeekc(f) != ')' && fpeekc(f) != ';') {
        buffer[i++] = fgetc(f);
    }
    buffer[i] = '\0';

    if (sscanf(buffer, "%f", &num) == 1) {
        return new_num(num);
    } else {
        return new_sym(buffer);
    }
}

bool next_token(FILE *f) {
    while (!feof(f) && (isspace(fpeekc(f)) || fpeekc(f) == ';')) {
        if (fpeekc(f) == ';') while (!feof(f) && fgetc(f) != '\n');
        else fgetc(f);
    }

    return !feof(f);
}

Obj *read(FILE *f) {
    if (!next_token(f)) return NULL;

    if (fpeekc(f) == '(') {
        fgetc(f);
        return read_list(f);
    } else if (fpeekc(f) == '\'') {
        fgetc(f);
        return new_cons(new_sym("quote"), new_cons(read(f), nil));
    } else if (fpeekc(f) == '`') {
        fgetc(f);
        return new_cons(new_sym("quasiquote"), new_cons(read(f), nil));
    } else if (fpeekc(f) == ',') {
        fgetc(f);
        return new_cons(new_sym("unquote"), new_cons(read(f), nil));
    } else if (fpeekc(f) == '@') {
        fgetc(f);
        return new_char(fgetc(f));
    } else {
        return read_atom(f);
    }
}

Obj *bind(Obj *syms, Obj *vals, Obj *env) {
    if (syms->kind == CONS) {
        if (strcmp(head(syms)->as.sym, ".") == 0) {
            return acons(head(tail(syms)), vals, env);
        } else {
            return acons(head(syms), head(vals), bind(tail(syms), tail(vals), env));
        }
    } else {
        return env;
    }
}

Obj *eval_list(Obj *env, Obj *list) {
    if (list->kind == CONS) {
        return new_cons(eval(env, head(list)), eval_list(env, tail(list)));
    } else {
        return nil;
    }
}

Obj *apply(Obj *env, Obj *f, Obj *args) {
    if (f->kind == PRIM) {
        return f->as.prim(env, args);
    } else if (f->kind == CLOS) {
        return eval(bind(clos_args(f), eval_list(env, args), append(clos_env(f), base_env)), clos_body(f));
    } else if (f->kind == MACRO) {
        return eval(env, eval(bind(macro_args(f), args, env), macro_body(f)));
    }
    assert(false && "Can't eval");
}

Obj *eval(Obj *env, Obj *obj) {
    assert(obj);
    switch (obj->kind) {
    case CONS:
        return apply(env, eval(env, head(obj)), tail(obj));
        break;
    case SYM:
        return get_env(env, obj->as.sym);
        break;
    default:
        return obj;
        break;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s [filename]", argv[0]);
        return 1;
    }

    init_heap();
    def_prim("def", f_def);
    def_prim("head", f_head);
    def_prim("tail", f_tail);
    def_prim("cons", f_cons);
    def_prim("+", f_add);
    def_prim("-", f_sub);
    def_prim("*", f_mul);
    def_prim("/", f_div);
    def_prim("%", f_mod);
    def_prim("=", f_eq);
    def_prim("<", f_less);
    def_prim("fn", f_fn);
    def_prim("macro", f_macro);
    def_prim("if", f_if);
    def_prim("lazy-if", f_lazy_if);
    def_prim("quote", f_quote);
    def_prim("env", f_env);
    def_prim("cons?", f_consp);
    def_prim("eval", f_eval);
    def_prim("print", f_print);

    FILE *f = fopen(argv[1], "r");
    Obj *obj;
    while ((obj = read(f)) != NULL) {
        eval(base_env, obj);
        garbage_collect();
    }

    return 0;
}

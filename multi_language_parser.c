#include <tree_sitter/api.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// from tree-sitter-python and tree-sitter-c libs
extern const TSLanguage *tree_sitter_python();
extern const TSLanguage *tree_sitter_c();

// Global Symbol Table (Hash Map)
#define HASH_SIZE 101

typedef struct SymbolNode {
    char *name;
    char *file;
    uint32_t line;
    struct SymbolNode *next;
} SymbolNode;

SymbolNode *symbol_table[HASH_SIZE];

unsigned int hash(const char *str) {
    unsigned int h = 0;
    while (*str) h = h * 31 + *str++;
    return h % HASH_SIZE;
}

void insert_symbol(const char *name, const char *file, uint32_t line) {
    unsigned int h = hash(name);
    SymbolNode *node = malloc(sizeof(SymbolNode));
    node->name = strdup(name);
    node->file = strdup(file);
    node->line = line;
    node->next = symbol_table[h];
    symbol_table[h] = node;
}

SymbolNode* find_symbol(const char *name) {
    unsigned int h = hash(name);
    SymbolNode *node = symbol_table[h];
    while (node) {
        if (strcmp(node->name, name) == 0) return node;
        node = node->next;
    }
    return NULL;
}

// Dependency Tracking
typedef struct {
    char *from_file;
    char *symbol;
    uint32_t line;
} Dependency;

Dependency *dependencies = NULL;
size_t dependency_count = 0;

void add_dependency(const char *from_file, const char *symbol, uint32_t line) {
    dependencies = realloc(dependencies, sizeof(Dependency) * (dependency_count + 1));
    dependencies[dependency_count].from_file = strdup(from_file);
    dependencies[dependency_count].symbol = strdup(symbol);
    dependencies[dependency_count].line = line;
    dependency_count++;
}

// Function to find Python Exports (e.g., 'def sum')
void find_python_exports(const char *filename, const char *source_code) {
    TSParser *parser = ts_parser_new();
    const TSLanguage *lang = tree_sitter_python();
    ts_parser_set_language(parser, lang);

    TSTree *tree = ts_parser_parse_string(parser, NULL, source_code, strlen(source_code));
    TSNode root = ts_tree_root_node(tree);

    const char *query_str = "(function_definition name: (identifier) @name)";
    uint32_t error_offset;
    TSQueryError error_type;
    TSQuery *query = ts_query_new(lang, query_str, strlen(query_str), &error_offset, &error_type);

    TSQueryCursor *cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query, root);

    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match)) {
        TSNode name_node = match.captures[0].node;
        uint32_t start = ts_node_start_byte(name_node);
        uint32_t end = ts_node_end_byte(name_node);
        
        char name[256];
        int len = end - start;
        if (len > 255) len = 255;
        strncpy(name, source_code + start, len);
        name[len] = '\0';

        TSPoint point = ts_node_start_point(name_node);
        insert_symbol(name, filename, point.row + 1);
    }

    ts_query_cursor_delete(cursor);
    ts_query_delete(query);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
}

// Function to find C Dependencies (metacall calls)
void find_c_metacall_calls(const char *filename, const char *source_code) {
    TSParser *parser = ts_parser_new();
    const TSLanguage *lang = tree_sitter_c();
    ts_parser_set_language(parser, lang);

    TSTree *tree = ts_parser_parse_string(parser, NULL, source_code, strlen(source_code));
    TSNode root = ts_tree_root_node(tree);

    // Capture the function name and the first argument
    const char *query_str =
        "(call_expression "
        "  function: (identifier) @func_name "
        "  arguments: (argument_list . (string_literal) @sym_name))";

    uint32_t error_offset;
    TSQueryError error_type;
    TSQuery *query = ts_query_new(lang, query_str, strlen(query_str), &error_offset, &error_type);

    TSQueryCursor *cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query, root);

    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match)) {
        TSNode func_node = match.captures[0].node;
        TSNode sym_node = match.captures[1].node;

        uint32_t f_start = ts_node_start_byte(func_node);
        uint32_t f_end = ts_node_end_byte(func_node);
        
        char func_name[256];
        int f_len = f_end - f_start;
        if (f_len > 255) f_len = 255;
        strncpy(func_name, source_code + f_start, f_len);
        func_name[f_len] = '\0';

        // Only care about metacallv_s
        if (strcmp(func_name, "metacallv_s") != 0) continue;

        uint32_t s_start = ts_node_start_byte(sym_node);
        uint32_t s_end = ts_node_end_byte(sym_node);

        // Extract symbol and strip quotes
        char symbol[256];
        int len = s_end - s_start;
        if (len > 255) len = 255;
        strncpy(symbol, source_code + s_start, len);
        symbol[len] = '\0';

        // Remove quotes if present
        char *clean_sym = symbol;
        if (symbol[0] == '"') {
            clean_sym++;
            if (symbol[len - 1] == '"') symbol[len - 1] = '\0';
        }

        TSPoint point = ts_node_start_point(sym_node);
        add_dependency(filename, clean_sym, point.row + 1);
    }

    ts_query_cursor_delete(cursor);
    ts_query_delete(query);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
}

void print_json_output() {
    printf("{\n");
    printf("  \"files\": [\"sum.py\", \"caller.c\"],\n");
    
    printf("  \"exports\": [\n");
    int first = 1;
    for (int i = 0; i < HASH_SIZE; i++) {
        SymbolNode *node = symbol_table[i];
        while (node) {
            if (!first) printf(",\n");
            printf("    { \"file\": \"%s\", \"name\": \"%s\", \"line\": %u, \"type\": \"function\" }", 
                   node->file, node->name, node->line);
            first = 0;
            node = node->next;
        }
    }
    printf("\n  ],\n");

    printf("  \"dependencies\": [\n");
    for (size_t i = 0; i < dependency_count; i++) {
        if (i > 0) printf(",\n");
        SymbolNode *resolved = find_symbol(dependencies[i].symbol);
        printf("    { \"from\": \"%s\", \"to\": \"%s\", \"line\": %u, \"resolved\": %s }", 
               dependencies[i].from_file, 
               dependencies[i].symbol, 
               dependencies[i].line,
               resolved ? "true" : "false");
    }
    printf("\n  ]\n");
    printf("}\n");
}

char *read_file_to_string(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *content = malloc(length + 1);
    if (content) {
        size_t read_len = fread(content, 1, length, file);
        content[read_len] = '\0';
    }
    fclose(file);
    return content;
}

int main() {
    char *python_code = read_file_to_string("src/sum.py");
    char *c_code = read_file_to_string("src/caller.c");

    if (python_code) {
        find_python_exports("src/sum.py", python_code);
        free(python_code);
    }

    if (c_code) {
        find_c_metacall_calls("src/caller.c", c_code);
        free(c_code);
    }

    print_json_output();

    return 0;
}

#include <tree_sitter/api.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// from tree-sitter-python and tree-sitter-c libs
extern const TSLanguage *tree_sitter_python();
extern const TSLanguage *tree_sitter_c();

void print_node_recursive(TSNode node, int depth) {
    const char *type = ts_node_type(node);
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);

    // Print indentation
    for (int i = 0; i < depth; i++) printf("  ");

    // Print node type and range
    printf("%s [%u - %u]", type, start, end);

    // If it's a leaf node (no children), print its text content (shortened if too long)
    uint32_t child_count = ts_node_child_count(node);
    if (child_count == 0) {
        printf(": \"(leaf)\"");
    }
    printf("\n");

    // Recursively print children
    for (uint32_t i = 0; i < child_count; i++) {
        print_node_recursive(ts_node_child(node, i), depth + 1);
    }
}

void print_ast(TSNode root) {
    printf("--- Abstract Syntax Tree ---\n");
    print_node_recursive(root, 0);
    printf("---------------------------\n");
}

// Function to find Python Exports (e.g., 'def sum')
void find_python_exports(const char *source_code) {
    TSParser *parser = ts_parser_new();
    const TSLanguage *lang = tree_sitter_python();
    
    if (!ts_parser_set_language(parser, lang)) {
        fprintf(stderr, "Error: Failed to set Python language\n");
        return;
    }

    TSTree *tree = ts_parser_parse_string(parser, NULL, source_code, strlen(source_code));
    if (!tree) {
        fprintf(stderr, "Error: Failed to parse Python code\n");
        ts_parser_delete(parser);
        return;
    }
    TSNode root = ts_tree_root_node(tree);

    // Query for function definitions
    const char *query_str = "(function_definition name: (identifier) @name)";
    uint32_t error_offset;
    TSQueryError error_type;
    TSQuery *query = ts_query_new(lang, query_str, strlen(query_str), &error_offset, &error_type);

    if (!query) {
        fprintf(stderr, "Error: Failed to create Python query at offset %u (Error type: %d)\n", error_offset, error_type);
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return;
    }

    TSQueryCursor *cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query, root);

    TSQueryMatch match;
    printf("--- Python Exports ---\n");
    while (ts_query_cursor_next_match(cursor, &match)) {
        TSNode name_node = match.captures[0].node;
        uint32_t start = ts_node_start_byte(name_node);
        uint32_t end = ts_node_end_byte(name_node);
        printf("Found Function Export: %.*s\n", end - start, source_code + start);
    }

    print_ast(root);

    ts_query_cursor_delete(cursor);
    ts_query_delete(query);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
}

// Function to find C Dependencies (metacall calls)
void find_c_metacall_calls(const char *source_code) {
    TSParser *parser = ts_parser_new();
    const TSLanguage *lang = tree_sitter_c();

    if (!ts_parser_set_language(parser, lang)) {
        fprintf(stderr, "Error: Failed to set C language\n");
        return;
    }

    TSTree *tree = ts_parser_parse_string(parser, NULL, source_code, strlen(source_code));
    if (!tree) {
        fprintf(stderr, "Error: Failed to parse C code\n");
        ts_parser_delete(parser);
        return;
    }
    TSNode root = ts_tree_root_node(tree);

    // Refined query for metacallv_s calls
    // We capture both the function name and the first argument
    const char *query_str =
        "(call_expression "
        "  function: (identifier) @func_name "
        "  arguments: (argument_list . (string_literal) @sym_name) "
        "  (#eq? @func_name \"metacallv_s\"))";

    uint32_t error_offset;
    TSQueryError error_type;
    TSQuery *query = ts_query_new(lang, query_str, strlen(query_str), &error_offset, &error_type);

    if (!query) {
        fprintf(stderr, "Error: Failed to create C query at offset %u (Error type: %d)\n", error_offset, error_type);
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return;
    }

    TSQueryCursor *cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query, root);

    TSQueryMatch match;
    printf("\n--- C Cross-Language Calls ---\n");
    while (ts_query_cursor_next_match(cursor, &match)) {
        TSNode func_node = match.captures[0].node;
        TSNode sym_node = match.captures[1].node;

        uint32_t f_start = ts_node_start_byte(func_node);
        uint32_t f_end = ts_node_end_byte(func_node);
        uint32_t s_start = ts_node_start_byte(sym_node);
        uint32_t s_end = ts_node_end_byte(sym_node);

        printf("Detected MetaCall: function=%.*s, symbol=%.*s\n", 
               f_end - f_start, source_code + f_start,
               s_end - s_start, source_code + s_start);
    }

    print_ast(root);

    ts_query_cursor_delete(cursor);
    ts_query_delete(query);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
}

char *read_file_to_string(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Could not open file");
        return NULL;
    }

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
    char *python_code = read_file_to_string("sum.py");
    char *c_code = read_file_to_string("caller.c");

    if (python_code) {
        find_python_exports(python_code);
        free(python_code);
    } else {
        printf("Failed to read sum.py\n");
    }

    if (c_code) {
        find_c_metacall_calls(c_code);
        free(c_code);
    } else {
        printf("Failed to read caller.c\n");
    }

    return 0;
}
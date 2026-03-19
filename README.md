# MetaCall + Tree-sitter POC

This POC uses **Tree-sitter** to print the symbol table and dependency map of a simple polyglot codebase where a **C** program calls a **Python** function via **MetaCall**.

## Quick Start

### 1. Run the Polyglot Application
Verify that the C code can call the Python function:
```bash
gcc src/caller.c -lmetacall -o caller && ./caller
# Output: Result of sum(3, 4): 7
```

### 2. Run the Introspection Parser
Analyze the code to find cross-language dependencies:

**Compile Grammars:**
```bash
# C Grammar
gcc -c grammar_for_tree-sitter/tree-sitter-c/src/parser.c -I grammar_for_tree-sitter/tree-sitter-c/src -o c_parser.o

# Python Grammar
gcc -c grammar_for_tree-sitter/tree-sitter-python/src/parser.c -I grammar_for_tree-sitter/tree-sitter-python/src -o py_parser.o
gcc -c grammar_for_tree-sitter/tree-sitter-python/src/scanner.c -I grammar_for_tree-sitter/tree-sitter-python/src -o py_scanner.o
```

**Build and Run Parser:**
```bash
gcc multi_language_parser.c c_parser.o py_parser.o py_scanner.o -ltree-sitter -o parser
./parser
```

## How it Works
1.  **Python Analysis:** Finds function definitions (e.g., `def sum`).
2.  **C Analysis:** Finds MetaCall calls (e.g., `metacallv_s("sum", ...)`).
3.  **Resolution:** Matches the C "dependency" to the Python "export" and outputs JSON:

```json
{
  "exports": [{ "file": "sum.py", "name": "sum", "line": 1 }],
  "dependencies": [{ "from": "caller.c", "to": "sum", "resolved": true }]
}
```

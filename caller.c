#include <stdio.h>
#include <metacall/metacall.h>

int main() {
    if (metacall_initialize() != 0) {
        fprintf(stderr, "Failed to initialize MetaCall\n");
        return 1;
    }

    const char* py_script_name = "sum.py";
    if (metacall_load_from_file("py", &py_script_name, 1, NULL) != 0) {
        fprintf(stderr, "Failed to load Python script\n");
        metacall_destroy();
        return 1;
    }

    void* args[] = {
        metacall_value_create_int(3),
        metacall_value_create_int(4)
    };

    void* res = metacallv_s("sum", args, 2);

    if (res != NULL) {
        int result = metacall_value_to_int(res);
        printf("Result of sum(3, 4): %d\n", result);
        metacall_value_destroy(res);
    } else {
        fprintf(stderr, "Failed to call function 'sum'\n");
    }

    metacall_value_destroy(args[0]);
    metacall_value_destroy(args[1]);
    metacall_destroy();

    return 0;
}

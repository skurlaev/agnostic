
#include "agnostic.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <assert.h>

static struct ag_component* extract_component(struct ag_project* project, int argc, const char** argv) {
    struct ag_component* ret = NULL;
    if (1 == argc) {
        ret = ag_find_component(project, *argv);
    } else if (2 == argc) {
        if (!strcmp("-c", *argv)) {
            ret = ag_find_component(project, *(argv+1));
        } else {
            die("Unrecognized argument: %s", *argv);
        }
    } else if (0 == argc) {
        ret = ag_find_current_component(project);
    } else {
        die("Too many arguments");
    }
    if (!ret) {
        die("Component not found");
    }
    return ret;
}

enum run_return_codes {
    NOTHING_TO_DO = 1,
    SCRIPT_FAILED,
    SCRIPT_ABORTED
};

static int run_component_script(struct ag_project* project, struct ag_component* c, const char* script_content) {
    assert(project);
    assert(c);

    if (!script_content || !script_content[0]) {
        return NOTHING_TO_DO;
    }

    char* script = create_temp_file("agnostic-script-", script_content);
    if (!script) {
        die("Unable to create script.");
    }
    char* parent_dir = ag_component_dir(project, c);
    if (!parent_dir) {
        remove(script);
        die("Unable to find parent directory of the component.");
    }
    debug_print("Running script %s from parent directory %s\n", script, parent_dir);
    pid_t child_pid = run_script(parent_dir, script);
    if (-1 == child_pid) {
        perror(NULL);
        remove(script);
        die("Failed to run build");
    }

    int status = 0;
    wait(&status);

    remove(script);

    int ret = OK;
    if (WIFEXITED(status)) {
        if (WEXITSTATUS(status)) {
            ret = SCRIPT_FAILED;
        }
    } else {
        ret = SCRIPT_ABORTED;
    }

    free(script);
    free(parent_dir);
    return ret;
}

static void build_component(struct ag_project* project, struct ag_component* c) {
    assert(project);
    assert(c);

    printf(PROP_COLOR "Building %s" COLOR_RESET "\n", c->name);

    switch (run_component_script(project, c, c->build)) {
    case NOTHING_TO_DO:
        die("Nothing to build: %s", c->name);
        break;

    case SCRIPT_FAILED:
        die("Failed to build: %s", c->name);
        break;

    case SCRIPT_ABORTED:
        die("Building aborted: %s", c->name);
        break;

    case OK:
        break;
    }
}

static void clean_component(struct ag_project* project, struct ag_component* c) {
    assert(project);
    assert(c);

    printf(PROP_COLOR "Cleaning %s" COLOR_RESET "\n", c->name);

    switch (run_component_script(project, c, c->clean)) {
    case NOTHING_TO_DO:
        printf("Nothing to clean: %s\n", c->name);
        break;

    case SCRIPT_FAILED:
        printf("Failed to clean: %s\n", c->name);
        break;

    case SCRIPT_ABORTED:
        printf("Cleaning aborted: %s\n", c->name);
        break;

    case OK:
        break;
    }
}

static void test_component(struct ag_project* project, struct ag_component* c) {
    assert(project);
    assert(c);

    printf(PROP_COLOR "Testing %s" COLOR_RESET "\n", c->name);

    switch (run_component_script(project, c, c->test)) {
    case NOTHING_TO_DO:
        printf("Nothing to test: %s\n", c->name);
        break;

    case SCRIPT_FAILED:
        printf("Failed to test: %s\n", c->name);
        break;

    case SCRIPT_ABORTED:
        printf("Testing aborted: %s\n", c->name);
        break;

    case OK:
        break;
    }
}

static struct list* list_current(struct ag_project* project) {
    return list_create(extract_component(project, 0, NULL), NULL);
}

static struct list* list_list(struct ag_project* project, int argc, const char** argv) {
    struct list* ret = NULL;
    struct list* tail = NULL;
    while (0 < argc) {
        struct ag_component* c = ag_find_component(project, *argv);
        // TODO: re-write this to support -c and use extract_component()
        if (!c) {
            die("Component not found: %s", *argv);
        }
        --argc;
        ++argv;
        list_add(&ret, &tail, c);
    }
    return ret;
}

static struct list* list_up_down(struct ag_project* project, int up, int* skip_disabled, int argc, const char** argv) {
    const char* up_to = NULL;

    while (1 <= argc) {
        if (!strcmp("-t", *argv) || !strcmp("--to", *argv)) {
            if (2 > argc) {
                die("Expected component name/alias after %s", *argv);
            }
            ++argv;
            --argc;
            up_to = *argv;
        } else {
            break;
        }
        ++argv;
        --argc;
    }
    *skip_disabled = (NULL == up_to);

    struct list* ret = NULL;
    int rc = 0;
    if (up) {
        ret = ag_build_up_list(project, extract_component(project, argc, argv), up_to, &rc);
    } else {
        ret = ag_build_down_list(project, extract_component(project, argc, argv), up_to, &rc);
    }
    if (!ret) {
        die("Failed to resolve build order. %s.", ag_error_msg(rc));
    }
    return ret;
}

static struct list* list_all(struct ag_project* project) {
    return ag_build_all_list(project);
}

static void perform_main(void (*p)(struct ag_project*, struct ag_component*), int argc, const char** argv) {
    struct ag_project* project = ag_load_default_or_die();

    int dry_run = 0;
    int skip_disabled = 0;

    // options
    while (1 <= argc) {
        if (!strcmp("-n", *argv) || !strcmp("--dry-run", *argv)) {
            dry_run = 1;
        } else {
            break;
        }
        --argc;
        ++argv;
    }

    struct list* list = NULL;

    // command
    if (1 <= argc) {
        if (!strcmp("up", *argv)) {
            list = list_up_down(project, 1, &skip_disabled, argc-1, argv+1);

        } else if (!strcmp("down", *argv)) {
            list = list_up_down(project, 0, &skip_disabled, argc-1, argv+1);
            
        } else if (!strcmp("all", *argv)) {
            list = list_all(project);
            skip_disabled = 1;
            
        } else {
            list = list_list(project, argc, argv);
        }
    } else {
        list = list_current(project);
    }

    for (struct list* i = list; i; i = i->next) {
        struct ag_component* c = (struct ag_component*)i->data;
        int skip = skip_disabled && c->disabled;
        if (dry_run) {
            if (!skip) {
                printf("%s\n", c->name);
            }
        } else if (skip) {
            printf(WARN_COLOR "Skipping %s" COLOR_RESET "\n", c->name);
        } else {
            p(project, c);
        }
    }

    list_free(list, NULL);
    ag_free(project);    
}

void build(int argc, const char** argv) {
    perform_main(&build_component, argc, argv);
}

void clean(int argc, const char** argv) {
    perform_main(&clean_component, argc, argv);
}

void test(int argc, const char** argv) {
    perform_main(&test_component, argc, argv);
}

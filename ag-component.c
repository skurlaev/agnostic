
#include "agnostic.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

static void print_component(struct ag_component* c) {
    assert(c);

    if (c->disabled) {
        printf(WARN_COLOR "Disabled" TERM_COLOR_RESET "\n");
    }
    printf(PROP_COLOR "Name:" TERM_COLOR_RESET " %s\n", c->name);
    if (c->alias) {
        printf(PROP_COLOR "Alias:" TERM_COLOR_RESET " %s\n", c->alias);
    }
    if (c->git) {
        printf(PROP_COLOR "Repository:" TERM_COLOR_RESET " %s (git)\n", c->git);
    } else if (c->hg) {
        printf(PROP_COLOR "Repository:" TERM_COLOR_RESET " %s (mercurial)\n", c->hg);
    }
    if (c->description && c->description[0]) {
        printf(PROP_COLOR "Description:" TERM_COLOR_RESET " %s\n", c->description);
    }
    if (c->build_after) {
        struct list* l = c->build_after;
        printf(PROP_COLOR "\nBuild after:\n" TERM_COLOR_RESET);
        while (l) {
            printf("%s ", (char*) l->data);
            l = l->next;
        }
        printf("\n");
    }
    if (c->build) {
        printf(PROP_COLOR "\nBuild:" TERM_COLOR_RESET "\n%s\n", c->build);
        if (c->integrate) {
            printf(PROP_COLOR "Integration build:" TERM_COLOR_RESET "\n%s\n", c->integrate);
        }
    }
    if (c->clean) {
        printf(PROP_COLOR "Clean:" TERM_COLOR_RESET "\n%s\n", c->clean);
    }
    if (c->test) {
        printf(PROP_COLOR "Test:" TERM_COLOR_RESET "\n%s\n", c->test);
    }
}

void component(int argc, const char** argv) {
    const char* comp_name = NULL;
    if (0 == argc) {
        // current component
        comp_name = NULL;
    } else if (1 == argc) {
        comp_name = *argv;
    } else {
        die("Too many arguments");
    }

    struct ag_project* project = ag_load_default_or_die();
    struct ag_component* c = NULL;
    if (comp_name) {
        c = ag_find_component(project, comp_name);
    } else {
        c = ag_find_current_component(project);
    }
    if (c) {
        print_component(c);
    } else {
        die("Component not found");
    }
    ag_free(project);
}

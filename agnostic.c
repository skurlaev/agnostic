
#include "agnostic.h"
#include "common.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static const char* error_messages[] = {
    [OK] = "OK",
    [UNABLE_TO_OPEN_FILE] = "Unable to open file",
    [FILE_NOT_FOUND] = "File not found",
    [PROJECT_GOES_AFTER_COMPONENT] = "Project section must go before any component sections"
};

const char* ag_error_msg(int code) {
    if (0 > code || code >= ARRAY_SIZE(error_messages)) {
        return NULL;
    }
    return error_messages[code];
}

struct ag_component_list* ag_create_component_node(struct ag_component* component, struct ag_component_list* next) {
    if (!component) {
        return NULL;
    }
    struct ag_component_list* ret = (struct ag_component_list*)xcalloc(1, sizeof(struct ag_component_list));
    ret->component = component;
    ret->next = next;
    return ret;
}

struct ag_string_list* ag_create_string_node(char* s, struct ag_string_list* next) {
    if (!s) {
        return NULL;
    }
    struct ag_string_list* ret = (struct ag_string_list*)xcalloc(1, sizeof(struct ag_string_list));
    ret->s = s;
    ret->next = next;
    return ret;
}

static void ag_free_component(struct ag_component* c);

static void ag_free_component_list(struct ag_component_list* c, bool free_components) {
    struct ag_component_list* n = NULL;
    while (c) {
        if (free_components) {
            ag_free_component(c->component);
        }
        n = c->next;
        free(c);
        c = n;
    }
}

void ag_shallow_free_component_list(struct ag_component_list* c) {
    ag_free_component_list(c, false);
}

void ag_free_string_list(struct ag_string_list* l) {
    struct ag_string_list* n = NULL;
    while (l) {
        free(l->s);
        n = l->next;
        free(l);
        l = n;
    }
}

static void ag_free_component(struct ag_component* c) {
    if (!c) {
        return;
    }
    free(c->name);
    free(c->alias);
    free(c->description);
    free(c->git);
    free(c->hg);
    free(c->build);
    free(c->integrate);
    free(c->clean);
    ag_free_string_list(c->build_after);
    free(c);
}

void ag_free(struct ag_project* p) {
    if (!p) {
        return;
    }
    free(p->name);
    free(p->description);
    free(p->bugs);
    free(p->dir);
    free(p->file);
    ag_free_component_list(p->components, true);
    ag_free_string_list(p->docs);
    free(p);
}

static bool file_exist(const char* fname) {
    return access(fname, F_OK) != -1;
}

static char * normalize_path(const char * src, size_t src_len) {
    // initial version of this function is written by the user 'arnaud576875' from StackOverflow:
    // http://stackoverflow.com/questions/4774116/c-realpath-without-resolving-symlinks        

    char * res;
    size_t res_len;

    const char * ptr = src;
    const char * end = &src[src_len];
    const char * next;

    if (src_len == 0 || src[0] != '/') {
        // relative path
        char* pwd = NULL;
        size_t pwd_len;

        if (!(pwd = getcwd(NULL, 0))) {
            return NULL;
        }

        pwd_len = strlen(pwd);
        res = xmalloc(pwd_len + 1 + src_len + 1);
        memcpy(res, pwd, pwd_len);
        res_len = pwd_len;
        free(pwd);
    } else {
        res = xmalloc((src_len > 0 ? src_len : 1) + 1);
        res_len = 0;
    }

    for (ptr = src; ptr < end; ptr=next+1) {
        size_t len;
        next = memchr(ptr, '/', end-ptr);
        if (next == NULL) {
            next = end;
        }
        len = next-ptr;
        switch(len) {
        case 2:
            if (ptr[0] == '.' && ptr[1] == '.') {
                const char * slash = strrchr(res, '/');
                if (slash != NULL) {
                    res_len = slash - res;
                }
                continue;
            }
            break;
        case 1:
            if (ptr[0] == '.') {
                continue;
            }
            break;
        case 0:
            continue;
        }
        res[res_len++] = '/';
        memcpy(&res[res_len], ptr, len);
        res_len += len;
    }

    if (res_len == 0) {
        res[res_len++] = '/';
    }
    res[res_len] = '\0';
    return res;
}

char* ag_find_project_file() {
    const int size = 2;
    const char* files[size] = { "../agnostic.yaml", "agnostic.yaml" };
    const char* relative = NULL;
    for (int i = 0; i < 2; ++i) {
        if (file_exist(files[i])) {
            relative = files[i];
            break;
        }
    }
    if (!relative) {
        return NULL;
    }
    return normalize_path(relative, strlen(relative));  // realpath() doesn't work here, since we DON'T want to resolve symlinks
}

struct ag_component* ag_find_current_component(struct ag_project* project) {
    assert(project);

    char* buf = getcwd(NULL, 0);
    if (!buf) {
        return NULL;
    }
    if (!strcmp(project->dir, buf)) {
        // we're in the project directory -> no current component.
        free(buf);
        return NULL;
    }
    char* name = strrchr(buf, '/');
    name = name ? (name + 1) : buf;
    struct ag_component* ret = ag_find_component(project, name);
    free(buf);
    return ret;
}

struct ag_component* ag_find_component(struct ag_project* project, const char* name_or_alias) {
    assert(project);
    assert(name_or_alias);

    struct ag_component_list* l = project->components;
    struct ag_component* ret = NULL;
    while (l && !ret) {
        if ((l->component->name && !strcmp(l->component->name, name_or_alias)) || 
            (l->component->alias && !strcmp(l->component->alias, name_or_alias))) {
            ret = l->component;
        }
        l = l->next;
    }
    return ret;
}

char* ag_component_dir(struct ag_project* project, struct ag_component* component) {
    assert(project);
    assert(project->dir);
    assert(component);
    assert(component->name);

    char* ret = NULL;
    asprintf(&ret, "%s/%s", project->dir, component->name);
    return ret;
}

static int is_component_up_in_branch(struct ag_project* project, struct ag_component* leaf, const char* name) {
    if (!leaf) {
        return 0;
    }
    if (!strcmp(leaf->name, name) || (leaf->alias && !strcmp(leaf->alias, name))) {
        return 1;
    }
    for (struct ag_string_list* slist = leaf->build_after; slist; slist = slist->next) {
        if (is_component_up_in_branch(project, ag_find_component(project, slist->s), name)) {
            return 1;
        }
    }
    return 0;
}

static struct ag_component_list* fill_build_up_list(struct ag_component_list* old_root, struct ag_project* project, struct ag_component* component, 
    const char* up_to_component) {

    // TODO: heavy code here and in is_component_up_in_branch() function. Need to rewrite in a more efficient way

    if (!old_root) {
        return NULL;
    }

    struct ag_component_list* new_root = old_root;
    
    for (struct ag_string_list* slist = component->build_after; slist; slist = slist->next) {
        const char* s = slist->s;

        bool found = false;
        for (struct ag_component_list* l = project->components; l && !found; l = l->next) {
            if (!strcmp(l->component->name, s)) {
                if (!up_to_component || is_component_up_in_branch(project, l->component, up_to_component)) {
                    new_root = ag_create_component_node(l->component, new_root);
                    new_root = fill_build_up_list(new_root, project, l->component, up_to_component);
                }
                found = true;
            }
        }

        if (!found || !new_root) {
            ag_shallow_free_component_list(new_root);
            return NULL;
        }
    }

    return new_root;
}

static void remove_duplicates(struct ag_component_list* list) {
    for (struct ag_component_list* i = list; i; i = i->next) {
        for (struct ag_component_list* j = i->next, *prev_j = i; j; prev_j = j, j = j->next) {
            if (!strcmp(i->component->name, j->component->name)) {
                prev_j->next = j->next;
                free(j);
                j = prev_j;
            }
        }
    }
}

struct ag_component_list* ag_build_up_list(struct ag_project* project, struct ag_component* component, const char* up_to_component) {
    assert(project);
    assert(component);
    struct ag_component_list* ret = fill_build_up_list(ag_create_component_node(component, NULL), project, component, up_to_component);
    remove_duplicates(ret);
    return ret;
}

static struct ag_component_list* fill_build_down_list(struct ag_component* root, struct ag_project* project, struct ag_component* down_cmp) {
    if (!root) {
        return NULL;
    }

    struct ag_component_list* ret = NULL;
    struct ag_component_list* l = NULL;

    struct ag_component_list* queue_head = ag_create_component_node(root, NULL);
    struct ag_component_list* queue_total_head = queue_head;
    struct ag_component_list* queue_tail = queue_head;
    while (queue_head) {
        struct ag_component* c = queue_head->component;
        if (l) {
            l->next = ag_create_component_node(c, NULL);
            l = l->next;
        } else {
            ret = ag_create_component_node(c, NULL);
            l = ret;
        }
        for (struct ag_component_list* pl = project->components; pl; pl = pl->next) {
            struct ag_component* pc = pl->component;
            for (struct ag_string_list* sl = pc->build_after; sl; sl = sl->next) {
                if (!strcmp(sl->s, c->name)) {
                    if (!down_cmp || is_component_up_in_branch(project, down_cmp, pc->name)) {
                        queue_tail->next = ag_create_component_node(pc, NULL);
                        queue_tail = queue_tail->next;
                    }
                    break;
                }
            }
        }
        queue_head = queue_head->next;
    }
    ag_shallow_free_component_list(queue_total_head);
    return ret;
}

struct ag_component_list* ag_build_down_list(struct ag_project* project, struct ag_component* component, const char* down_to_component) {
    assert(project);
    assert(component);
    struct ag_component* down_cmp = NULL;
    if (down_to_component) {
        down_cmp = ag_find_component(project, down_to_component);
    }
    struct ag_component_list* ret = fill_build_down_list(component, project, down_cmp);
    remove_duplicates(ret);
    return ret;
}

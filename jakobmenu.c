/*
Copyright 2022 Vlad Mesco

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>

#include <unistd.h>
#include <dirent.h>

#include <sys/queue.h>
#include <sys/types.h>

// turn to 1 if running a mem checker or something
#ifndef DELETE_CATEGORIES
# define DELETE_CATEGORIES 0
#endif

#define OPTSTRING "hVp:a"

extern char* optarg;
extern int opterr, optind, optopt;

static int useAllCategories = 0;

SLIST_HEAD(dirshead, entry) dirs;
struct entry {
    const char* path;
    SLIST_ENTRY(entry) entries;
};

struct item {
    char *Name, *Exec, *Category, *Icon, *Path;
    int useTerminal;
};
#define INIT_ITEM(p) do{\
    memset(p, 0, sizeof(struct item));\
}while(0)

struct item* new_item(
        char* Name,
        char* Exec,
        char* Category,
        char* Icon,
        char* Path,
        int useTerminal)
{
    struct item* rval = malloc(sizeof(struct item));
    INIT_ITEM(rval);
    assert(Name);
    assert(Exec);
    rval->Name = Name;
    rval->Exec = Exec;
    if(Category)
        rval->Category = Category;
    else
        rval->Category = strdup("Misc");
    if(Icon) rval->Icon = Icon;
    if(Path) rval->Path = Path;
    rval->useTerminal = useTerminal;
    return rval;
}

static inline void delete_item(struct item** item)
{
    free((*item)->Name);
    free((*item)->Exec);
    free((*item)->Category);
    free((*item)->Icon);
    free((*item)->Path);
    free(*item);
    *item = NULL;
}

#define DEFAULT_CAPACITY 2
size_t ccategories = DEFAULT_CAPACITY;
struct category {
    char* name;
    size_t cmembers, nmembers;
    struct item **members;
} **categories = NULL;
size_t ncategories = 0;
#define INIT_CATEGORY(p) do{\
    memset(p, 0, sizeof(struct category));\
    p->cmembers = DEFAULT_CAPACITY;\
    p->nmembers = 0;\
    p->members = calloc(p->cmembers, sizeof(struct item*));\
}while(0)

static inline void delete_category(struct category** category)
{
    struct item** end = (*category)->members + (*category)->nmembers;
    for(struct item** p = (*category)->members; p != end; ++p) {
        if(strcmp((*p)->Category, (*category)->name) == 0) {
            delete_item(p);
        } else {
            *p = NULL;
        }
    }
    free((*category)->name);
    free((*category)->members);
    free(*category);
    *category = NULL;
}

int compare_categories(const void* left, const void* right)
{
    assert(left);
    assert(right);

    return strcmp((*(struct category**)left)->name, (*(struct category**)right)->name);
}

int compare_items(const void* left, const void* right)
{
    assert(left);
    assert(right);

    return strcmp((*(struct item**)left)->Name, (*(struct item**)right)->Name);
}

struct category* append_category(const char* name) 
{
    if(ncategories >= ccategories) {
        ccategories *= 2;
        categories = realloc(categories, ccategories * sizeof(struct category*));
    }
    categories[ncategories] = malloc(sizeof(struct category));
    INIT_CATEGORY(categories[ncategories]);
    categories[ncategories]->name = strdup(name);
    ncategories++;
    return categories[ncategories-1];
}

#define ADD_MEMBER(CATEGORY, newMember) do{\
    if(CATEGORY->nmembers >= CATEGORY->cmembers) {\
        CATEGORY->cmembers *= 2;\
        CATEGORY->members = realloc(CATEGORY->members, CATEGORY->cmembers * sizeof(struct item*));\
    }\
    CATEGORY->members[CATEGORY->nmembers++] = newMember;\
}while(0)

static inline struct category* get_category(const char* category)
{
    for(int i = 0; i < ncategories; ++i) {
        if(strcmp(categories[i]->name, category) == 0) {
            return categories[i];
        }
    }
    return NULL;
}


static char* menuId = "jakobmenu-1";
static char* menuTitle = "jakobmenu";

static char* expand(const char* path)
{
    if(!path || !*path) return NULL;

    char* rval = NULL;

    // Deal with ~/
    // TODO handle ~user/Desktop at some point
    if(path[0] == '~' && (
                    path[1] == '/' ||
                    path[1] == '\0'
                ))
    {
        char* home = getenv("HOME");
        if(!home)
        {
            fprintf(stderr, "%s:%d: Failed to resolve $HOME\n",
                    __FILE__, 
                    __LINE__);
            return NULL;
        }
        size_t homeLen = strlen(home);
        int homeEndsInSlash = home[homeLen - 1] == '/';
        const char* pathWithoutHomePrefix = path + 2;

        size_t newSz = strlen(pathWithoutHomePrefix)
            + strlen(home)
            + !homeEndsInSlash
            + 1;
        rval = (char*)malloc(newSz);
        strcpy(rval, home);
        if(!homeEndsInSlash) {
            strcat(rval, "/");
        }
        strcat(rval, pathWithoutHomePrefix);
    } else {
        rval = strdup(path);
    }

    return rval;
}

static void addPath(const char* path)
{
    assert(path);
    char* expandedPath = expand(path);
    struct entry* e = (struct entry*)malloc(sizeof(struct entry));
    memset(e, 0, sizeof(struct entry));
    e->path = expandedPath;
    SLIST_INSERT_HEAD(&dirs, e, entries);
}

/**
 * splitByEquals
 *
 * Splits a line that looks like key=value into key and value.
 * This function does not allocate memory.
 * This function WILL touch line.
 *
 * line     the input line
 * key      text up to the first =, stripped of leading and trailing spaces
 * value    text after the first =, stripped of leading and trailing spaces
 * @returns 1 if there was an =, 0 if it couldn't parse
 */
static int splitByEquals(char* line, char** key, char**value)
{
    char* equals = strchr(line, '=');

    if(!equals)
        return 0;

    char* valueTail = NULL;
    *value = equals + 1;
    valueTail = equals + strlen(equals);
    while(**value && isspace(**value))
        (*value)++;
    while(valueTail > *value && isspace(*(valueTail - 1)))
        --valueTail;
    *valueTail = '\0';

    *equals = '\0';
    *key = line;
    while(**key && isspace(**key))
        (*key)++;
    while(equals > *key && isspace(*(equals - 1)))
        --equals;
    *equals = '\0';

    return 1;
}

static void parseRC(const char* path)
{
    char* expandedPath = expand(path);

    FILE* f = fopen(expandedPath, "r");
    if(!f) {
        //fprintf(stderr, "Failed to open %s for reading\n", path);
        goto end1;
    }

    int lineNo = 0;
    while(!feof(f)) {
        int lineCap = 128;
        char* line = (char*)malloc(lineCap);
        assert(line);
        int lineLen = 0;
        line[lineLen] = '\0';
        // read a line
        do {
            int c = fgetc(f);
            if(feof(f) || c < 0 || c == '\n') break;

            if(c == '\\') {
                c = fgetc(f);
                if(feof(f) || c < 0) break;
            }

            // comment -- ignore rest of line
            if(c == '#') {
                do {
                    c = fgetc(f);
                    if(feof(f) || c == '\n' || c < 0) break;
                } while(!feof(f));
                break;
            }

            line[lineLen++] = (char)(c & 0xFF);
            if(lineLen >= lineCap - 1/*terminator*/) {
                lineCap *= 2;
                line = (char*)realloc(line, lineCap);
                assert(line);
            }
        } while(!feof(f));
        lineNo++;
        line[lineLen] = '\0';

        // parse line
        char* key = NULL, *value = NULL;
        if(!splitByEquals(line, &key, &value)) {
            int i = 0;

            // determine if it's a syntax error or whitespace/comments
            for(i = 0; i < strlen(line); ++i) {
                if(isspace(line[i])) continue;
                free(line);
                fprintf(stderr, "Invalid syntax in file %s line %d\n",
                        expandedPath, lineNo);
                goto end2;
            }

            free(line);
            continue;
        } else { // if(splitByEquals...)
            if(strcmp(key, "path") == 0) {
                assert(value);
                if(strlen(value) == 0) {
                    fprintf(stderr, "Invalid syntax in file %s line %d: expected value\n", expandedPath, lineNo);
                    goto end2;
                }
                addPath(value);
                free(line);
                continue;
            } else if(strcmp(key, "useAllCategories") == 0) {
                assert(value);
                if(strlen(value) == 0) {
                    fprintf(stderr, "Invalid syntax in file %s line %d: expected value\n", expandedPath, lineNo);
                    goto end2;
                }
                int logicalValue = atoi(value);
                useAllCategories = logicalValue != 0;
            } else {
                free(line);
                fprintf(stderr, "Invalid syntax in file %s line %d\n",
                        expandedPath, lineNo);
                goto end2;
            }
            // never reached
        } // if(!equals) ... else
    } // while(!feof(f))

end2:
    fclose(f);

end1:
    free(expandedPath);
}

static int sectionType(const char* line)
{
    while(*line && isspace(*line)) {
        line++;
    }
    if(*line != '[') return 0;
    if(strncmp("[Desktop Entry]", line, strlen("[Desktop Entry]")) == 0) return 1;
    return 2;
}

static void parseDotDesktop(const char* path)
{
    FILE* f = fopen(path, "r");
    if(!f)
        return;

    // information extracted from a .desktop file
    char *Name = NULL, *Exec = NULL, *Icon = NULL;
    char *Categories = NULL, *Path = NULL;
    int isOk = 1, useTerminal = 0;
    // isOk will be set to 0 if it's something we shouldn't/can't show

    // state machine:
    // 0 - ignore everything until [Desktop Entry] is encountered
    // 1 - extract Name, Exec etc
    // 2 - different section, ignore
    int foundDesktopEntry = 0;

    while(!feof(f) && foundDesktopEntry < 2) {
        size_t lineCap = 128;
        char* line = malloc(lineCap);
        size_t lineLen = 0;
        int c = 0;
        line[0] = '\0';
        // read a line
        do {
            c = fgetc(f);
            if(c < 0 || feof(f) || c == '\n') break;
            // comment -- ignore everything until end of line
            if(c == '#') {
                do {
                    c = fgetc(f);
                    if(feof(f) || c == '\n' || c < 0) break;
                } while(!feof(f));
                break;
            }
            line[lineLen++] = (c & 0xFF);
            if(lineLen >= lineCap - 1/*terminator*/) {
                lineCap *= 2;
                line = realloc(line, lineCap);
            }
        } while(!feof(f));
        // add terminator
        line[lineLen] = '\0';

        // see foundDesktopEntry
        switch(sectionType(line)) {
            default:
            case 0:
                break;
            case 1:
                foundDesktopEntry = 1;
                break;
            case 2:
                if(foundDesktopEntry) {
                    foundDesktopEntry = 2;
                }
                break;
        }

        // parse statements
        char *key = NULL, *value = NULL;
        if(foundDesktopEntry == 1 && splitByEquals(line, &key, &value)) {
            if(strcmp(key, "Type") == 0) {
                isOk = isOk && (strcmp(value, "Application") == 0);
            } else if(strcmp(key, "Hidden") == 0) {
                isOk = isOk && (strcmp(value, "true") != 0);
            } else if(strcmp(key, "NoDisplay") == 0) {
                isOk = isOk && (strcmp(value, "true") != 0);
            } else if(strcmp(key, "Name") == 0) {
                Name = strdup(value);
            } else if(strcmp(key, "Icon") == 0) {
                Icon = strdup(value);
            } else if(strcmp(key, "Exec") == 0) {
                char* ss = NULL;
                Exec = strdup(value);
                // strip %U, %u, %f, openbox cannot provide that
                while((ss = strstr(Exec, "%U")) != NULL) {
                    ss[0] = ' ';
                    ss[1] = ' ';
                }
                while((ss = strstr(Exec, "%u")) != NULL) {
                    ss[0] = ' ';
                    ss[1] = ' ';
                }
                while((ss = strstr(Exec, "%F")) != NULL) {
                    ss[0] = ' ';
                    ss[1] = ' ';
                }
                while((ss = strstr(Exec, "%f")) != NULL) {
                    ss[0] = ' ';
                    ss[1] = ' ';
                }
            } else if(strcmp(key, "Categories") == 0) {
                Categories = strdup(value);
            } else if(strcmp(key, "Path") == 0) {
                Path = strdup(value);
            } else if(strcmp(key, "Terminal") == 0) {
                useTerminal = (strcmp(value, "true") == 0);
            }
        }

        free(line);
    }

    // if we're ok and we have at least Name and Exec...
    isOk = isOk && Name && Exec;

    if(isOk) {
        // grab first category
        char* foundSemicolon = Categories ? strchr(Categories, ';') : NULL;
        if(foundSemicolon) *foundSemicolon = '\0';
        // create a menu item
        struct item* item = new_item(Name, Exec, Categories, Icon, Path, useTerminal);
        // add it to its main categories
        struct category* category = get_category(item->Category);
        if(!category) {
            category = append_category(item->Category);
        }
        ADD_MEMBER(category, item);

        if(useAllCategories) {
            // also add it to all other categories
            while(foundSemicolon != NULL) {
                char* base = foundSemicolon + 1;
                foundSemicolon = strchr(base, ';');
                if(foundSemicolon) {
                    *foundSemicolon = '\0';
                }
                // skip over BS
                if(strlen(base) == 0) continue;
                if(strstr(base, "X-") == base) continue;
                if(strstr(base, "x-") == base) continue;
                category = get_category(base);
                if(!category) {
                    category = append_category(base);
                }
                ADD_MEMBER(category, item);
            }
        }
    } else {
        free(Name);
        free(Exec);
        free(Categories);
        free(Icon);
        free(Path);
    }

    fclose(f);
}

static void parseAll()
{
    struct entry *np = NULL;
    for(np = SLIST_FIRST(&dirs); np != NULL; np = SLIST_NEXT(np, entries)) {
        DIR* dir;
        struct dirent* dep = NULL;
        dir = opendir(np->path);
        if(!dir) continue;
        while((dep = readdir(dir)) != NULL) {
            static const char dotDesktop[] = ".desktop";
            static const size_t dotDesktopLen = sizeof(dotDesktop) - 1;
            size_t namelen = strlen(dep->d_name);
            if(namelen > dotDesktopLen &&
                    strcmp(dep->d_name + namelen - dotDesktopLen, dotDesktop) == 0)
            {
                int endsInSlash = (*np->path && np->path[strlen(np->path)-1] == '/');
                char* fullPath = (char*)malloc(strlen(np->path) + !endsInSlash + namelen + 1);
                strcpy(fullPath, np->path);
                if(!endsInSlash) strcat(fullPath, "/");
                strcat(fullPath, dep->d_name);
                parseDotDesktop(fullPath);
                free(fullPath);
            }
        }
        closedir(dir);
    }
}

void version(int yesexit)
{
    fprintf(stderr,
"jakobmenu %s -- An openbox pipe menu that parses .desktop files" "\n"
        , VERSION
        );
    if(yesexit) exit(2);
}

void usage(const char* argv0)
{
    version(0);
    fprintf(stderr,
"" "\n"
"Usage: %s [-%s]" "\n"
"\t"    "-h                     prints this message and exits" "\n"
"\t"    "-V                     prints version information and exits" "\n"
"\t"    "-a                     duplicate items in all declared categories" "\n"
"\t"    "-p /some/path/         add a search path" "\n"
"" "\n"
"This program will output an <openbox_pipe_menu/> structure compatible" "\n"
"with OpenBox." "\n"
"" "\n"
"See %s%s." "\n"
"" "\n"
"A sample configuration file should be available here: \n"
"\t"    "%s%s" "\n"
"" "\n"
        , argv0, OPTSTRING
        , PREFIX, "/share/doc/jakobmenu/README.md"
        , PREFIX, "/share/jakobmenu/jakobmenu.conf"
        );
    // TODO use err
    exit(2);
}

#if HAVE_UNVEIL
void unveilAll()
{
    struct entry *np = NULL;
    for(np = SLIST_FIRST(&dirs); np != NULL; np = SLIST_NEXT(np, entries)) {
        DIR* dir;
        struct dirent *dep = NULL;
        unveil(np->path, "r");
        dir = opendir(np->path);
        if(!dir) continue;
        while((dep = readdir(dir)) != NULL) {
            if(dep == NULL) break;

            static const char dotDesktop[] = ".desktop";
            static const size_t dotDesktopLen = sizeof(dotDesktop) - 1;
            size_t namelen = strlen(dep->d_name);
            if(namelen > dotDesktopLen &&
                    strcmp(dep->d_name + namelen - dotDesktopLen, dotDesktop) == 0)
            {
                int endsInSlash = (*np->path && np->path[strlen(np->path)-1] == '/');
                char* fullPath = (char*)malloc(strlen(np->path) + !endsInSlash + namelen + 1);
                strcpy(fullPath, np->path);
                if(!endsInSlash) strcat(fullPath, "/");
                strcat(fullPath, dep->d_name);
                unveil(fullPath, "r");
                free(fullPath);
            }
        }
        closedir(dir);
    }
}
#endif

int main(int argc, char* argv[])
{
    SLIST_INIT(&dirs);

#if HAVE_PLEDGE
    // pledges
    if(pledge("stdio rpath unveil", NULL))
        err(1, "Failed to pledge");
#endif

    char* realHomeConf = expand(HOME_CONF);

#if HAVE_UNVEIL
    // unveil rc files
    unveil(ETC_CONF, "r");
    unveil(realHomeConf, "r");
#endif

    // parse rc files
    parseRC(ETC_CONF);
    parseRC(realHomeConf);

    free(realHomeConf);

    // parse command line
    // TODO
    // - add command line flag to create submenus per categories, or per top level path
    // - add rc commands for the above
    // - add command line flag to skip parsing config files, and change the order we parse things in... pfff
    int ch;
    while((ch = getopt(argc, argv, OPTSTRING)) != -1) {
        switch(ch) {
            // FIXME this should probably be handled before calls to parseRC...
            case 'h':
                usage(argv[0]);
                break;
            case 'V':
                version(1);
                break;
            case 'p':
                addPath(optarg);
                break;
            case 'a':
                useAllCategories = 1;
                break;
            default:
                usage(argv[0]);
        }
    }
    argc -= optind;
    argv += optind;

#if HAVE_UNVEIL
    // unveil all .desktop files
    unveilAll();

    // no more unveils
    unveil(NULL, NULL);
#endif

#if HAVE_PLEDGE
    // no further pledges
    pledge("stdio rpath", NULL);
    pledge(NULL, NULL);
#endif

    categories = calloc(DEFAULT_CAPACITY, sizeof(struct category*));

    // parse all files
    parseAll();
    qsort(categories, ncategories, sizeof(struct category*), &compare_categories);

    printf("<openbox_pipe_menu>\n");
    for(int i = 0; i < ncategories; ++i) {
        struct category *category = categories[i];
        printf(" <menu id=\"%s\" label=\"%s\">\n", category->name, category->name);
        qsort(category->members, category->nmembers, sizeof(struct item*), &compare_items);
        for(int j = 0; j < category->nmembers; ++j) {
            char* buffer = NULL;
            const char* toExec = category->members[j]->Exec;
            if(category->members[j]->useTerminal) {
                buffer = malloc(strlen(toExec) + strlen("xterm -e ") + 1);
                strcpy(buffer, "xterm -e ");
                strcat(buffer, toExec);
                toExec = buffer;
            }
            printf("  <item label=\"%s\"><action name=\"Execute\"><execute>"
                    "%s</execute></action></item>\n",
                    category->members[j]->Name,
                    toExec);
            free(buffer);
        }
        printf(" </menu>\n");
    }
    printf("</openbox_pipe_menu>\n");

#if DELETE_CATEGORIES
    for(struct category** p = categories; p != categories + ncategories; ++p) {
        delete_category(p);
    }
    while(!SLIST_EMPTY(&dirs)) {
        struct entry* n = SLIST_FIRST(&dirs);
        SLIST_REMOVE_HEAD(&dirs, entries);
        free(n);
    }
#endif

    return 0;
}

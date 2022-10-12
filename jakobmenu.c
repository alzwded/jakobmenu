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

extern char* optarg;
extern int opterr, optind, optopt;

SLIST_HEAD(dirshead, entry) dirs;
struct entry {
    const char* path;
    SLIST_ENTRY(entry) entries;
};

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
        do {
            int c = fgetc(f);
            if(feof(f) || c < 0 || c == '\n') break;

            if(c == '\\') {
                c = fgetc(f);
                if(feof(f) || c < 0) break;
            }

            if(c == '#') {
                do {
                    c = fgetc(f);
                    if(feof(f) || c == '\n' || c < 0) break;
                } while(1);
                break;
            }

            line[lineLen++] = (char)(c & 0xFF);
            if(lineLen >= lineCap) {
                lineCap *= 2;
                line = (char*)realloc(line, lineCap);
                assert(line);
            }
        } while(1);
        lineNo++;
        line[lineLen] = '\0';

        // process line
        char* key = NULL, *value = NULL;
        if(!splitByEquals(line, &key, &value)) {
            int i = 0;

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
                addPath(value);
                free(line);
                continue;
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

static void parseDotDesktop(const char* path)
{
    FILE* f = fopen(path, "r");
    if(!f)
        return;

    char *Name = NULL, *Exec = NULL, *Icon = NULL;
    char *Categories = NULL, *Path = NULL;
    int isOk = 1, useTerminal = 0;

    while(!feof(f)) {
        size_t lineCap = 128;
        char* line = malloc(lineCap);
        size_t lineLen = 0;
        int c = 0;
        line[0] = '\0';
        do {
            c = fgetc(f);
            if(c < 0 || feof(f) || c == '\n') break;
            line[lineLen++] = (c & 0xFF);
            if(lineLen >= lineCap) {
                lineCap *= 2;
                line = realloc(line, lineCap);
            }
        } while(1);
        line[lineLen] = '\0';

        char *key = NULL, *value = NULL;
        if(splitByEquals(line, &key, &value)) {
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
                // strip %U, openbox cannot provide that
                while((ss = strstr(Exec, "%U")) != NULL) {
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

    if(isOk) {
        printf("  <item label=\"%s\"><action name=\"Execute\"><execute>"
                "%s</execute></action></item>\n",
                Name,
                Exec);
    }

    free(Name);
    free(Exec);
    free(Icon);
    free(Categories);
    free(Path);

ret1:
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

void version()
{
    fprintf(stderr, "TODO version\n");
    // TODO use err
    exit(2);
}

void usage()
{
    fprintf(stderr, "TODO usage\n");
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
    if(pledge("stdio rpath", NULL))
        err(1, "Failed to pledge");
#endif

    // parse command line
    // TODO
    // - add command line flag for menu name
    // - add command line flag to create submenus per categories, or per top level path
    // - add rc commands for the above
    int ch;
    while((ch = getopt(argc, argv, "hVp:")) != -1) {
        switch(ch) {
            // FIXME this should probably be handled before calls to parseRC...
            case 'h':
                usage();
                break;
            case 'V':
                version();
                break;
            case 'p':
                addPath(optarg);
                break;
            default:
                usage();
        }
    }
    argc -= optind;
    argv += optind;

#if HAVE_UNVEIL
    // unveil rc files
    unveil(ETC_CONF, "r");
    unveil(HOME_CONF, "r");

    // unveil all .desktop files
    unveilAll();

    // no more unveils
    unveil(NULL, NULL);
#endif

#if HAVE_PLEDGE
    // no further pledges
    pledge(NULL, NULL);
#endif

    printf("<openbox_pipe_menu>\n");

    printf(" <menu id=\"%s\" label=\"%s\">\n", menuId, menuTitle);

    // parse rc files
    parseRC(ETC_CONF);
    parseRC(HOME_CONF);

    // parse all files
    parseAll();

    printf(" </menu>\n");
    printf("</openbox_pipe_menu>\n");

    return 0;
}

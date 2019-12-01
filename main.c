/*
 *
 * Copyright (c) 2011-2019, Roman Kraevskiy <rkraevskiy@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */



/*
 *  TODO:
 *  + ackrc
 *  - utf8
 *  + OOM
 *  - exit codes
 *  + ctx->data == NULL
 *  - cleanup
 *  + errors handling
 *  + types
 *  + input from pipe
 *  + input from file/directory
 *  + help types
 * --line=NUM            Only print line(s) NUM of each file
 * +-w, --word-regexp    Force PATTERN to match only whole words
 * +-Q, --literal        Quote all metacharacters; PATTERN is literal
 * +--[no]smart-case     Ignore case distinctions in PATTERN,
 *                       only if PATTERN contains no upper case
 *                       Ignored if -i is specified
 * +with perl ack bugs -o                   Show only the part of a line matching PATTERN
 *                       (turns off text highlighting)
 * --output=expr        Output the evaluation of expr for each line
 *                       (turns off text highlighting)
 * --pager=COMMAND       Pipes all ack output through COMMAND.  For example,
 * --pager="less -R".  Ignored if output is redirected.
 * +--nopager             Do not send output through a pager.  Cancels any
 * setting in ~/.ackrc, ACK_PAGER or ACK_PAGER_COLOR.
 * +--[no]color           Highlight the matching text (default: on unless
 *                       output is redirected, or on Windows)
 * +--[no]colour          Same as --[no]color
 * +--color-filename=COLOR
 * +--color-match=COLOR
 * +--color-lineno=COLOR  Set the color for filenames, matches, and line numbers.
 * +--flush               Flush output immediately, even when ack is used
 *                       non-interactively (when output goes to a pipe or
 *                       file).
 * +--perl                Include only Perl files.
 * +--type=perl           Include only Perl files.
 * +--noperl              Exclude Perl files.
 * +--[no]follow          Follow symlinks.  Default is off.
 *
 * +--noenv               Ignore environment variables and ~/.ackrc
 * +--thpppt              Bill the Cat
 * +--man                 Man page
 */

#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "pcre.h"
#include <stdlib.h>
#include <stdio.h>
#include <locale.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "queue.h"
#include <string.h>

#include <fcntl.h>

#if !defined(WINDOWS)
#   if defined(__MINGW32__) || defined(_WIN32) || defined (cygwin) || defined(__WIN32__) || defined(_WIN64) || defined(__TOS_WIN__) || defined(__WINDOWS__)
#       define WINDOWS
#   endif
#endif



#ifdef WINDOWS
#   define DIRSEPS "\\"
#   define DIRSEPC '\\'
#   define lstat stat

#include <io.h>
#include <fcntl.h>
#include <windows.h>

int scandir( const char *dirname, struct dirent ***namelist, int (*select)(const struct dirent *), int (*compar)(const void*, const void*));
int alphasort(const void *a, const void *b);

#ifndef PATH_MAX
#  define PATH_MAX MAX_PATH
#endif


#define STRNCASECMP _strnicmp
#define FILENAMECMP _stricmp 
#define FILENAMENCMP _strnicmp

#define F_LONGLONG "I64"
#else
#   define DIRSEPS "/"
#   define DIRSEPC '/'

#define STRNCASECMP strncasecmp
#define FILENAMECMP strcasecmp
#define FILENAMENCMP strncasecmp
#define F_LONGLONG "ll"
#endif


#if !defined O_NOATIME
# define O_NOATIME 0
#endif


#define USE_READ

#ifdef USE_READ
#   define FISGOOD(x) (x!=-1)
#   define FHANDLE int
#   define FREAD(fid,buf,size) read(fid,buf,size)
#   define FOPEN(name) open(name,O_RDONLY)
#   define FCLOSE(fid) close(fid)
#   define FSTDIN_HANDLE 0
#   define FSTDOUT_HANDLE 1
#else
#   define FISGOOD(x) (x!=NULL)
#   define FHANDLE FILE*
#   define FREAD(fid,buf,size) fread(buf,1,size,fid)
#   define FOPEN(name) fopen(name,"rb")
#   define FCLOSE(fid) fclose(fid)
#   define FSTDIN_HANDLE stdin
#   define FSTDOUT_HANDLE stdout
#endif

#define BUFFER_SIZE 64*1024

#ifdef DEBUG
static void* (*x_malloc)(size_t) = malloc;
static void* (*x_calloc)(size_t,size_t) = calloc;
static void (*x_free)(void*) = free;
static char* (*x_strdup)(const char*) = strdup;
#define malloc dmalloc
#define free dfree
#define calloc dcalloc
#define strdup dstrdup
long malloced=0;
#endif

#define stringify(name) #name
#define STR(name) stringify(name)

#define false 0
#define true !false
#define bool int
#define OFFSETS_SIZE 120
#define numberof(x) (sizeof(x)/sizeof((x)[0]))
#define MATCH   0
#define NOMATCH 1

typedef struct{
    int size;
    char *bits;
}bitfiels_t;

typedef struct{
    char *buf;
    size_t allocated;
    size_t used;
    size_t start;
}buf_t;



typedef struct {
    int start;
    int len;
}match_t;

typedef struct re{
    pcre *re;
    pcre_extra *pe;
    int plen;
    char *pattern;
    int (*findall)(struct re *re,const char *str,long len,match_t *matches, int matches_len);
}re_t;

typedef struct ext{
    LIST_ENTRY(ext) next;
    char *ext;
    int len;
    struct filetype *type;
}ext_t;

typedef LIST_HEAD(ext_list,ext) ext_list_t;

typedef struct filetype{
    LIST_ENTRY(filetype) next;
    char *name;
    int namelen;
    int i;
    int wanted;
}filetype_t;

typedef LIST_HEAD(filetypes_list,filetype) filetypes_list_t;


typedef struct string{
    LIST_ENTRY(string) next;
    char *str;
    int len;
}string_t;


typedef int (*str_cmp_func_t)(const char *,const char *,size_t len);

typedef LIST_HEAD(string_list,string) string_list_t;


filetype_t *find_filetype(char *filetype);

static const unsigned char *pcretables = NULL;


struct {
    int v; /* -v, --invert-match    Invert match: select non-matching lines */
    int w; /* -w, --word-regexp     Force PATTERN to match only whole words */
    int Q; /* -Q, --literal         Quote all metacharacters; PATTERN is literal */
    int i; /* -i, --ignore-case     Ignore case distinctions in PATTERN */
    int smart_case; /* --[no]smart-case      Ignore case distinctions in PATTERN,
                       only if PATTERN contains no upper case
                       Ignored if -i is specified */
    long line; /* --line=NUM            Only print line(s) NUM of each file */
    int l; /* -l, --files-with-matches Only print filenames containing matches */
    int L; /* -L, --files-without-matches Only print filenames with no matches */
    int o; /* -o Show only the part of a line matching PATTERN (turns off text highlighting) */
    int passthru; /* --passthru  Print all lines, whether matching or not */
    char *output; /*  --output=expr  Output the evaluation of expr for each line (turns off text highlighting) */
    re_t match; /* --match PATTERN       Specify PATTERN explicitly. */
    char *match_pattern;
    long m; /* -m, --max-count=NUM   Stop searching in each file after NUM matches */
    int one; /*  -1 Stop searching after one match of any kind */
    int H; /* -H, --with-filename   Print the filename for each match */
    int h; /* -h, --no-filename     Suppress the prefixing filename on output */
    int c; /* -c, --count   Show number of lines matching per file */
    int column; /* --column Show the column number of the first match */
    int A; /* -A NUM, --after-context=NUM Print NUM lines of trailing context after matching lines. */
    int B; /* -B NUM, --before-context=NUM Print NUM lines of leading context before matching lines. */
    int C; /*  -C [NUM], --context[=NUM]  Print NUM lines (default 2) of output context. */
    int print0; /*  --print0              Print null byte as separator between filenames,
                    only works with -f, -g, -l, -L or -c.*/
    char *pager; /*   --pager=COMMAND       Pipes all ack output through COMMAND.  For example,
                      --pager="less -R".  Ignored if output is redirected.*/
    int nopager; /*   --nopager             Do not send output through a pager.  Cancels any
                      setting in ~/.ackrc, ACK_PAGER or ACK_PAGER_COLOR.
                      */
    int _break; /*  --[no]break           Print a break between results from different files.
                    (default: on when used interactively)*/
    int group; /*  --group               Same as --heading --break */
    int nogroup; /* --nogroup             Same as --noheading --nobreak */

    int heading; /*--[no]heading         Print a filename heading above each file's results.
                   (default: on when used interactively) */
    int flush; /* --flush               Flush output immediately, even when ack is used
                  non-interactively (when output goes to a pipe or
                  file). */
    int f; /* -f Only print the files found, without searching. The PATTERN must not be specified. */
    re_t g; /* -g REGEX              Same as -f, but only print files matching REGEX. */
    char *g_pattern;
    re_t G; /*   -G REGEX              Only search files that match REGEX */
    char *G_pattern;
    int sort_files; /* --sort-files          Sort the found files lexically. */
    int invert_file_match; /* --invert-file-match   Print/search handle files that do not match -g/-G.*/
    int show_types; /*   --show-types          Show which types each file has.*/
    int a; /* -a, --all-types       All file types searched;
              Ignores CVS, .svn and other ignored directories*/

    int u; /* -u, --unrestricted    All files and directories searched */
    int r; /* -r, -R, --recurse     Recurse into subdirectories (ack's default behavior) */
    int follow; /*   --[no]follow          Follow symlinks.  Default is off.*/
    int env; /* --(no)env */
    int help;
    int help_types;
    int version;
    int thpppt;

    int color;
    char *color_filename; /* --color-filename=COLOR */
    char *color_match; /* --color-match=COLOR */
    char *color_lineno; /* --color-lineno=COLOR  Set the color for filenames, matches, and line numbers.*/

    /* switches */
    int show_filename;
    char *line_end;
    int show_total;
    int recursive;
    int print_count0;
    int show_context;
    filetypes_list_t all_filetypes;
    ext_list_t exts;
    int nfiletypes;
    int types_type;
    int nexts;
    char *self_name;
    bitfiels_t *req_filetypes;
    string_list_t ignore_dirs;
    string_list_t file_list;
}opt;


typedef struct{
    FHANDLE f;
    char *fullname;
    char *name;
    int namelen;
    long nmatches;
    long line; /* current line */
    bitfiels_t *filetypes;
    int is_binary;
    int type_processed;
    buf_t buf;
}file_t;

struct {
    long files_matched;
    long total_matches;
    buf_t *history;
    int hused;
    int hprint;
    bitfiels_t *filetypes;
    long long size_processed;
    long file_processed;
    filetype_t *ft_text;
    filetype_t *ft_skipped;
    filetype_t *ft_make;
    filetype_t *ft_ruby;
    filetype_t *ft_binary;
    int offsets[OFFSETS_SIZE];
    int nmatches;
    match_t matches[OFFSETS_SIZE];
    file_t file;
}vars;


typedef struct{
    char *first;
    char *second;
}string_pairs_t;


string_pairs_t skip_dirs [] = {
    {".bzr","Bazaar"},
    {".cdv","Codeville"},
    {"~.dep","Interface Builder"},
    {"~.dot","Interface Builder"},
    {"~.nib","Interface Builder"},
    {"~.plst","Interface Builder"},
    {".git","Git"},
    {".hg","Mercurial"},
    {".pc","quilt"},
    {".svn","Subversion"},
    {"_MTN","Monotone"},
    {"blib","Perl module building"},
    {"CVS","CVS"},
    {"RCS", "RCS"},
    {"SCCS","SCCS"},
    {"_darcs","darcs"},
    {"_sgbak","Vault/Fortress"},
    {"autom4te.cache","autoconf"},
    {"cover_db","Devel::Cover"},
    {"_build","Module::Build"}
};



string_pairs_t colors[] = {
    {"clear","0"},
    {"reset","0"},
    {"dark",""},
    {"bold","1"},
    {"underline","4"},
    {"underscore","4"},
    {"blink","5"},
    {"reverse","7"},
    {"concealed","8"},
    {"black","30"},
    {"red","31"},
    {"green","32"},
    {"yellow","33"},
    {"blue","34"},
    {"magenta","35"},
    {"cyan","36"},
    {"white","37"},
    {"on_black","40"},
    {"on_red","41"},
    {"on_green","42"},
    {"on_yellow","43"},
    {"on_blue","44"},
    {"on_magenta","45"},
    {"on_cyan","46"},
    {"on_white","47"},
    {NULL,NULL}
};

struct{
    char *name;
    char *exts;
}file_types [] = {
    {"ada", ".ada,.adb,.ads"},
    {"actionscript",".as,.mxml"},
    {"apl",".apl"},
    {"asciidoc",".adoc,.ad,.asc,.asciidoc"},
    {"asm",".asm,.S"},
    {"awk",".awk"},
    {"batch",".bat,.cmd"},
    {"bitbake",".bb,.bbappend,.bbclass,.inc"},
    {"binary","Binary files (default: off)"},
    {"bro",".bro,.bif"},
    {"cc",".c,.h,.xs"},
    {"cfmx",".cfc,.cfm,.cfml"},
    {"chpl",".chpl"},
    {"clojure",".clj,.cljs,.cljc,.cljx"},
    {"coffee",".coffee,.cjsx"},
    {"config",".cfg,.conf"},
    {"coq",".coq,.g,.v"},
    {"cpp",".cpp,.cc,.cxx,.m,.hpp,.hh,.h,.hxx,.C,.H"},
    {"crystal",".cr,.ecr"},
    {"csharp",".cs"},
    {"css",".css"},
    {"ctx",".ctx"},
    {"cython",".pyx,.pxd,.pxi"},
    {"delphi",".pas,.int,.dfm,.nfm,.dof,.dpk,.dproj,.groupproj,.bdsgroup,.bdsproj"},
    {"dlang",".d,.di"},
    {"dot",".dot,.gv"},
    {"dts",".dts,.dtsi"},
    {"ebuild",".ebuild,.eclass"},
    {"elisp",".el"},
    {"elixir",".ex,.eex,.exs"},
    {"elm",".elm"},
    {"erlang",".erl,.hrl"},
    {"factor",".factor"},
    {"fortran",".f,.f77,.f90,.f95,.f03,.for,.ftn,.fpp"},
    {"fsharp",".fs,.fsi,.fsx"},
    {"gettext",".po,.pot,.mo"},
    {"glsl",".vert,.tesc,.tese,.geom,.frag,.comp"},
    {"go",".go"},
    {"groovy",".groovy,.gtmpl,.gpp,.grunit,.gradle"},
    {"haml",".haml"},
    {"handlebars",".hbs"},
    {"haskell",".hs,.lhs,.hsig"},
    {"haxe",".hx"},
    {"hh",".h"},
    {"html",".htm,.html,.shtml,.xhtml"},
    {"idris",".idr,.ipkg,.lidr"},
    {"ini",".ini"},
    {"ipython",".ipynb"},
    {"isabelle",".thy"},
    {"j",".ijs"},
    {"jade",".jade"},
    {"java",".java,.properties"},
    {"jinja2",".j2"},
    {"js",".js,.min.js,-min.js,.es6,.jsx,.vue"},
    {"json",".json"},
    {"jsp",".jsp,.jspx,.jhtm,.jhtml,.jspf,.tag,.tagf"},
    {"julia",".jl"},
    {"kotlin",".kt"},
    {"less",".less"},
    {"liquid",".liquid"},
    {"lisp",".lisp,.lsp"},
    {"log",".log"},
    {"lua",".lua"},
    {"make",".mk,.mak,Makefile"},
    {"mako",".mako"},
    {"markdown",".markdown,.mdown,.mdwn,.mkdn,.mkd,.md"},
    {"mason",".mas,.mhtml,.mpl,.mtxt"},
    {"matlab",".m"},
    {"mathematica",".m,.wl"},
    {"mercury",".m,.moo"},
    {"naccess",".asa,.rsa"},
    {"nim",".nim"},
    {"nix",".nix"},
    {"objc",".m,.h"},
    {"objcpp",".mm,.h"},
    {"ocaml",".ml,.mli,.mll,.mly"},
    {"octave",".m"},
    {"org",".org"},
    {"parrot",".pir,.pasm,.pmc,.ops,.pod,.pg,.tg"},
    {"pdb",".pdb"},
    {"perl",".pl,.pm,.pod,.t,.pm6"},
    {"php",".php,.phpt,.php3,.php4,.php5,.phtml"},
    {"pike",".pike,.pmod"},
    {"plist",".plist"},
    {"plone",".pt,.cpt,.metadata,.cpy,.py"},
    {"proto",".proto"},
    {"pug",".pug"},
    {"puppet",".pp"},
    {"python",".py"},
    {"qml",".qml"},
    {"racket",".rkt,.ss,.scm"},
    {"rake","Rakefiles"},
    {"restructuredtext",".rst"},
    {"rs",".rs"},
    {"r",".r,.R,.Rmd,.Rnw,.Rtex,.Rrst"},
    {"rdoc",".rdoc"},
    {"ruby",".rb,.rhtml,.rjs,.rxml,.erb,.rake,.spec,.haml"},
    {"rust",".rs"},
    {"salt",".sls"},
    {"sass",".sass,.scss"},
    {"scala",".scala"},
    {"scheme",".scm,.ss"},
    {"shell",".sh,.bash,.csh,.tcsh,.ksh,.zsh,.fish"},
    {"skipped","Files, but not directories, normally skipped (default: off)"},
    {"smalltalk",".st"},
    {"sml",".sml,.fun,.mlb,.sig"},
    {"sql",".sql,.ctl"},
    {"stata",".do,.ado"},
    {"stylus",".styl"},
    {"swift",".swift"},
    {"tcl",".tcl,.itcl,.itk"},
    {"terraform",".tf,.tfvars"},
    {"tex",".tex,.cls,.sty"},
    {"thrift",".thrift"},
    {"text","Text files (default: off)"},
    {"tla",".tla"},
    {"tt",".tt,.tt2,.ttml"},
    {"toml",".toml"},
    {"ts",".ts,.tsx"},
    {"twig",".twig"},
    {"vala",".vala,.vapi"},
    {"vb",".bas,.cls,.frm,.ctl,.vb,.resx,.vbs"},
    {"velocity",".vm,.vtl,.vsl"},
    {"verilog",".v,.vh,.sv"},
    {"vhdl",".vhd,.vhdl"},
    {"vim",".vim"},
    {"wix",".wxi,.wxs"},
    {"wsdl",".wsdl"},
    {"wadl",".wadl"},
    {"xml",".xml,.dtd,.xsl,.xslt,.ent,.tld,.plist"},
    {"yaml",".yaml,.yml"},

};

int re_findall(re_t *re,const char *str,long len,match_t *matches, int matches_len);
int str_findall(re_t *re,const char *str,long len,match_t *matches, int matches_len);
int str_casefindall(re_t *re,const char *str,long len,match_t *matches, int matches_len);
int is_regexp(char * str, int len);
void get_filetypes(file_t *file);
int _ends_with(const char *name,int nsize,const char *ext,int esize);
int is_searchable(file_t *);
char *_strnstr1(const char *s, const char *f, int sl);
char *_strnstr2(const char *s, int sl, const char *f, int fl);
char *_strnstr3(const char *s, int sl, const char *f, int fl);
char *_strncasestr(const char *s, int sl, const char *f, int fl);

#define strnstr _strnstr3


char* shells[] = {"bash","tcsh","ksh","zsh","ash","sh",",fish",NULL};
char* interprets[] = {"ruby","perl","php","python","lua","awk",NULL};



int isdir(char *filename){
    struct stat statbuf;

    if (lstat(filename, &statbuf) < 0)
        return 0;
    return ((statbuf.st_mode & S_IFMT) == S_IFDIR);
}




#ifdef DEBUG
void *dmalloc(size_t size){
    void * res;

    res = x_malloc(size);
    if (res)
        malloced++;
    return res;
}

void dfree(void* ptr){
    if (ptr){
        malloced--;
    }
    x_free(ptr);
}

void* dcalloc(size_t num,size_t size){
    void * res;

    res = x_calloc(num,size);
    if (res){
        malloced++;
    }
    return res;
}

char* dstrdup(char* str){
    char *res;

    res = x_strdup(str);
    if (res){
        malloced ++;
    }
    return res;
}

#endif




static string_t * string_new(char *str){
    string_t *ptr;

    ptr = malloc(sizeof(string_t));
    if (ptr){
        ptr->str = strdup(str);
        if (ptr->str){
            ptr->len = strlen(str);
        }else{
            free(ptr);
            ptr = NULL;
        }
    }
    return ptr;
}



static string_t *string_find(string_list_t *head,char *str,str_cmp_func_t func){
    string_t *ptr;
    int len;

    len = strlen(str);
    LIST_FOREACH(ptr,head,next){
        if (ptr->len == len && func(str,ptr->str,len)==0){
            return ptr;
        }
    }
    return NULL;
}

static int string_del(string_list_t *head,char *str, str_cmp_func_t func){
    string_t *ptr;

    ptr = string_find(head,str,func);
    if (ptr){
        LIST_REMOVE(ptr,next);
    }
    return 0;
}

static int string_add(string_list_t *head,char *str, str_cmp_func_t func){
    string_t *ptr;

    ptr = string_find(head,str,func);
    if (!ptr){
        ptr = string_new(str);
        if (ptr){
            LIST_INSERT_HEAD(head,ptr,next);
            return 1;
        }
    }else{
        return 1;
    }
    return 0;
}


static void strings_free(string_list_t *head){
    string_t *str;

    while(!LIST_EMPTY(head)){
        str = LIST_FIRST(head);
        LIST_REMOVE(str,next);
        free(str->str);
        free(str);
    }
}


/*
 * ===========================================================================
 * bit field
 * ===========================================================================
 */

void bf_reset(bitfiels_t *b){
    memset(b->bits,0,b->size);
}

bitfiels_t * bf_new(int size){
    bitfiels_t *res;
    res = malloc(sizeof(bitfiels_t));
    if (res){
        res->size = (size+7)/8;
        res->bits = malloc(res->size);
        if (!res->bits){
            free(res);
            res = NULL;
        }else{
            bf_reset(res);
        }
    }
    return res;
}

void bf_set(bitfiels_t *b,int i){
    int idx;
    int sh;

    idx = i/8;
    sh = i%8;
    assert(idx<b->size);


    b->bits[idx] |= (1<<sh);
}

int bf_fast_intersect(bitfiels_t* f, bitfiels_t* s){
    int i;
    assert(f->size == s->size);

    for(i=0;i<f->size;i++){
        if (f->bits[i] & s->bits[i]){
            return 1;
        }
    }
    return 0;
}


int bf_isset(bitfiels_t *b,int i){
    int idx;
    int sh;


    idx = i/8;
    sh = i%8;
    assert(idx<b->size);


    return ((b->bits[idx] & (1<<sh))!=0);
}

void bf_print(bitfiels_t* b){
    int i;

    printf(" ");
    for(i=0;i<b->size;i++){
        printf("%08x ",b->bits[i]);
    }
    printf("\n");
}

void bf_clear(bitfiels_t *b, int i){
    int idx;
    int sh;

    assert(i<b->size);

    idx = i/8;
    sh = i%8;
    assert(i<b->size);

    b->bits[idx] &= ~(1<<sh);
}



void bf_free(bitfiels_t *b) {
    free(b->bits);
    free(b);
}

/* bit field */
/* ========================================================================= */

int read_file(buf_t *line,FHANDLE f, long size) {
    int res;
    int _free;
    char *tmp;

    _free =  line->allocated - line->used;

    if (!_free){
        tmp = realloc(line->buf,line->allocated+size);
        if (!tmp){
            return -1;
        }
        line->buf = tmp;
        line->allocated += size;
    }

   size = line->allocated - (line->start+line->used);
   //if (size > (line->allocated - (line->start+line->used))){
   if(!(size)){
        if (line->used){
            memmove(line->buf,&line->buf[line->start],line->used);
        }
        line->start = 0;
        size = line->allocated - (line->start+line->used);
    }

    res = FREAD(f,&line->buf[line->start+line->used],size);
    if (res>0){
        line->used+=res;
    }
    return res;
}



/*
const char* _strnchr(const char *str, int len, int ch) {
    int i;
    const char *ptr;

    ptr = str;
    for(i=0;i<len;i++){
        if (*ptr == ch){
            return ptr;
        }
        ptr++;
    }
    return NULL;
}*/

#define _strnchr(str,len,c) memchr(str,c,len)

int get_line(buf_t *line, file_t *file) {
    int start;
    int len;
    const char *ptr;
    char *tmp;
    int res;

    ptr = NULL;

    start = 0;
    while(!ptr){
        ptr = _strnchr(&file->buf.buf[file->buf.start+start],file->buf.used-start,0x0a);
        if (!ptr){
            start = file->buf.used;
            res = read_file(&file->buf,file->f,BUFFER_SIZE);
            if (res<=0){
                break;
            }
        }
    }

    if (ptr){
        len = ptr-(file->buf.buf+file->buf.start)+1;
    }else{
        len = file->buf.used;
    }

    if (line->allocated < len){
        tmp = realloc(line->buf,len*2);
        if (!tmp){
            fprintf(stderr,"%s: "__FILE__":"STR(__LINE__)" OOM\n",opt.self_name);
            return 0;
        }
        line->allocated = len*2;
        line->used = 0;
        line->start = 0;
        line->buf = tmp;
    }
    if (len){
       memcpy(line->buf,&file->buf.buf[file->buf.start],len);
       file->buf.used -= len;
       file->buf.start += len;
       if (!file->buf.used){
          file->buf.start = 0;
       }
    }
    line->used = len;
    return len;
}

int compile(re_t *re,char *pattern,int options) {
    const char *error;
    int erroffset;

    re->re  =  pcre_compile ((char *) pattern, options, &error, &erroffset, NULL);
    if (!re->re){
        fprintf(stderr,"%s: Failed to compile regex '%s':%s\n",opt.self_name,pattern,error);
        return 0;
    }

    re->plen = strlen(pattern);
    re->pattern = pattern;
    if (is_regexp(pattern,re->plen)){
       re->findall = re_findall;
       re->pe = pcre_study(re->re,0,&error);
    }else{
       if (options & PCRE_CASELESS){
          re->findall = str_casefindall;
       }else{
          re->findall = str_findall;
       }
    }
    return 1;
}






int simple_match(re_t *re,const char *str, long len,match_t *matches, int matches_len) {
    return re->findall(re,str,len,matches,matches_len);
}


int re_findall(re_t *re,const char *str,long len,match_t *matches, int matches_len){
    int nmatches = 0;
    match_t *mptr;

    mptr = matches;
    while(len && nmatches<matches_len && (0<pcre_exec(re->re,re->pe, (char *)str,len,0,0|PCRE_NOTEMPTY,vars.offsets,OFFSETS_SIZE))){
        nmatches++;
        if (mptr){
            mptr->start = vars.offsets[0];
            mptr->len = vars.offsets[1]-vars.offsets[0];
            mptr++;
            str+= vars.offsets[1];
            len -= vars.offsets[1];
        }
    }
    return nmatches;
}

int str_findall(re_t *re,const char *str,long len,match_t *matches, int matches_len){
    const char *r;
    int nmatches = 0;

    while(len && nmatches<matches_len && (r = strnstr(str,len,re->pattern,re->plen))){
        nmatches++;
        matches->start = r-str;
        matches->len = re->plen;
        matches++;
        r=r+re->plen;
        len -= r-str;
        str = r;
    }
    return nmatches;
}

int str_casefindall(re_t *re,const char *str,long len,match_t *matches, int matches_len){
    const char *r;
    int nmatches = 0;

    while(len && nmatches<matches_len && (r = _strncasestr(str,len,re->pattern,re->plen))){
        nmatches++;
        matches->start = r-str;
        matches->len = re->plen;
        matches++;
        r=r+re->plen;
        len -= r-str;
        str = r;
    }
    return nmatches;
}

/*
inline int simple_matches(re_t *re,char *str, long len) {
    vars.nmatches = 0;
    int *ptr;

    ptr = vars.matches;
    while(len && simple_match(re,str,len,NULL) && vars.nmatches<OFFSETS_SIZE){
        vars.nmatches++;
        *ptr = vars.offsets[0];
        ptr++;
        *ptr = vars.offsets[1]-vars.offsets[0];
        ptr++;
        str+= vars.offsets[1];
        len -= vars.offsets[1];
    }
    return vars.nmatches;
}

*/

char *_basename(char* fullname) {
    char *f2;
    char *f1;

    f1 = strrchr(fullname,'/');
    f2 = strrchr(fullname,'\\');

    f1 = f1>f2?f1:f2;

    if (f1){
        f1++;
    }
    if (!f1){
        return fullname;
    }
    return f1;
}

void print_count(char *filename,long nmatches,char *le,int count,int show_filename) {
    if (show_filename){
        if (count){
            printf("%s:%ld%s",filename,nmatches,le);
        }else{
            printf("%s%s",filename,le);
        }
    }else{
        if (count){
            printf("%ld%s",nmatches,le);
        }
    }
}

char *_strnstr2(const char *s,int sl, const char *f,int fl){
    register const char *ptr;
    register const char *eptr;

    if(sl<fl){
        return NULL;
    }

    if (f && fl){
        ptr = _strnchr(s,sl,*f);
        if (ptr){
            if (!memcmp(ptr,f,fl)){
                return (char*)ptr;
            }
            eptr = s+sl-fl+1;
            ptr++;
            while(ptr<eptr){
                ptr = _strnchr(ptr,eptr-ptr,*f);
                if (ptr){
                    if (!memcmp(ptr,f,fl)){
                        return (char*)ptr;
                    }
                    ptr++;
                }else{
                    break;
                }
            }
        }
    }else{
        return (char*)s;
    }
    return NULL;
}


char *_strnstr3(const char *s,int sl, const char *f,int fl){
    const char *ptr;
    const char *eptr;

    if(sl<fl){
        return NULL;
    }

    if (f && fl){
        int xfl = fl-1;
        eptr = s+sl-xfl;

        ptr = _strnchr(s,eptr-s,*f);
        if (ptr){
           char e = f[xfl];
           while(ptr && (ptr<eptr)){
              if (*(ptr+xfl) == e){
                 if (!memcmp(ptr,f,xfl)){
                    return (char*)ptr;
                 }
              }
              ptr++;
              ptr = _strnchr(ptr,eptr-ptr,*f);
           }
        }
    }
    return NULL;
}


const char* strncasechr(const char *s,int sl,char uch, char lch)
{
   register char v;
   register int i;

   if (sl){
      i = 0;
      while( (v = s[i]) && i<sl){
         if (v == lch || v == uch){
            return &s[i];
         }
         i++;
      }
   }
   return NULL;
}

char *_strncasestr(const char *s,int sl, const char *f,int fl){
    const char *ptr;
    const char *eptr;

    if(sl<fl){
        return NULL;
    }

    if (f && fl){
        int ei = fl-1;
        int mi = ei/2;
        char ufirst = toupper(*f);
        char lfirst = tolower(ufirst);


        eptr = s+sl-ei;

        ptr = strncasechr(s,eptr-s,ufirst,lfirst);
        if (ptr){
           char le;
           char ue;
           char lm;
           char um;
           char v;

           le = tolower(f[ei]);
           ue = toupper(le);

           lm = tolower(f[mi]);
           um = toupper(lm);

           while(ptr && (ptr<eptr)){
              v = ptr[ei];
              if (v == le || v == ue){
                 v = ptr[mi];
                 if (v == lm || v == um){
                    if (!STRNCASECMP(ptr,f,ei)){
                       return (char*)ptr;
                    }
                 }
              }
              ptr++;
              ptr = strncasechr(ptr,eptr-ptr,ufirst,lfirst);
           }
        }
    }
    return NULL;
}


char *_strnstr1(const char *s, const char *f, int sl){

#if 0
    int fl;
    register const char *ptr;
    register const char *eptr;



    if (f && *f){
        ptr = _strnchr(s,sl,*f);
        if (ptr){
            fl = strlen(f);
            if (!memcmp(ptr,f,fl)){
                return (char*)ptr;
            }
            eptr = s+sl-fl+1;
            ptr++;
            while(ptr<eptr){
                ptr = _strnchr(ptr,eptr-ptr,*f);
                if (ptr){
                    if (!memcmp(ptr,f,fl)){
                        return (char*)ptr;
                    }
                    ptr++;
                }else{
                    break;
                }
            }
        }
    }else{
        return (char*)s;
    }
    return NULL;
#else
   int fl;
   fl = strlen(f);

   return strnstr(s,sl,f,fl);
#endif
}

char *analyse_header(char *str,int len) {
    char **ptr;
    char *name;


    name = str;
    if (name){
        for(ptr = interprets;*ptr;ptr++){
            if(_strnstr1(name,*ptr,len)){
                return *ptr;
            }
        }

        for(ptr = shells;*ptr;ptr++){
            if(_strnstr1(name,*ptr,len)){
                return "shell";
            }
        }

    }
    return NULL;
}


char* analyse_internals(file_t *file) {
    int size;
    char *ptr;
    char *res = "text";

    size = read_file(&file->buf,file->f,1024);
    if (size>0){
        vars.size_processed+=size;

        if ( (size>6) && (0==STRNCASECMP(file->buf.buf,"<?xml ",6))){
            res = "xml";
        }else if ( (file->buf.buf[0] == '#') && (file->buf.buf[1] == '!')){
            /* find 0x0a */
            ptr = _strnchr(file->buf.buf,size,0x0a);
            if (ptr){
                ptr = analyse_header(file->buf.buf,ptr-file->buf.buf);
                if (ptr){
                    res = ptr;
                }
                /*
                 * types:
                 * - shell,TEXT == (?:ba|t?c|k|z)?sh) == bash|t*sh|ksh|zsh|sh
                 * - $2,TEXT    == (ruby|perl|php|python)
                 * */

            }
        }else{
            ptr = _strnchr(file->buf.buf,size,0x00);
            if (!ptr){
                res = "text";
            }else{
                res = "binary";
                //printf("%s: binary on %04x\n",file->fullname,ptr-file->buf.buf); // XXX
                file->is_binary = 1;
            }
        }
    }else{
    }
    return res;
}


void out_line(buf_t *line) {
    fwrite(line->buf,1,line->used,stdout);
}

void out_context(char *name,buf_t *str,long line,long column,int is_match, match_t* matches,int nmatches) {
    char ch = is_match? ':':'-';
    char *ptr;
    char *end;
    int i;
    match_t *mptr;

    if (opt.show_filename){
        if (!opt.heading){
            printf("%s%c",name,ch);
        }
        if (opt.color){
            printf("%s%ld\e[0m\e[K%c",opt.color_lineno,line,ch);
        }else{
            printf("%ld%c",line,ch);
        }
    }
    if (opt.column){
        printf("%ld%c",column,ch);
    }
    if (opt.o){
        if (is_match && matches){
            mptr = matches;
            ptr=str->buf;
            for(i=0;i<nmatches;i++){
                ptr+=mptr->start;
                fwrite(ptr,1,mptr->len,stdout);
                ptr+=mptr->len;
                printf("\n");
                mptr++;
            }
        }
    }else{
        if (str->used){
            ptr = str->buf+str->used;
            ptr--;
            while(str->used && ((*ptr == 0x0d) || (*ptr== 0x0a))){
                ptr--;
                str->used--;
            }
            assert((str->buf-1)<=ptr);
        }
        if (nmatches == 0 || !opt.color){
            out_line(str);
        }else{
            mptr = matches;
            ptr = str->buf;
            end = ptr+str->used;
            for(i=0;i<nmatches;i++){
                fwrite(ptr,1,mptr->start,stdout);
                fprintf(stdout,"%s",opt.color_match);
                ptr+=mptr->start;
                fwrite(ptr,1,mptr->len,stdout);
                fprintf(stdout,"\e[0m\e[K");
                //fwrite(str->buf+cstart+clen,1,str->used-(cstart+clen),stdout);
                ptr+=mptr->len;
                mptr++;
            }
            if (ptr<end){
                fwrite(ptr,1,end-ptr,stdout);
            }
        }
        printf("\n");

    }
}

long analize_file(file_t *file) {
    buf_t *p;
    int res;
    buf_t *hptr;

    vars.hprint = 0;
    vars.hused = 0;

    p = &vars.history[vars.hused];
    p->used = 0;
    while(get_line(p,file)){
        file->line++;
        if (opt.passthru){
            out_line(p);
            p->used = 0;
            continue;
        }
        if ((opt.v != 0 ) != (0 != (vars.nmatches=simple_match(&opt.match,p->buf,p->used,vars.matches,OFFSETS_SIZE)))){
            if (opt.show_context){
                if(file->is_binary){
                    if (vars.files_matched && !file->nmatches){
                        printf("\n");
                    }
                    printf("Binary file %s matches\n",file->fullname);
                    return 1;
                }else{
                    if (opt.show_filename && opt._break){
                        if (vars.files_matched && !file->nmatches){
                            printf("\n");
                        }
                    }
                    if (opt.heading && opt.show_filename){
                        if (!file->nmatches){
                            if (opt.color){
                                printf("%s",opt.color_filename);
                            }
                            printf("%s\n",file->fullname);
                            if(opt.color){
                                printf("\e[0m\e[K");

                            }
                        }
                    }else{
                    }
                    if((opt.A || opt.B) && (file->nmatches || !opt.heading)){
                        printf("--\n");
                    }

                    vars.hprint = opt.A;
                    hptr = vars.history;
                    while(vars.hused){
                        out_context(file->fullname,hptr,file->line-vars.hused,0,0,0,0);
                        hptr->used = 0;
                        hptr++;
                        vars.hused--;
                    }
                    out_context(file->fullname,p,file->line,vars.matches->start+1,1,vars.matches,vars.nmatches);
                    p = &vars.history[vars.hused];
                }
            }
            file->nmatches++;
            if ((opt.m && (opt.m==file->nmatches))){
                break;
            }

        }else{
            if (vars.hprint){
                out_context(file->fullname,p,file->line,0,0,0,0);
                vars.hprint--;
            }else{
                if (opt.B){
                    if (vars.hused >= opt.B){
                        buf_t ttt;
                        ttt = vars.history[0];
                        memmove(&vars.history[0],&vars.history[1],sizeof(buf_t)*(vars.hused));
                        vars.history[vars.hused] = ttt;
                    }else{
                        vars.hused++;
                    }
                    p = &vars.history[vars.hused];
                }
            }
        }
        p->used = 0;

    }

    vars.hused=0;
    p->used = 0;

    res = file->nmatches? 1:0;
    return res;
}


void get_filetypes(file_t *file) {
    ext_t *ext;
    filetype_t *ft;
    int len;
    int res;
    char *type;

    if (file->type_processed)
        return;

    file->type_processed = 1;
    res = 0;

    if (!is_searchable(file)){
        // "skiped"
        bf_set(file->filetypes,vars.ft_skipped->i);
        return;
    }


    if ( (type = analyse_internals(file)) ){
        ft = find_filetype(type);
        if (ft){
            res++;
            bf_set(file->filetypes,ft->i);
        }
    }


    if (0 == FILENAMECMP("makefile",file->name) ||
            0 == FILENAMECMP("gnumakefile",file->name)){
        // "make" + "text"
        bf_set(file->filetypes,vars.ft_make->i);
        res++;
    }else if (0==FILENAMECMP("rakefile",file->name)){
        // "rake", "ruby", "text"
        ft = find_filetype("rake");
        bf_set(file->filetypes,ft->i);
        bf_set(file->filetypes,vars.ft_ruby->i);
        res++;
    }

    len = file->namelen;

    LIST_FOREACH(ext,&opt.exts,next){
        if (_ends_with(file->name,len,ext->ext,ext->len)){
            bf_set(file->filetypes,ext->type->i);
            res++;
        }
    }

    if (res && !file->is_binary ){
        bf_set(file->filetypes,vars.ft_text->i);
    }

    return;
}


int ignore_dir(const char *dirname) {
    string_t *str;
    int dirlen;

    dirlen = strlen(dirname);

    LIST_FOREACH(str,&opt.ignore_dirs,next){
        if ((dirlen == str->len) && FILENAMENCMP(dirname,str->str,str->len)==0) {
            return 1;
        }
    }
    return 0;
}

int scandir_ignore_dir(const struct dirent *d) {
    return !ignore_dir(d->d_name);
}



int is_interesting(file_t *file) {
    get_filetypes(file);
    return (bf_fast_intersect(file->filetypes,opt.req_filetypes));
}


int _starts_with(const char *name,int nsize, const char *start,int ssize) {
    if ((nsize < ssize) || (*name!=*start))
        return 0;
    return 0 == FILENAMENCMP(name,start,ssize);
}

int _ends_with(const char *name,int nsize,const char *ext,int esize) {
    if (nsize < esize)
        return 0;
    {
        return 0 == FILENAMENCMP(name+nsize-esize,ext,esize);
    }
}

int is_searchable(file_t *file) {
    int len = file->namelen;
    return !(
            /* \.bak */_ends_with(file->name,len,".bak",4) ||
            /* ~$ */_ends_with(file->name,len,"~",1) ||
            /* [._].*\.swp */ ((_starts_with(file->name,len,".",1) || _starts_with(file->name,len,"_",1)) && _ends_with(file->name,len,".swp",4)) ||
            /* \.bak$ */ _ends_with(file->name,len,".tmp",4) ||
            /* #.+#$ */ (_starts_with(file->name,len,"#",1) && _ends_with(file->name,len,"#",1)) ||
            0/*TODO: (_starts_with(file->name,len,"core.",5))*/
            );
}


long process_sdtdin(FHANDLE f) {
    file_t file;

    memset(&file,0,sizeof(file_t));
    file.fullname = "";
    file.name = "";
    file.filetypes = vars.filetypes;
    bf_reset(file.filetypes);
    file.f = f;
    vars.file_processed++;
    vars.files_matched += analize_file(&file);
    vars.total_matches += file.nmatches;
    free(file.buf.buf);
    return file.nmatches;
}


long process_file(char *fullname,char *name) {

    vars.file.buf.start = 0;
    vars.file.buf.used = 0;
    vars.file.fullname = fullname;
    vars.file.name = name;
    vars.file.namelen = strlen(name);
    vars.file.filetypes = vars.filetypes;
    vars.file.nmatches = 0;
    vars.file.line = 0;
    vars.file.is_binary = 0;
    vars.file.type_processed = 0;

    bf_reset(vars.file.filetypes);
    vars.file.f = FOPEN(vars.file.fullname);
    vars.file_processed++;
    if (FISGOOD(vars.file.f)){

        if ( /*opt.u ||*/
                (opt.a && is_searchable(&vars.file))||
                ((!opt.a) && is_interesting(&vars.file))
           ){

            if (opt.f){
                vars.file.nmatches++;
                printf("%s",vars.file.fullname);
                if (opt.show_types){
                    filetype_t *ft;
                    int i;

                    printf(" => ");
                    i = 0;
                    get_filetypes(&vars.file);
                    LIST_FOREACH(ft,&opt.all_filetypes,next){
                        if (bf_isset(vars.file.filetypes,ft->i)){
                            if (i){
                                printf(",");
                            }
                            printf("%s",ft->name);
                            i++;
                        }
                    }
                }
                printf("%s",opt.line_end);
            }else{
                get_filetypes(&vars.file);
                analize_file(&vars.file);
                if (!opt.show_total && (opt.l || opt.c)){
                    if (vars.file.nmatches){
                        print_count(vars.file.fullname,vars.file.nmatches,opt.line_end,opt.c,opt.show_filename);
                    }else if (opt.print_count0){
                        print_count(vars.file.fullname,vars.file.nmatches,opt.line_end,1,opt.show_filename);
                    }
                }
           }
        }
        FCLOSE(vars.file.f);
    }else{
        fprintf(stderr,"%s: %s: Failed to open %d:%s\n",opt.self_name,vars.file.fullname,errno,strerror(errno));
    }
    vars.files_matched += vars.file.nmatches;
    vars.total_matches += vars.file.nmatches;
    return vars.file.nmatches;

}


void process(char *filename) {
    struct dirent** dents;
    char fullname[PATH_MAX];
    int count;
    struct dirent* dent;
    int i;
    struct stat statbuf;

    if (lstat(filename, &statbuf) < 0){
        fprintf(stderr,"%s: Can't stat '%s'\n",opt.self_name,filename);
        return;
    }
    if (S_ISDIR(statbuf.st_mode)){
        count = scandir(filename,&dents, (opt.u) ? NULL : scandir_ignore_dir,opt.sort_files?alphasort:NULL);
        if (count>=0){
            i = strlen(filename);
            if (i){
                i--;
            }
            if(filename[i] == DIRSEPC){
                filename[i] = 0;
            }
            for(i=0;(i<count) && !(opt.one && vars.total_matches) ;i++){
                dent = dents[i];

                if (strcmp(dent->d_name, ".") != 0 && strcmp(dent->d_name, "..") != 0){
                    if(strcmp(filename,".")){
                        snprintf(fullname,sizeof(fullname)-1,"%s" DIRSEPS "%s",filename,dent->d_name);
                    }else{
                        strcpy(fullname,dent->d_name);
                    }
                    fullname[sizeof(fullname)-1] = 0;
                    if (lstat(fullname, &statbuf) < 0){
                        fprintf(stderr,"%s: Can't stat '%s'\n",opt.self_name,filename);
                        return;
                    }
#ifndef WINDOWS
                    if (S_ISLNK(statbuf.st_mode) && !opt.follow){
                        continue;
                    }
#endif
                    if (S_ISDIR(statbuf.st_mode)){
                        if(/*(opt.u || !ignore_dir(fullname)) &&*/ opt.recursive){
                            process(fullname);
                        }
                    }else{
                        if(opt.G.re){
                            if (opt.invert_file_match == simple_match(&opt.G,dent->d_name,strlen(dent->d_name),NULL,1)){
                                continue;
                            }
                        }
                        process_file(fullname,dent->d_name);
                    }
                }
            }
            while(count){
                count--;
                free(dents[count]);
            }
            free(dents);
        }else{
            fprintf(stderr, "%s: Failed to open directory %s: %s\n", opt.self_name,filename,
                    strerror(errno));
        }
    }else{
        process_file(filename,_basename(filename));
    }
}

filetype_t *find_filetype(char *filetype) {
    filetype_t *ft;
    int len = strlen(filetype);

    LIST_FOREACH(ft,&opt.all_filetypes,next){
        if ( (ft->namelen == len) && (0==strcmp(ft->name,filetype)) ){
            break;
        }
    }
    return ft;
}


int add_exts(char* filetype, char *exts,int del_old) {
    char *str;
    char *ptr;
    char *s;
    ext_t *ext;
    ext_t *prev;
    filetype_t *ft;
    int last;


    ft = find_filetype(filetype);
    if (!ft){
        ft = malloc(sizeof(filetype_t));
        if (!ft){
            fprintf(stderr,"%s: "__FILE__":"STR(__LINE__)" OOM\n",opt.self_name);
            exit(NOMATCH);
        }
        ft->name = strdup(filetype);
        ft->namelen = strlen(filetype);
        ft->i = opt.nfiletypes;
        ft->wanted = 0;
        opt.nfiletypes++;
        LIST_INSERT_HEAD(&opt.all_filetypes,ft,next);
    }

    if(del_old){
        prev = NULL;
        LIST_FOREACH(ext,&opt.exts,next){
            if (ft == ext->type){
                LIST_REMOVE(ext,next);
                opt.nexts--;
                free(ext->ext);
                free(ext);
                if (prev){
                    ext = prev;
                }else{
                    ext = LIST_FIRST(&opt.exts);
                }
            }else{
                prev = ext;
            }
        }
    }
    str = strdup(exts);
    if (str){
        ptr = str;
        s = ptr;
        last = 0;
        while(1){
            if (*ptr == ',' || *ptr == 0){
                if (*ptr == 0)
                    last = 1;
                *ptr = 0;
                ext = malloc(sizeof(ext_t));
                if (!ext){
                    fprintf(stderr,"%s: "__FILE__":"STR(__LINE__)" OOM\n",opt.self_name);
                    exit(NOMATCH);
                }
                ext->ext = strdup(s);
                ext->type = ft;
                ext->len = ptr-s;
                opt.nexts++;
                LIST_INSERT_HEAD(&opt.exts,ext,next);
                s = ptr+1;
                if (last){
                    break;
                }
            }
            ptr++;
        }
    }
    free(str);
    return 1;
}

void free_filetypes() {
    ext_t *ext;
    filetype_t *ft;


    while(!LIST_EMPTY(&opt.all_filetypes)){
        ft = LIST_FIRST(&opt.all_filetypes);
        LIST_REMOVE(ft,next);
        free(ft->name);
        free(ft);
        opt.nfiletypes--;
    }
    while(!LIST_EMPTY(&opt.exts)){
        ext = LIST_FIRST(&opt.exts);
        LIST_REMOVE(ext,next);
        free(ext->ext);
        free(ext);
        opt.nexts--;
    }
}





/*
 * ==============================================================
 * Parser
 * ==============================================================
 */


struct opt_ctx;

typedef int (*opt_error_func_t)(int cause, struct opt_ctx *opt);


typedef struct opt_ctx{
    struct option *op;
    char *data;
    bool _long;
    int len;
    char *pname;
    int errors;
    bool wait4data;
    char *error_data;
    int erros_cause;
    opt_error_func_t error_func;
}opt_ctx_t;


/*
 * input: parser context
 * return: count of processed symbols (bytes)
 *
 */

typedef int (*parser_func_t)(opt_ctx_t *opt);


typedef struct option{
    char *_short;
    char *_long;
    long flags;
    parser_func_t parser;
    void  *ptr;
    void  *_default;
}option_t;



typedef option_t * (*opt_find_t)(char* long_opt, char *short_opt, int opt_len, void* data);


#define _OPT_DATA      0x01
#define _OPT_OPTIONAL  0x02


enum{
    OPT_NODATA = 0,
    OPT_DATA = _OPT_DATA, /* with param */
    OPT_OPT_DATA = _OPT_DATA|_OPT_OPTIONAL, /* with optional param */
};




int count_digits(char *str) {
    char *ptr = str;
    int i = 0;
    bool hex;

    hex = false;
    while((*ptr != 0 )){
        if ((i == 1)&& *ptr =='x'){
            i++;
            hex = true;
        }else{
            if(
                    (i==0 && *ptr == '-')|| isdigit(*ptr) ||
                    (hex && i>=2 && ((*ptr >='A' && *ptr <='F') || (*ptr>='a' && *ptr<='f')) )
              ){
                i++;
            }else{
                break;
            }
        }
        ptr++;
    }
    return i;
}


/*
 *
 *  >>-+-------------+--+-----+--+----+--+--------+----------------><
 *     '-white space-'  +- + -+  +-0--+  '-digits-'
 *                      '- - -'  +-0x-+
 *                               '-0X-'
 *
 */

int parse_ulong(char *str,unsigned long *res) {
    unsigned long v;
    char *endp;
    int len;

    errno = 0;
    v = strtoul(str,&endp,0);
    if (errno == ERANGE || errno == EINVAL){
        return -1;
    }
    len = endp - str;
    if (len)
        (*res) = v;
    return len;
}

/*
int parse_ulong2 (char* str, unsigned long *res)
{
    int r;

    r = sscanf(str,"0x%x",res);
    if ( (r == 0)  || (r == EOF) ){
        r = sscanf(str,"%lu",res);
    }

    if (r!= EOF && r!=0){
        return true;
    }
    return false;
}
*/

int parse_long (char* str, long *res) {
    unsigned long v;
    char *endp;
    int len;

    errno = 0;
    v = strtoul(str,&endp,0);
    if (errno == ERANGE || errno == EINVAL){
        return 0;
    }
    len = endp - str;
    if (len)
        (*res) = v;
    return len;
}


int parse_long_long (char* str, long long *res) {
    int r;

    r = sscanf(str,"0x%"F_LONGLONG"x",res);
    if ( (r == 0)  || (r == EOF) ){
        r = sscanf(str,"%"F_LONGLONG"d",res);
    }
    if (r != EOF){
        return true;
    }
    return false;
}


int opt_long(opt_ctx_t *ctx) {
    long res;
    int i;

    i = count_digits(ctx->data);
    if (parse_long(ctx->data,&res)){
        (*(long*)ctx->op->ptr) = res;
        return i;
    }else{
        if (ctx->op->flags & _OPT_OPTIONAL){
            (*(long*)ctx->op->ptr) = ((long)ctx->op->_default);
        }
    }
    return -1;
}


int opt_uint(opt_ctx_t *ctx) {
    unsigned long res;
    int i;
    char *endp;

    errno = 0;
    res = strtoul(ctx->data,&endp,0);
    if (errno != 0 || endp == ctx->data){
        if (ctx->op->flags & _OPT_OPTIONAL){
            (*(unsigned int*)ctx->op->ptr) = ((unsigned int)(ctx->op->_default));
            return 0;
        }
        return -1;
    }

    (*(unsigned int*)ctx->op->ptr) = res;
    return endp - ctx->data;

    if (parse_ulong(ctx->data,&res)){
        (*(unsigned int*)(ctx->op->ptr)) = res;
        return i;
    }else{
        if (ctx->op->flags & _OPT_OPTIONAL){
            (*(unsigned int*)ctx->op->ptr) = ((unsigned int)(ctx->op->_default));
            return 0;
        }
    }
    return -1;
}


int opt_int(opt_ctx_t *ctx) {
    long res;
    int i;

    i = count_digits(ctx->data);
    res = 0;
    if (parse_long(ctx->data,&res)){
        (*(int*)ctx->op->ptr) = res;
        return i;
    }else{
        if (ctx->op->flags & _OPT_OPTIONAL){
            (*(int*)ctx->op->ptr) = ((int)(ctx->op->_default));
        }
    }
    return -1;
}

int opt_inc_int(opt_ctx_t *ctx) {
    (*(int*)ctx->op->ptr)++;
    return 0;
}

int opt_set_true(opt_ctx_t *ctx) {
    *((int*)ctx->op->ptr) = true;
    return 0;
}

int opt_set_false(opt_ctx_t *ctx) {
    *((int*)ctx->op->ptr) = false;
    return 0;
}

int opt_invert(opt_ctx_t* ctx) {
    *((int*)ctx->op->ptr) = !(*((int*)ctx->op->ptr));
    return 0;
}


int opt_string(opt_ctx_t *ctx) {
    *((char**)(ctx->op->ptr)) = ctx->data;
    return strlen(ctx->data);
}

int opt_dupstring(opt_ctx_t *ctx) {
    *((char**)(ctx->op->ptr)) = strdup(ctx->data);
    return strlen(ctx->data);
}


option_t * opt_find(char *long_opt, char* short_opt,int len,void *options) {
    option_t *ptr;

    if (!long_opt && ! short_opt)
        return NULL;

    ptr = (option_t*)options;

    while(ptr->_short || ptr->_long){
        if (long_opt && ptr->_long){
            if ((0 == strncmp(long_opt,ptr->_long,len)) && (strlen(ptr->_long) == len))
                return ptr;
        }else if (short_opt && ptr->_short){
            if ((0 == strncmp(short_opt,ptr->_short,len)) && (strlen(ptr->_short) == len))
                return ptr;
        }
        ptr++;
    }
    return NULL;
}


bool _opt_try_to_call(opt_ctx_t *ctx) {
    char *opt;
    char *opt_prefix;


    ctx->len = 0;
    opt =  ctx->_long?ctx->op->_long:ctx->op->_short;
    opt_prefix = ctx->_long?"--":"-";
    if ((ctx->op->flags&_OPT_DATA)){
        if (!ctx->data || !*ctx->data){
            if  (!ctx->wait4data){
                ctx->wait4data = true;
            }else{
                ctx->wait4data = false;
                if (ctx->op->flags&_OPT_OPTIONAL){
                    goto make_call;
                }else{
                    fprintf(stderr,"%s: option '%s%s' requires an argument\n",ctx->pname,opt_prefix,opt);
                    ctx->errors++;
                }
            }
            return false;
        }
    }else{
        if (ctx->_long && ctx->data){
            fprintf(stderr,"%s: option '%s%s' doesn't allow an argument\n",ctx->pname,opt_prefix,opt);
            ctx->wait4data = false;
            ctx->errors++;
            return false;
        }
    }

make_call:

    if (!ctx->data){
        ctx->data ="";
    }
    ctx->len = ctx->op->parser(ctx);
    if ((ctx->len<0)||( ctx->wait4data && ctx->data && (ctx->len != strlen(ctx->data)))){
        if (ctx->op->flags&_OPT_OPTIONAL){
        }else{
            ctx->errors++;
            fprintf(stderr,"%s: invalid argument after %s%s\n",ctx->pname,opt_prefix,opt);
        }
    }
    ctx->wait4data = false;
    return true;
}




void _opt_shift(int argc, char *argv[],int from) {
    char *tmp;

    tmp = argv[from];
    memmove(&argv[from],&argv[from+1],sizeof(char *)*(argc-from-1));
    if (argc>1){
        argv[argc-1] = tmp;
    }
}


/* -l -a -c
 * -lac
 * -s 24 -s24
 * --size=24
 * --size 24
 * -- end of the options
 *
 * actions:
 *  - set to true
 *  - set to false
 *  - invert
 *  - increment
 *  - int value
 *  - string value
 *
 * results:
 *  - parsed ok, returned a len of used data -> do nothing
 *  - an error while parsing data -> show an error
 *  - partially parsed -> show an error
 */

int opt_parse(int argc, char *argv[], opt_find_t _opt_find,void* opt_find_data,opt_error_func_t error_func) {
    opt_ctx_t ctx;
    char *ptr;
    int i;
    int oplen;
    int old_argc;

    ctx.wait4data = false;
    ctx._long = false;
    ctx.pname = argv[0];
    ctx.errors = 0;

    old_argc = argc;
    for(i=1;i<argc;i++){
        ptr = argv[i];
        if (ctx.wait4data){
            ctx.data = ptr;
            _opt_try_to_call(&ctx);
            ctx.wait4data = false;
            if (ctx.len>0){
                if (ctx.len == strlen(ptr)){
                    continue;
                }
                ptr += ctx.len;
            }
        }
        if (ptr[0] != '-' || (ptr[0] == '-' && ptr[1] == 0)){
            /* not an option, skip */
            _opt_shift(old_argc,argv,i);
            argc--;
            i--;
            continue;
        }

        ptr++;
        if (*ptr == '-'){
            if (*ptr == 0){
                /* end of the options */
                break;
            }
            /* long option */
            ctx.data = strchr(ptr,'=');
            if (ctx.data){
                oplen = ctx.data-ptr;
                ctx.data++;
            }else{
                oplen = strlen(ptr);
            }
            ptr++;
            oplen--;
            ctx.op = _opt_find(ptr,NULL,oplen,opt_find_data);
            if (ctx.op){
                ctx._long = true;
                ctx.wait4data = ctx.data != NULL;
                _opt_try_to_call(&ctx);
            }else{
                fprintf(stderr,"%s: unrecognized option '%s'\n",ctx.pname,ptr);
                ctx.errors++;
            }
        }else{
            while(*ptr){
                if (*ptr=='-'){
                    ptr++;
                    continue;
                }
                ctx.op = _opt_find(NULL,ptr,1,opt_find_data);
                if (ctx.op){
                    ctx.data=ptr+1;
                    ctx._long = false;
                    if (_opt_try_to_call(&ctx)){
                        if (ctx.len>=0){
                            ptr+=ctx.len;
                        }
                    }
                }else{
                    fprintf(stderr,"%s: unrecognized option '%c'\n",ctx.pname,*ptr);
                    ctx.errors++;
                }
                ptr++;
            }
        }
    }
    if (ctx.wait4data){
        _opt_try_to_call(&ctx);
    }
    if (ctx.errors){
        i = -i;
    }
    return i;
}



/*
 * Parser end
 * ==============================================================
 */




int type_modify(opt_ctx_t *ctx,int del) {
    char *eptr;
    char *sptr;
    char type[8*1024];

    eptr = strchr(ctx->data,'=');
    if(eptr){
        sptr = ctx->data;
        while(isspace(*sptr)) sptr++;
        if ((eptr-sptr)>(sizeof(type)-1)){
            return -1;
        }
        strncpy(type,sptr,eptr-sptr);
        type[eptr-sptr] = 0;
        eptr++;
        add_exts(type,eptr,del);
        return strlen(ctx->data);
    }
    return -1;

}

int type_add(opt_ctx_t *ctx) {
    return type_modify(ctx,false);
}

int type_set(opt_ctx_t *ctx) {
    return type_modify(ctx,true);
}

int ignore_dir_add(opt_ctx_t *ctx) {

    string_add(&opt.ignore_dirs,ctx->data,FILENAMENCMP);
    return strlen(ctx->data);
}

int ignore_dir_del(opt_ctx_t *ctx) {

    string_del(&opt.ignore_dirs,ctx->data,FILENAMENCMP);
    return strlen(ctx->data);
}


int _type_wanted(char *str) {
    filetype_t *ft;
    char *tmp;

    tmp = NULL;

    ft = find_filetype(str);


    if (!ft){
        if (0==strncmp(str,"no",2)){
            tmp = str+2;
            ft = find_filetype(tmp);
        }
    }

    if (ft){
        if (tmp){

            ft->wanted = -1;
            if (0 == opt.types_type){
                opt.types_type = -1;
            }
        }else{
            ft->wanted = 1;
            opt.types_type = 1;
        }
        return strlen(str);
    }
    return -1;
}


int type_wanted(opt_ctx_t *ctx) {
    return _type_wanted(ctx->data);
}

int type_wanted2(opt_ctx_t *ctx) {
    if (_type_wanted(ctx->op->_long)>=0){
        return 0;
    }
    return -1;
}

int help_types(opt_ctx_t *ctx) {
    if (strcmp(ctx->data,"types")==0){
        opt.help_types = true;
        return strlen(ctx->data);
    }
    opt.help = true;
    return 0;
}

int opt_ignore(opt_ctx_t *ctx) {
    return 0;
}

int opt_not_implemented(opt_ctx_t *ctx) {
    char *opt;

    opt =  ctx->_long?ctx->op->_long:ctx->op->_short;
    fprintf(stderr,"%s: Option '%s' is not implemented. Ignored.\n",ctx->pname,opt);
    return 0;
}



option_t _opt;

option_t * type_opt_find(char *long_opt, char* short_opt,int len,void *options) {
    char *tmp;
    filetype_t *ft;
    option_t *opt;

    if (!long_opt && ! short_opt)
        return NULL;

    opt = opt_find(long_opt,short_opt,len,options);


    if (opt)
        return opt;

    if (!long_opt)
        return NULL;

    tmp = NULL;
    ft = find_filetype(long_opt);

    if (!ft){
        if (0==strncmp(long_opt,"no",2)){
            tmp = long_opt+2;

            ft = find_filetype(tmp);
        }
    }

    if (ft){
        _opt._short = NULL;
        _opt._long = long_opt;
        _opt.flags = OPT_NODATA;
        _opt.parser = type_wanted2;
        _opt._default = NULL;
        return &_opt;
    }
    return NULL;

}


int parse_colors(opt_ctx_t *ctx) {
    char *eptr;
    char *sptr;
    char res[8096];
    int len;
    int slen;
    int count;
    string_pairs_t *pair;
    int tmp;
    int f;


    eptr = ctx->data;
    f = sizeof(res);
    slen = snprintf(res,f,"\e[");
    count = 0;
    while(*eptr){
        while(isspace(*eptr)){
            eptr++;
        }
        sptr = eptr;
        while(*eptr && *eptr!=' '){
            eptr++;
        }
        len = eptr - sptr;
        for(pair=colors;pair->first;pair++){
            if (0 == strncmp(pair->first,sptr,len) && strlen(pair->first) == len){
                if (count){
                    tmp = snprintf(&res[slen],f,";");
                    if (tmp>0){
                        slen += tmp;
                        f-=tmp;
                    }else{
                        return -1;
                    }
                }
                count++;
                tmp = snprintf(&res[slen],f,"%s",pair->second);
                if (tmp>0){
                    slen+=tmp;
                    f -= tmp;
                }else{
                    return -1;
                }
                break;
            }
        }
        if (!pair->first){
            return -1;
        }
    }
    tmp = snprintf(&res[slen],f,"m");
    if (tmp>0){
        *((char**)ctx->op->ptr) = strdup(res);
        return strlen(ctx->data);
    }else{
        return -1;
    }
}

option_t args[] = {
    {"v","invert-match", OPT_NODATA, opt_set_true, &opt.v,0},
    {"w","word-regexp",OPT_NODATA, opt_set_true,&opt.w,0},
    {"Q","literal",OPT_NODATA, opt_set_true,&opt.Q,0},
    {"i","ignore-case",OPT_NODATA, opt_set_true,&opt.i,0},
    {NULL,"smart-case",OPT_NODATA, opt_set_true,&opt.smart_case,0},
    {NULL,"nosmart-case",OPT_NODATA, opt_set_false,&opt.smart_case,0},
    {NULL,"line",OPT_DATA, opt_not_implemented,&opt.line,0},
    {"l","files-with-matches",OPT_NODATA, opt_set_true,&opt.l,0},
    {"L","files-without-matches",OPT_NODATA, opt_set_true,&opt.L,0},
    {"o",NULL,OPT_NODATA, opt_set_true,&opt.o,0},
    {NULL,"passthru",OPT_NODATA, opt_set_true,&opt.passthru,0},
    {NULL,"output",OPT_DATA, opt_string,&opt.output,0},
    {NULL,"match",OPT_DATA, opt_string,&opt.match_pattern,0},
    {"m","max-count",OPT_DATA, opt_long,&opt.m,0},
    {"1",NULL,OPT_NODATA, opt_set_true,&opt.one,0},
    {"H","with-filename",OPT_NODATA, opt_set_true,&opt.H,0},
    {"h","without-filename",OPT_NODATA, opt_set_true,&opt.h,0},
    {"c","count",OPT_NODATA, opt_set_true,&opt.c,0},
    {NULL,"column",OPT_NODATA, opt_set_true,&opt.column,0},
    {"A","after-context",OPT_DATA, opt_uint,&opt.A,0},
    {"B","before-context",OPT_DATA, opt_uint,&opt.B,0},
    {"C","context",OPT_OPT_DATA, opt_uint,&opt.C,(void*)2},
    {NULL,"print0",OPT_NODATA, opt_set_true,&opt.print0,0},
    {NULL,"pager",OPT_NODATA, opt_not_implemented,&opt.pager,0},
    {NULL,"nopager",OPT_NODATA, opt_set_true,&opt.nopager,0},
    {NULL,"break",OPT_NODATA, opt_set_true,&opt._break,0},
    {NULL,"nobreak",OPT_NODATA, opt_set_false,&opt._break,0},
    {NULL,"noheading",OPT_NODATA, opt_set_false,&opt.heading,0},
    {NULL,"heading",OPT_NODATA, opt_set_true,&opt.heading,0},
    {NULL,"flush",OPT_NODATA, opt_set_true,&opt.flush,0},
    {"f",NULL,OPT_NODATA, opt_set_true,&opt.f,0},
    {"g",NULL,OPT_DATA, opt_string,&opt.g_pattern,0},
    {"G",NULL,OPT_DATA, opt_string,&opt.G_pattern,0},
    {NULL,"sort-files",OPT_NODATA, opt_set_true,&opt.sort_files,0},
    {NULL,"invert-file-match",OPT_NODATA, opt_set_true,&opt.invert_file_match,0},
    {NULL,"show-types",OPT_NODATA, opt_set_true,&opt.show_types,0},
    {"a","all-types",OPT_NODATA, opt_set_true,&opt.a,0},
    {"u","unrestricted",OPT_NODATA, opt_set_true,&opt.u,0},
    {"r","recurse",OPT_NODATA, opt_set_true,&opt.r,0},
    {"n","no-recurse",OPT_NODATA, opt_set_false,&opt.r,0},
    {NULL,"group",OPT_NODATA, opt_set_true,&opt.group,0},
    {NULL,"nogroup",OPT_NODATA, opt_set_true,&opt.nogroup,0},
    {"R",NULL,OPT_NODATA, opt_set_true,&opt.r,0},
    {NULL,"follow",OPT_NODATA, opt_set_true,&opt.follow,0},
    {NULL,"nofollow",OPT_NODATA, opt_set_false,&opt.follow,0},
    {NULL,"env",OPT_NODATA, opt_set_true,&opt.env,0},
    {NULL,"noenv",OPT_NODATA, opt_set_false,&opt.env,0},
    {NULL,"type",OPT_DATA,type_wanted,NULL,0},
    {NULL,"type-set",OPT_DATA,type_set,NULL,0},
    {NULL,"type-add",OPT_DATA,type_add,NULL,0},
    {NULL,"ignore-dirs",OPT_DATA,ignore_dir_add,NULL,0},
    {NULL,"noignore-dirs",OPT_DATA,ignore_dir_del,NULL,0},

    {NULL,"help",OPT_OPT_DATA,help_types,&opt.help,0},
    {NULL,"help-types",OPT_NODATA,opt_set_true,&opt.help_types,0},
    {NULL,"man",OPT_NODATA,opt_set_true,&opt.help,0},
    {NULL,"version",OPT_NODATA,opt_set_true,&opt.version,0},
    {NULL,"color",OPT_NODATA,opt_set_true,&opt.color,0},
    {NULL,"nocolor",OPT_NODATA,opt_set_false,&opt.color,0},
    {NULL,"colour",OPT_NODATA,opt_set_true,&opt.color,0},
    {NULL,"nocolour",OPT_NODATA,opt_set_false,&opt.color,0},
    {NULL,"color-filename",OPT_DATA,parse_colors,&opt.color_filename,0},
    {NULL,"color-match",OPT_DATA,parse_colors,&opt.color_match,0},
    {NULL,"color-lineno",OPT_DATA,parse_colors,&opt.color_lineno,0},

    {NULL,"thpppt",OPT_NODATA,opt_set_true,&opt.thpppt,0},

    {NULL,NULL,OPT_NODATA,NULL,NULL,0}

    /*{"","",OPT_NODATA, opt_inc_int,&opt.},*/

};



void print_thpppt()
{
    printf(
            "_   /|\n"
            "\\'o.O'\n"
            "=(___)=\n"
            "   U    ack --thpppt!\n"
          );
}

void print_version()
{
    printf(
            "ack in C " VERSION "\n"
            "\n"
            "Copyright (c) 2011-2019, Roman Kraevskiy <rkraevskiy@gmail.com>\n"
            "All rights reserved.\n"
            "\n"
            "Redistribution and use in source and binary forms, with or without\n"
            "modification, are permitted provided that the following conditions are met:\n"
            "\n"
            "1. Redistributions of source code must retain the above copyright notice, this\n"
            "   list of conditions and the following disclaimer.\n"
            "2. Redistributions in binary form must reproduce the above copyright notice,\n"
            "   this list of conditions and the following disclaimer in the documentation\n"
            "   and/or other materials provided with the distribution.\n"
            "\n"
            "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\" AND\n"
            "ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED\n"
            "WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE\n"
            "DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR\n"
            "ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES\n"
            "(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;\n"
            "LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND\n"
            "ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n"
            "(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS\n"
            "SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n"
          );

}


void print_types()
{
    filetype_t *ft;
    ext_t *ext;


    printf( "Usage: ack [OPTION]... PATTERN [FILES]\n"
            "\n"
            "The following is the list of filetypes supported by ack.  You can\n"
            "specify a file type with the --type=TYPE format, or the --TYPE\n"
            "format.  For example, both --type=perl and --perl work.\n"
            "\n"
            "Note that some extensions may appear in multiple types.  For example,\n"
            ".pod files are both Perl and Parrot.\n"
            "\n");


    LIST_FOREACH(ft,&opt.all_filetypes,next){
        printf("    --[no]%s ",ft->name);
        LIST_FOREACH(ext,&opt.exts,next){
            if (ext->type == ft){
                printf("%s ",ext->ext);
            }
        }
        printf("\n");
    }

}

void print_usage()
{

    long i;
    string_pairs_t *ptr;


    printf("Usage: ack [OPTION]... PATTERN [FILE]\n"
            "\n"
            "Search for PATTERN in each source file in the tree from cwd on down.\n"
            "If [FILES] is specified, then only those files/directories are checked.\n"
            "ack may also search STDIN, but only if no FILE are specified, or if\n"
            "one of FILES is \"-\".\n"
            "\n"
            "Default switches may be specified in ACK_OPTIONS environment variable or\n"
            "an .ackrc file. If you want no dependency on the environment, turn it\n"
            "off with --noenv.\n"
            "\n"
            "Example: ack -i select\n"
            "\n"
            "Searching:\n"
            "  -i, --ignore-case     Ignore case distinctions in PATTERN\n"
            "  --[no]smart-case      Ignore case distinctions in PATTERN,\n"
            "                        only if PATTERN contains no upper case\n"
            "                        Ignored if -i is specified\n"
            "  -v, --invert-match    Invert match: select non-matching lines\n"
            "  -w, --word-regexp     Force PATTERN to match only whole words\n"
            "  -Q, --literal         Quote all metacharacters; PATTERN is literal\n"
            "\n"
            "Search output:\n"
            "  --line=NUM            Only print line(s) NUM of each file\n"
            "  -l, --files-with-matches\n"
            "                        Only print filenames containing matches\n"
            "  -L, --files-without-matches\n"
            "                        Only print filenames with no matches\n"
            "  -o                    Show only the part of a line matching PATTERN\n"
            "                        (turns off text highlighting)\n"
            "  --passthru            Print all lines, whether matching or not\n"
            "  --output=expr         Output the evaluation of expr for each line\n"
            "                        (turns off text highlighting)\n"
            "  --match PATTERN       Specify PATTERN explicitly.\n"
            "  -m, --max-count=NUM   Stop searching in each file after NUM matches\n"
            "  -1                    Stop searching after one match of any kind\n"
            "  -H, --with-filename   Print the filename for each match\n"
            "  -h, --no-filename     Suppress the prefixing filename on output\n"
            "  -c, --count           Show number of lines matching per file\n"
            "  --column              Show the column number of the first match\n"
            "\n"
            "  -A NUM, --after-context=NUM\n"
            "                        Print NUM lines of trailing context after matching\n"
            "                        lines.\n"
            "  -B NUM, --before-context=NUM\n"
            "                        Print NUM lines of leading context before matching\n"
            "                        lines.\n"
            "  -C [NUM], --context[=NUM]\n"
            "                        Print NUM lines (default 2) of output context.\n"
            "\n"
            "  --print0              Print null byte as separator between filenames,\n"
            "                        only works with -f, -g, -l, -L or -c.\n"
            "\n"
            "File presentation:\n"
            "  --pager=COMMAND       Pipes all ack output through COMMAND.  For example,\n"
            "                        --pager=\"less -R\".  Ignored if output is redirected.\n"
            "  --nopager             Do not send output through a pager.  Cancels any\n"
            "                        setting in ~/.ackrc, ACK_PAGER or ACK_PAGER_COLOR.\n"
            "  --[no]heading         Print a filename heading above each file's results.\n"
            "                        (default: on when used interactively)\n"
            "  --[no]break           Print a break between results from different files.\n"
            "                        (default: on when used interactively)\n"
            "  --group               Same as --heading --break\n"
            "  --nogroup             Same as --noheading --nobreak\n"
            "  --[no]color           Highlight the matching text (default: on unless\n"
            "                        output is redirected, or on Windows)\n"
            "  --[no]colour          Same as --[no]color\n"
            "  --color-filename=COLOR\n"
            "  --color-match=COLOR\n"
            "  --color-lineno=COLOR  Set the color for filenames, matches, and line numbers.\n"
            "  --flush               Flush output immediately, even when ack is used\n"
            "                        non-interactively (when output goes to a pipe or\n"
            "                        file).\n"
            "\n"
            "File finding:\n"
            "  -f                    Only print the files found, without searching.\n"
            "                        The PATTERN must not be specified.\n"
            "  -g REGEX              Same as -f, but only print files matching REGEX.\n"
            "  --sort-files          Sort the found files lexically.\n"
            "  --invert-file-match   Print/search handle files that do not match -g/-G.\n"
            "  --show-types          Show which types each file has.\n"
            "\n"
            "File inclusion/exclusion:\n"
            "  -a, --all-types       All file types searched;\n"
            "                        Ignores CVS, .svn and other ignored directories\n"
            "  -u, --unrestricted    All files and directories searched\n"
            "  --[no]ignore-dir=name Add/Remove directory from the list of ignored dirs\n"
            "  -r, -R, --recurse     Recurse into subdirectories (ack's default behavior)\n"
            "  -n, --no-recurse      No descending into subdirectories\n"
            "  -G REGEX              Only search files that match REGEX\n"
            "\n"
            "  --perl                Include only Perl files.\n"
            "  --type=perl           Include only Perl files.\n"
            "  --noperl              Exclude Perl files.\n"
            "  --type=noperl         Exclude Perl files.\n"
            "                        See \"ack --help type\" for supported filetypes.\n"
            "\n"
            "  --type-set TYPE=.EXTENSION[,.EXT2[,...]]\n"
            "                        Files with the given EXTENSION(s) are recognized as\n"
            "                        being of type TYPE. This replaces an existing\n"
            "                        definition for type TYPE.\n"
            "  --type-add TYPE=.EXTENSION[,.EXT2[,...]]\n"
            "                        Files with the given EXTENSION(s) are recognized as\n"
            "                        being of (the existing) type TYPE\n"
            "\n"
            "  --[no]follow          Follow symlinks.  Default is off.\n"
            "\n"
            "  Directories ignored by default:\n");

    ptr = skip_dirs;
    printf("  ");
    for(i=0;i<sizeof(skip_dirs)/sizeof(skip_dirs[0]);i++){
        if (i != 0){
            printf(", ");
        }
        printf("%s",ptr->first);
        ptr++;
    }

    printf("\n\n"
            "  Files not checked for type:\n"
            "    /~$/           - Unix backup files\n"
            "    /#.+#$/        - Emacs swap files\n"
            "    /[._].*\\.swp$/ - Vi(m) swap files\n"
/*            "    /core\\.\\d+$/   - core dumps\n"*/
            "    /tmp$/         - temp files\n"
            "\n"
            "Miscellaneous:\n"
            "  --noenv               Ignore environment variables and ~/.ackrc\n"
            "  --help                This help\n"
            "  --man                 Man page\n"
            "  --version             Display version & copyright\n"
            "  --thpppt              Bill the Cat\n"
            "\n"
            "Exit status is 0 if match, 1 if no match.\n"
            "\n"
            "This is version " VERSION " of ack in C.\n");

}





int process_config(char *fname){
    char buf[8*1024], *p,*s;
    FILE *fp;
    int res = true;
    int nline = 0;

    fp = fopen(fname,"r");
    if (fp){
        while (fgets(buf, sizeof(buf), fp) != NULL) {
            if ((p = strchr(buf, '\n')) == NULL) {
                fprintf(stderr, "%s: %s: input line too long.\n",opt.self_name,fname);
                fclose(fp);
                return false;
            }
            nline++;
            *p = '\0';
            s = buf;
            while(p!=s && isspace(*(p-1))){
                *(--p) = 0;
            }
            while(*s && isspace(*s)){
                s++;
            }

            if (*s && *s != '#'){
                char *argv[2] = {opt.self_name,s};
                if ( 1 >= opt_parse(2,argv,type_opt_find,args,NULL)){
                    res = false;
                    fprintf(stderr,"Bad option in file %s line %d\n",fname,nline);
                }
            }
        }
    }
    return res;
}

int _configure2(char *dname,char *fname){
    char name[PATH_MAX];

    if (dname){
        snprintf(name,PATH_MAX,"%s/%s",dname,fname);
        return process_config(name);
    }
    return true;
}

int _configure1(char *dname){
    bool res = true;

    if (dname){
        res =  res &&_configure2(dname,".ackrc");
        res = res && _configure2(dname,"_ackrc");
    }
    return res;
}


int configure(){
    bool res;

    res = _configure1(getenv("HOME"));
    res = res && _configure1(getenv("USERPROFILE"));
    res = res && _configure1("~");
    return res;
}

void init_skip_dirs(){
    long i;
    string_pairs_t *ptr;

    ptr = skip_dirs;

    for(i=0;i<numberof(skip_dirs);i++)
    {
        string_add(&opt.ignore_dirs,ptr->first,FILENAMENCMP);
        ptr++;
    }

}

void init_exts(){
    long i;
    for(i=0;i<numberof(file_types);i++){
        add_exts(file_types[i].name,file_types[i].exts,false);
    }
    vars.ft_text = find_filetype("text");
    vars.ft_skipped = find_filetype("skipped");
    vars.ft_make = find_filetype("make");
    vars.ft_ruby = find_filetype("ruby");
    vars.ft_binary = find_filetype("binary");
    //add_exts("make",".mk,.mak",false);
}

void init_req_filetypes(){
    filetype_t *ft;
    opt.req_filetypes = bf_new(opt.nfiletypes);

    LIST_FOREACH(ft,&opt.all_filetypes,next){
        if ( (ft->wanted == 1 ) || opt.a || (opt.types_type <= 0  && ft->wanted != -1 &&
                    (ft != vars.ft_skipped && ft != vars.ft_binary && ft != vars.ft_text)
                    )){
            bf_set(opt.req_filetypes,ft->i);
        }
    }
}

int is_regexp(char * str, int len){
    static char regexp_chars[] = ".+?*\\)]^}|";

    return (NULL != strpbrk(str,regexp_chars));
}

int main(int argc, char *argv[]){
    char *locale=NULL;
    char *locale_from=NULL;
    int options = 0;
    int to_pipe = 0;
    int from_pipe = 0;
    int errors;
    time_t start_time;
    long times;
    int nargc;


#ifdef WINDOWS
    _setmode(_fileno(stdin),_O_BINARY);
    _setmode(_fileno(stdout),_O_BINARY);
#endif



    from_pipe = !isatty(fileno(stdin));
    to_pipe =  !isatty(fileno(stdout));
    opt.self_name = argv[0];
    start_time = time(NULL);
    errors = 0;
    opt.r = true;
    opt.follow = 0;
    opt.a = 0;
    opt._break = !to_pipe;
    opt.heading = !to_pipe;
    opt.B = 0;
    opt.A = 0;
    opt.env = true;
    opt.color = 0;

#ifdef WINDOWS
    if (GetModuleHandle("ANSI32.DLL")){
        opt.color = true;
    }else if (LoadLibraryA("ANSI32.DLL")){
        opt.color = true;
    }
#else
    opt.color = true;
#endif

    opt.color_filename = strdup("\e[1;32m");
    opt.color_lineno = strdup("\e[1;33m");
    opt.color_match = strdup("\e[43;30m");
    vars.total_matches = 0;
    vars.hused = 0;
    vars.hprint = 0;
    LIST_INIT(&opt.all_filetypes);
    LIST_INIT(&opt.exts);
    LIST_INIT(&opt.ignore_dirs);
    LIST_INIT(&opt.file_list);


    init_exts();
    init_skip_dirs();

    opt.line_end = "\n";

    /* handle --noenv first */
    for(int i=0;i<argc;i++){
        if (0==strcmp(argv[i],"--noenv")){
            opt.env = false;
        }
    }
    if (opt.env && !configure()){
        errors++;
    }

    /* set from argv options */

    nargc = opt_parse(argc,argv,type_opt_find,args,NULL);

    if (argc<=1){
        print_usage();
    }else if (nargc>=0){
        if (opt.thpppt){
            print_thpppt();
        }else if (opt.help_types){
            print_types();
        }else if (opt.help){
            print_usage();
        }else if (opt.version){
            print_version();
        }else{
            /* process options */

            if (to_pipe){
                opt.color = false;
            }
            if (opt.flush){
                setbuf(stdout,NULL);
            }

            if (opt.print0){
                opt.line_end = "\0";
            }
            if (opt.group){
                opt._break = opt.heading = true;
            }

            if (opt.nogroup){
                opt._break = opt.heading = false;
            }

            if (opt.g_pattern){
                opt.f = true;
                opt.G_pattern = opt.g_pattern;
                opt.g_pattern = NULL;
            }

            if(opt.f){
                opt.c = opt.H = opt.L = opt.l = opt.m = 0;
                opt.C = opt.A = opt.B = 0;
            }


            if (opt.L){
                opt.l = opt.v = 1;
            }

            if (opt.C){
                if (opt.C<0){
                    fprintf(stderr,"%s: -C may not be negative\n",opt.self_name);
                    errors++;
                    opt.C = 0;
                }else{
                    opt.A = opt.B = opt.C;
                }
            }

            if (opt.A<0){
                fprintf(stderr,"%s: -A may not be negative\n",opt.self_name);
                errors++;
                opt.A = 0;
            }

            if (opt.B<0){
                fprintf(stderr,"%s: -B may not be negative\n",opt.self_name);
                errors++;
                opt.B = 0;
            }

            if (opt.m<0){
                fprintf(stderr,"%s: -m may not be negative\n",opt.self_name);
                errors++;
                opt.m = 0;
            }

            opt.show_filename = 1; // TODO if not a single file

            if (opt.l){
                opt.show_filename = 1;
                opt.m = 1;
            }

            if (opt.h)
                opt.show_filename = 0;

            if (opt.H)
                opt.show_filename = 1;

            if (opt.c){
                opt.m = 0;
            }

            if (opt.one){
                opt.m = 1;
            }


            if (opt.f /*|| opt.lines*/){
                if (opt.match_pattern){
                    errors++;
                    fprintf(stderr,"%s: Can't specify both a regex (%s) and use one of --line, -f or -g.\n",opt.self_name,opt.match_pattern);
                }
            }else{
                if (argc>nargc){
                    opt.match_pattern = argv[nargc];
                    nargc++;
                }
            }

            if (opt.nopager){
                opt.pager = NULL;
            }
            vars.history = malloc(sizeof(buf_t)*(opt.B+1));
            memset(vars.history,0,sizeof(buf_t)*(opt.B+1));
            opt.print_count0 = (opt.c && !opt.l);
            opt.show_total = opt.c && !opt.show_filename;
            opt.show_context = !(opt.c || opt.l || opt.f);
            opt.recursive =  (opt.r || opt.u);

            vars.filetypes = bf_new(opt.nfiletypes);
            init_req_filetypes();
            /* checks */
            if (from_pipe){
                //setmode()
                //freopen(NULL,"rb", stdin);
                if (opt.g.re || opt.f || opt.l){
                    fprintf(stderr,"%s: Can't use -f or -g or -l when acting as filter",opt.self_name);
                    errors++;
                }
                opt.show_filename = 0;
            }

            if (locale == NULL){
                locale = getenv("LC_ALL");
                locale_from = "LCC_ALL";
            }

            if (locale == NULL){
                locale = getenv("LC_CTYPE");
                locale_from = "LC_CTYPE";
            }

            if (locale != NULL){
                if (setlocale(LC_CTYPE, locale) == NULL){
                    fprintf(stderr, "%s: Failed to set locale %s (obtained from %s)\n",opt.self_name,locale, locale_from);
                    errors++;
                }else{
                    pcretables = pcre_maketables();
                }
            }


            {
                char *ptr;
                bool upper;

                if (opt.smart_case && opt.match_pattern){
                    ptr = opt.match_pattern;
                    upper = isupper(*ptr);
                    while(*ptr){
                        if (upper != isupper(*ptr)){
                            break;
                        }
                        ptr++;
                    }
                    if(*ptr){
                        opt.smart_case = 0;
                    }
                }

                opt.i = opt.i || opt.smart_case;
                if (opt.i){
                    options |= PCRE_CASELESS;
                }
            }

            if (opt.match_pattern){
                if (opt.Q || opt.w){
                    char *tmp;
                    tmp = opt.match_pattern;
                    opt.match_pattern = malloc(strlen(opt.match_pattern)+sizeof("\\b\\Q\\E\\b    "));
                    if (!opt.match_pattern){
                        fprintf(stderr,"%s: "__FILE__":"STR(__LINE__)" OOM",opt.self_name);
                        errors++;
                    }else{
                        opt.match_pattern[0] = 0;
                        if (opt.w){
                            strcat(opt.match_pattern,"\\b");
                        }
                        if (opt.Q){
                            strcat(opt.match_pattern,"\\Q");
                        }
                        strcat(opt.match_pattern,tmp);
                        if (opt.Q){
                            strcat(opt.match_pattern,"\\E");
                        }
                    }

                }
                if (opt.match_pattern){
                    if(!compile(&opt.match,opt.match_pattern,options)){//|PCRE_FIRSTLINE|PCRE_MULTILINE))
                        fprintf(stderr,"%s: Failed to compile --match regex ('%s')\n",opt.self_name,opt.match_pattern);
                        errors++;
                    }
                }
            }else if (!opt.f){
                fprintf(stderr,"%s: No regular expression found\n",opt.self_name);
                errors++;
            }

            if (opt.G_pattern){
                if (!compile(&opt.G,opt.G_pattern,0)){
                    fprintf(stderr,"%s: Failed to compile -G regex\n",opt.self_name);
                    errors++;
                }
            }


            if (!errors){
                if (from_pipe){
                    process_sdtdin(FSTDIN_HANDLE);
                }else{
                    if (nargc<argc){
                        char *ptr;

                        while(nargc<argc){
                            ptr = argv[nargc];
                            process(ptr);
                            nargc++;
                        }
                    }else{
                        process(".");
                    }
                }


                if ( vars.total_matches && opt.show_total) {
                    print_count("", vars.total_matches , "\n", 1, 0  );
                }
            }
            {
                long i;
                for(i=0;i<opt.B+1;i++){
                    free(vars.history[i].buf);
                }
                free(vars.history);
            }
            if (opt.Q || opt.w){
                free(opt.match_pattern);
            }
            bf_free(vars.filetypes);
            bf_free(opt.req_filetypes);
            free(vars.file.buf.buf);

            times = time(NULL) - start_time;

        }
    }else{
        fprintf(stderr,"See %s --help, %s --help-types or %s --man for options.\n",opt.self_name,opt.self_name,opt.self_name);
    }
    free(opt.color_filename);
    free(opt.color_match);
    free(opt.color_lineno);
    free_filetypes();

    strings_free(&opt.ignore_dirs);

    if (!times){
        times = 1;
    }
    fflush(stdout);
#ifdef DEBUG
    //assert(malloced==0);
#endif

    return (vars.files_matched != 0)?MATCH:NOMATCH;
}





#ifdef WINDOWS
int scandir( const char *dirname, struct dirent ***namelist, int (*select)(const struct dirent *), int (*compar)(const void*, const void*)){
    register struct dirent *d, *p, **names;
    register int nitems;
    register int count;
    int i;
    DIR *dir;
    struct dirent **tmp;
    int my_errno;

    if ((dir = opendir(dirname)) == NULL){
        return -1;
    }

    nitems = 0;
    count = 0;

    while( (d=readdir(dir)) ){
        if (!select || (*select)(d)){
            nitems++;
        }
    }

    rewinddir(dir);
    names = calloc(nitems,sizeof(struct dirent*));
    if (!names){
        my_errno = errno;
        goto error1;
    }

    while( (d = readdir(dir)) ){
        if (!select || (*select)(d)){
            p = malloc(sizeof(struct dirent));
            if (!p){
                my_errno = errno;
                goto error;
            }
            memcpy(p,d,sizeof(struct dirent));
            if (++count>=nitems){
                tmp = realloc(names,count*sizeof(struct dirent*));
                if (!tmp){
                    my_errno = errno;
                    goto error;
                }
                nitems = count;
                names = tmp;
            }
            names[count-1] = p;
        }
    }
    closedir(dir);

    if (compar){
        qsort(names,count,sizeof(struct dirent*),compar);
    }
    *namelist = names;
    return count;

error:
    for(i = 0; i<nitems;i++){
        free(names[i]);
    }
    free(names);

error1:
    closedir(dir);
    errno = my_errno;
    return -1;
}

#ifdef WINDOWS
int alphasort(const void *a, const void *b) {
    if (a == b){
        return 0;
    }

    return strcmp((*(struct dirent **)a)->d_name,
            (*(struct dirent **)b)->d_name);
}

#else
/*int alphasort(const void *a, const void *b){
  if (a == b){
  return 0;
  }

  return stricmp((*(struct dirent **)a)->d_name,
  (*(struct dirent **)b)->d_name);
  }
  */
#endif

#endif


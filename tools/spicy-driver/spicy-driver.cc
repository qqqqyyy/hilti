//
// A stand-alone driver for Spicy generated parsers. This code has two
// modes of operation:
//
// (1) By default, it's a C program that can be built along with *.spicy files
// using hilti-build.
//
// (2) If SPICY_DRIVER_JIT is defined, it turns into a C++ program that
// compiles into a standalone binary that will JIT *.spicy files at runtime.
//
// TODO: This is quite messy coe right now, needs a clean up.
//
// Note: This has bulk mode and threading stripped out. Shoudn't be hard to
// put it back in we need to, but it makes the code quite a bit more complex
// and not sure we still need it.

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <sys/resource.h>

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <stdint.h>

#undef DEBUG

#ifdef SPICY_DRIVER_JIT
#include <spicy.h>
#include <spicy/jit.h>
#include <hilti.h>
#include <hilti/jit.h>

extern "C" {
#include <libspicy.h>
}

#else

extern "C" {
#include <libspicy.h>
#include <libhilti.h>
}

struct Options {
    bool debug;
    bool optimize;
    unsigned int profile;

    Options()
    {
        debug = optimize = false;
        profile = 0;
    }
};

#endif

typedef struct {
    int first;
    const char* second;
    int mark;
} Embed;

int driver_debug = 0;
int debug_hooks = 0;

spicy_parser* request = 0;
spicy_parser* reply = 0;

#ifdef SPICY_DRIVER_JIT
std::set<string> cgdbg;
typedef hlt_list* (*spicy_parsers_func)(hlt_exception** excpt, hlt_execution_context* ctx);
spicy_parsers_func JitParsers = nullptr;
std::unique_ptr<hilti::JIT> Jit;
std::shared_ptr<spicy::CompilerContext> PacContext;
#endif

static void check_exception(hlt_exception* excpt)
{
    if ( excpt ) {
        hlt_execution_context* ctx = hlt_global_execution_context();
        hlt_exception_print_uncaught(excpt, ctx);

        if ( hlt_exception_is_yield(excpt) || hlt_exception_is_termination(excpt) )
            exit(0);
        else {
            GC_DTOR(excpt, hlt_exception, ctx);
            GC_DTOR(request, hlt_SpicyHilti_Parser, ctx);
            GC_DTOR(reply, hlt_SpicyHilti_Parser, ctx);
            exit(1);
        }
    }
}

static void usage(const char* prog)
{
#ifdef SPICY_DRIVER_JIT
    auto dbglist = spicy::Options().cgDebugLabels();
    auto dbgstr = util::strjoin(dbglist.begin(), dbglist.end(), "/");
#endif

    fprintf(stderr, "%s *.spicy [options]\n\n", prog);
    fprintf(stderr, "  Options:\n\n");
    fprintf(stderr, "    -p <parser>[/<reply-parsers>]  Use given parser(s)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "    -g            Enable spicy-driver's debug output\n");
    fprintf(stderr, "    -B            Enable Spicy debugging hooks\n");
    fprintf(stderr, "    -i <n>        Feed input incrementally in chunks of size <n>\n");
    fprintf(stderr,
            "    -e <off:str>  Embed string <str> at offset <off>; can be given multiple times\n");
    fprintf(stderr, "    -l            Show available parsers\n");
    fprintf(stderr, "    -m <off>      Set mark at offset <off>; can be given multiple times\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "    -P            Enable profiling\n");
    fprintf(stderr, "    -c            After parsing, compose data back to binary\n");
#ifdef SPICY_DRIVER_JIT
    fprintf(stderr, "    -I            Add directory to import path.\n");
    fprintf(stderr, "    -d            Enable debug mode for JIT compilation\n");
    fprintf(stderr, "    -D <type>     Debug output during code generation; type can be %s\n",
            dbgstr.c_str());
    fprintf(stderr, "    -O            Optimize generated code.             [Default: off].\n");
    fprintf(stderr, "    -C            Use module cache.                    [Default: off].\n");
#endif
    fprintf(stderr, "\n");

    exit(1);
}

hlt_list* getParsers()
{
    hlt_exception* excpt = 0;
    hlt_execution_context* ctx = hlt_global_execution_context();

#ifdef SPICY_DRIVER_JIT
    assert(JitParsers);
    return (*JitParsers)(&excpt, ctx);
#else
    return spicy_parsers(&excpt, ctx);
#endif
}

void listParsers()
{
    hlt_exception* excpt = 0;
    hlt_execution_context* ctx = hlt_global_execution_context();

    hlt_list* parsers = getParsers();

    if ( hlt_list_size(parsers, &excpt, ctx) == 0 ) {
        fprintf(stderr, "  No parsers defined.\n\n");
        exit(1);
    }

    fprintf(stderr, "  Available parsers: \n\n");

    hlt_iterator_list i = hlt_list_begin(parsers, &excpt, ctx);
    hlt_iterator_list end = hlt_list_end(parsers, &excpt, ctx);

    int count = 0;

    while ( ! (hlt_iterator_list_eq(i, end, &excpt, ctx) || excpt) ) {
        ++count;

        spicy_parser* p = *(spicy_parser**)hlt_iterator_list_deref(i, &excpt, ctx);

        fputs("    ", stderr);

        hlt_string_print(stderr, p->name, 0, &excpt, ctx);

        for ( int n = 25 - hlt_string_len(p->name, &excpt, ctx); n; n-- )
            fputc(' ', stderr);

        hlt_string_print(stderr, p->description, 0, &excpt, ctx);

        int first = 1;

        if ( p->ports ) {
            hlt_iterator_list j = hlt_list_begin(p->ports, &excpt, ctx);
            hlt_iterator_list end2 = hlt_list_end(p->ports, &excpt, ctx);

            while ( ! (hlt_iterator_list_eq(j, end2, &excpt, ctx) || excpt) ) {
                if ( first )
                    fputs("  [", stderr);
                else
                    fputs(", ", stderr);

                hlt_port p = *(hlt_port*)hlt_iterator_list_deref(j, &excpt, ctx);
                hlt_string s = hlt_object_to_string(&hlt_type_info_hlt_port, &p, 0, &excpt, ctx);
                hlt_string_print(stderr, s, 0, &excpt, ctx);

                j = hlt_iterator_list_incr(j, &excpt, ctx);
                first = 0;
            }
        }

        if ( p->mime_types ) {
            hlt_iterator_list j = hlt_list_begin(p->mime_types, &excpt, ctx);
            hlt_iterator_list end2 = hlt_list_end(p->mime_types, &excpt, ctx);

            while ( ! (hlt_iterator_list_eq(j, end2, &excpt, ctx) || excpt) ) {
                if ( first )
                    fputs("  [", stderr);
                else
                    fputs(", ", stderr);

                hlt_string s = *(hlt_string*)hlt_iterator_list_deref(j, &excpt, ctx);
                hlt_string_print(stderr, s, 0, &excpt, ctx);

                j = hlt_iterator_list_incr(j, &excpt, ctx);
                first = 0;
            }
        }

        if ( ! first )
            fputc(']', stderr);

        fputc('\n', stderr);

        i = hlt_iterator_list_incr(i, &excpt, ctx);
    }

    check_exception(excpt);

    if ( ! count )
        fprintf(stderr, "    None.\n");

    fputs("\n", stderr);
}

static spicy_parser* findParser(const char* name)
{
    hlt_execution_context* ctx = hlt_global_execution_context();
    hlt_exception* excpt = 0;

    hlt_list* parsers = getParsers();

    hlt_string hname = hlt_string_from_asciiz(name, &excpt, ctx);

    hlt_iterator_list i = hlt_list_begin(parsers, &excpt, ctx);
    hlt_iterator_list end = hlt_list_end(parsers, &excpt, ctx);

    spicy_parser* result = 0;

    while ( ! (hlt_iterator_list_eq(i, end, &excpt, ctx) || excpt) ) {
        spicy_parser* p = *(spicy_parser**)hlt_iterator_list_deref(i, &excpt, ctx);

        if ( hlt_string_cmp(hname, p->name, &excpt, ctx) == 0 ) {
            result = p;
            break;
        }

        i = hlt_iterator_list_incr(i, &excpt, ctx);
    }

    check_exception(excpt);

    return result;
}

hlt_bytes* readAllInput(Embed* embeds)
{
    hlt_execution_context* ctx = hlt_global_execution_context();
    hlt_exception* excpt = 0;
    hlt_bytes* input = hlt_bytes_new(&excpt, ctx);
    check_exception(excpt);

    int8_t buffer[4096];

    if ( embeds->second && embeds->first == 0 ) {
        if ( embeds->mark )
            hlt_bytes_append_mark(input, &excpt, ctx);

        else {
            hlt_string s = hlt_string_from_asciiz(embeds->second, &excpt, ctx);
            hlt_bytes_append_object(input, &hlt_type_info_hlt_string, &s, &excpt, ctx);
        }

        ++embeds;
    }

    size_t offset = 0;

    while ( ! excpt ) {
        size_t len = embeds->second ? (embeds->first - offset) : sizeof(buffer);
        size_t n = fread(buffer, 1, len, stdin);
        offset += n;

        int8_t* copy = (int8_t*)hlt_malloc(n);
        memcpy(copy, buffer, n);
        hlt_bytes_append_raw(input, copy, n, &excpt, ctx);

        if ( embeds->second ) {
            if ( embeds->mark )
                hlt_bytes_append_mark(input, &excpt, ctx);

            else {
                hlt_string s = hlt_string_from_asciiz(embeds->second, &excpt, ctx);
                hlt_bytes_append_object(input, &hlt_type_info_hlt_string, &s, &excpt, ctx);
            }

            ++embeds;
        }

        if ( feof(stdin) )
            break;

        if ( ferror(stdin) ) {
            fprintf(stderr, "error while reading from stdin\n");
            exit(1);
        }
    }

    check_exception(excpt);

    // hlt_string s = hlt_bytes_to_string(&hlt_type_info_hlt_bytes, &input, 0, &excpt, ctx);
    // hlt_string_print(stderr, s, 1, &excpt, ctx);

    return input;
}

static void dump_memstats()
{
    if ( ! driver_debug )
        return;

    hlt_memory_stats stats = hlt_memory_statistics();
    uint64_t heap = stats.size_heap / 1024 / 1024;
    uint64_t alloced = stats.size_alloced / 1024 / 1024;
    uint64_t total_refs = stats.num_refs;
    uint64_t current_allocs = stats.num_allocs - stats.num_deallocs;
    uint64_t num_nullbuffer = stats.num_nullbuffer;
    uint64_t max_nullbuffer = stats.max_nullbuffer;

    fprintf(stderr,
            "--- spicy-driver stats: "
            "%" PRIu64
            "M heap, "
            "%" PRIu64
            "M alloced, "
            "%" PRIu64
            " allocations, "
            "%" PRIu64
            " totals refs "
            "%" PRIu64
            " in nullbuffer "
            "%" PRIu64
            " max nullbuffer"
            "\n",
            heap, alloced, current_allocs, total_refs, num_nullbuffer, max_nullbuffer);
}

void composeOutput(hlt_bytes* data, void** obj, hlt_type_info* type, void* user,
                   hlt_exception** excpt, hlt_execution_context* ctx)
{
    hlt_bytes_block block;
    hlt_iterator_bytes start = hlt_bytes_begin(data, excpt, ctx);
    hlt_iterator_bytes end = hlt_bytes_end(data, excpt, ctx);

    void* cookie = 0;

    do {
        cookie = hlt_bytes_iterate_raw(&block, cookie, start, end, excpt, ctx);
        fwrite(block.start, 1, (block.end - block.start), stdout);
    } while ( cookie );
}

void processParseObject(spicy_parser* p, void* pobj)
{
    if ( ! p->compose_func )
        return;

    hlt_execution_context* ctx = hlt_global_execution_context();
    hlt_exception* excpt = 0;

    GC_CCTOR_GENERIC(&pobj, p->type_info, ctx);
    p->compose_func(pobj, composeOutput, 0, &excpt, ctx);
    GC_DTOR_GENERIC(&pobj, p->type_info, ctx);

    check_exception(excpt);
}

void parseSingleInput(spicy_parser* p, int chunk_size, Embed* embeds)
{
    hlt_execution_context* ctx = hlt_global_execution_context();
    hlt_exception* excpt = 0;

    hlt_bytes* input = readAllInput(embeds);
    GC_CCTOR(input, hlt_bytes, ctx);

    hlt_iterator_bytes cur = hlt_bytes_begin(input, &excpt, ctx);

    check_exception(excpt);

    if ( ! chunk_size ) {
        // Feed all input at once.
        hlt_bytes_freeze(input, 1, &excpt, ctx);

        if ( driver_debug )
            fprintf(stderr, "--- spicy-driver: starting parsing single input chunk.\n");

        void* pobj = (*p->parse_func)(input, 0, &excpt, ctx);

        if ( driver_debug )
            fprintf(stderr, "--- spicy-driver: done parsing single input chunk.\n");

        processParseObject(p, pobj);

        GC_DTOR(input, hlt_bytes, ctx);
        check_exception(excpt);
        dump_memstats();
        return;
    }

    // Feed incrementally.

    hlt_bytes* chunk1;
    hlt_iterator_bytes end = hlt_bytes_end(input, &excpt, ctx);
    hlt_iterator_bytes cur_end;
    int8_t done = 0;
    hlt_exception* resume = 0;

    hlt_bytes* incr_input = hlt_bytes_new(&excpt, ctx);
    GC_CCTOR(incr_input, hlt_bytes, ctx);

    dump_memstats();

    while ( ! done ) {
        if ( hlt_bytes_at_object(cur, &hlt_type_info_hlt_string, &excpt, ctx) ) {
            done = 0;
            void* o = hlt_bytes_retrieve_object(cur, &hlt_type_info_hlt_string, &excpt, ctx);
            hlt_bytes_append_object(incr_input, &hlt_type_info_hlt_string, o, &excpt, ctx);
            cur_end = hlt_bytes_skip_object(cur, &excpt, ctx);
        }

        else
            cur_end = hlt_iterator_bytes_incr_by(cur, chunk_size, &excpt, ctx);

        done = (hlt_iterator_bytes_eq(cur_end, end, &excpt, ctx) &&
                ! hlt_bytes_at_object(cur_end, &hlt_type_info_hlt_string, &excpt, ctx));

        // fprintf(stderr, "\n");
        chunk1 = hlt_bytes_sub(cur, cur_end, &excpt, ctx);
        hlt_bytes_append(incr_input, chunk1, &excpt, ctx);

        if ( done )
            hlt_bytes_freeze(incr_input, 1, &excpt, ctx);

        int frozen = hlt_bytes_is_frozen(incr_input, &excpt, ctx);

        check_exception(excpt);

        // Ref count the persistent locals, we may trigger safepoints.
        GC_CCTOR(cur_end, hlt_iterator_bytes, ctx);

        if ( ! resume ) {
            if ( driver_debug )
                fprintf(stderr, "--- spicy-driver: starting parsing (eod=%d).\n", frozen);

            void* pobj = (*p->parse_func)(incr_input, 0, &excpt, ctx);
            processParseObject(p, pobj);
        }

        else {
            if ( driver_debug )
                fprintf(stderr, "--- spicy-driver: resuming parsing (eod=%d, excpt=%p).\n", frozen,
                        static_cast<void *>(resume));

            void* pobj = (*p->resume_func)(resume, &excpt, ctx);
            processParseObject(p, pobj);
        }

        GC_DTOR(cur_end, hlt_iterator_bytes, ctx);

        if ( driver_debug )
            dump_memstats();

        if ( excpt ) {
            if ( hlt_exception_is_yield(excpt) ) {
                if ( driver_debug )
                    fprintf(stderr, "--- spicy-driver: parsing yielded.\n");

                resume = excpt;
                excpt = 0;
            }

            else
                check_exception(excpt);
        }

        else if ( ! done ) {
            if ( driver_debug )
                fprintf(stderr,
                        "spicy-driver: end of input reached even though more could be parsed.");

            break;
        }

        cur = cur_end;
    }

    GC_DTOR(incr_input, hlt_bytes, ctx);
    GC_DTOR(input, hlt_bytes, ctx);
}

#ifdef SPICY_DRIVER_JIT

// C++ code for JIT version.

bool jitSpicy(const std::list<string>& spcy, std::shared_ptr<spicy::Options> options)
{
    hilti::init();
    spicy::init();

    PacContext = std::make_shared<spicy::CompilerContext>(options);

    std::list<std::unique_ptr<llvm::Module>> llvm_modules;

    for ( auto p : spcy ) {
        auto llvm_module = PacContext->compile(p);

        if ( ! llvm_module ) {
            fprintf(stderr, "compiling %p failed\n", p.c_str());
            return false;
        }

        llvm_modules.push_back(std::move(llvm_module));
    }

    auto linked_module = PacContext->linkModules("<jit analyzers>", std::move(llvm_modules));

    if ( ! linked_module ) {
        fprintf(stderr, "linking failed\n");
        return false;
    }

    Jit = PacContext->hiltiContext()->jit(std::move(linked_module));

    if ( ! Jit ) {
        fprintf(stderr, "jit failed");
        return false;
    }

    JitParsers = (spicy_parsers_func)Jit->nativeFunction("spicy_parsers");

    if ( ! JitParsers ) {
        fprintf(stderr, "jit error: no function 'spicy_parsers'");
        return false;
    }

    return true;
}

#endif

int main(int argc, char** argv)
{
    int chunk_size = 0;
    int list_parsers = false;
    const char* parser = 0;
    char* reply_parser = 0;
    Embed embeds[256];
    int embeds_count = 0;

    const char* progname = argv[0];

#ifdef SPICY_DRIVER_JIT
    auto options = std::make_shared<spicy::Options>();

    options->generate_parsers = true;
    options->generate_composers = false;
#else
    ::Options _options;
    ::Options* options = &_options;
#endif

    char ch;
    while ( (ch = getopt(argc, argv, "i:p:t:v:s:dOBhD:UlTPgCI:e:m:c")) != -1 ) {
        switch ( ch ) {
        case 'i':
            chunk_size = atoi(optarg);
            break;

        case 'p':
            parser = optarg;
            break;

        case 'd':
            options->debug = true;
            break;

        case 'g':
            ++driver_debug;
            break;

        case 'B':
            debug_hooks = 1;
            break;

        case 'P':
            ++options->profile;
            break;

        case 'l':
            list_parsers = true;
            break;

        case 'e': {
            char* m = strchr(optarg, ':');

            if ( ! m ) {
                fprintf(stderr, "syntax error for -e argument\n");
                exit(1);
            }

            *m = '\0';
            int offset = atoi(optarg);

            if ( embeds_count < static_cast<int>(sizeof(embeds)) - 1 ) {
                embeds[embeds_count].first = offset;
                embeds[embeds_count].second = m + 1;
                embeds[embeds_count].mark = 0;
                embeds_count++;
            }

            break;
        }

        case 'm': {
            int offset = atoi(optarg);

            if ( embeds_count < static_cast<int>(sizeof(embeds)) - 1 ) {
                embeds[embeds_count].first = offset;
                embeds[embeds_count].second = optarg; // Dummy, just != 0.
                embeds[embeds_count].mark = 1;
                embeds_count++;
            }

            break;
        }

#ifdef SPICY_DRIVER_JIT
        case 'c':
            options->generate_composers = true;
            break;

        case 'I':
            options->libdirs_spicy.push_back(optarg);
            break;

        case 'D':
            options->cg_debug.insert(optarg);
            break;

        case 'O':
            options->optimize = true;
            break;

        case 'C':
            options->module_cache = ".cache";
            break;
#endif

        case 'h':
        // Fall-through.
        default:
            usage(progname);
        }
    }

    argc -= optind;
    argv += optind;

    embeds[embeds_count].first = 0;
    embeds[embeds_count].second = 0;

    hlt_config cfg = *hlt_config_get();

    if ( options->profile ) {
        fprintf(stderr, "Enabling profiling ...\n");
        cfg.profiling = 1;
    }

    hlt_config_set(&cfg);

#ifdef SPICY_DRIVER_JIT
    std::list<string> spcy;

    for ( int i = 0; i < argc; i++ )
        spcy.push_back(argv[i]);

    if ( ! spcy.size() )
        usage(progname);

    options->jit = true;

    if ( ! jitSpicy(spcy, options) )
        return 1;
#else
    hlt_init();
    spicy_init();
#endif

    if ( list_parsers ) {
        listParsers();
        return 0;
    }

    hlt_execution_context* ctx = hlt_global_execution_context();

    spicy_enable_debugging(debug_hooks);

    if ( ! parser ) {
        hlt_exception* excpt = 0;

        hlt_list* parsers = getParsers();

        // If we have exactly one parser, that's the one we'll use.
        if ( hlt_list_size(parsers, &excpt, ctx) == 1 ) {
            hlt_iterator_list i = hlt_list_begin(parsers, &excpt, ctx);
            request = *(spicy_parser**)hlt_iterator_list_deref(i, &excpt, ctx);
            assert(request);
            reply = request;
        }

        else {
            // If we don't have any parsers, we do nothing and just exit
            // normally.
            int64_t size = hlt_list_size(parsers, &excpt, ctx);

            if ( size == 0 )
                exit(0);

            fprintf(stderr, "no parser specified; see usage for list.\n");
            exit(1);
        }
    }

    else {
        reply_parser = strchr((char*)parser, '/');
        if ( reply_parser )
            *reply_parser++ = '\0';

        request = findParser(parser);

        if ( ! request ) {
            fprintf(stderr, "unknown parser '%s'. See usage for list.\n", parser);
            exit(1);
        }

        if ( reply_parser ) {
            reply = findParser(reply_parser);

            if ( ! reply ) {
                fprintf(stderr, "unknown reply parser '%s'. See usage for list.\n", reply_parser);
                exit(1);
            }
        }

        else
            reply = request;
    }

    assert(request && reply);

    if ( request && ! request->parse_func ) {
        fprintf(stderr, "parsing support not compile for %s\n", parser);
        exit(1);
    }

    if ( reply && ! reply->parse_func ) {
        fprintf(stderr, "parsing support not compile for %s\n", reply_parser);
        exit(1);
    }

    GC_CCTOR(request, hlt_SpicyHilti_Parser, ctx);
    GC_CCTOR(reply, hlt_SpicyHilti_Parser, ctx);

    if ( options->profile ) {
        hlt_exception* excpt = 0;
        hlt_string profiler_tag =
            hlt_string_from_asciiz("app-total", &excpt, hlt_global_execution_context());
        hlt_profiler_start(profiler_tag, Hilti_ProfileStyle_Standard, 0, 0, &excpt,
                           hlt_global_execution_context());
    }

    parseSingleInput(request, chunk_size, embeds);

    if ( options->profile ) {
        hlt_exception* excpt = 0;
        hlt_string profiler_tag =
            hlt_string_from_asciiz("app-total", &excpt, hlt_global_execution_context());
        hlt_profiler_stop(profiler_tag, &excpt, hlt_global_execution_context());
    }

    GC_DTOR(request, hlt_SpicyHilti_Parser, ctx);
    GC_DTOR(reply, hlt_SpicyHilti_Parser, ctx);

    exit(0);
}

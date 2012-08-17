
#include <getopt.h>
#include <iostream>
#include <fstream>

#include <binpac.h>
#include <hilti.h>
#include <util.h>

using namespace std;

const char* Name = "binpac";

int  debug = 0;
bool dump_ast = false;
bool verify = true;
bool resolve = true;
bool output_binpac = false;
bool dbg_scanner = false;
bool dbg_parser = false;
bool dbg_scopes = false;

string output = "/dev/stdout";
binpac::string_list import_paths;

static struct option long_options[] = {
    { "ast",     no_argument, 0, 'A' },
    { "debug",   no_argument, 0, 'd' },
    { "cgdebug", no_argument, 0, 'D' },
    { "help",    no_argument, 0, 'h' },
    { "print",   no_argument, 0, 'p' },
    { "print-always",   no_argument, 0, 'W' },
    { "prototypes", no_argument, 0, 'P' },
    { "output",  required_argument, 0, 'o' },
    { "version", no_argument, 0, 'v' },
    { 0, 0, 0, 0 }
};

void usage()
{
    cerr << "Usage: " << Name << " [options] <input.pac2>\n"
            "\n"
            "Options:\n"
            "\n"
            "  -A | --ast            Dump intermediary ASTs to stderr.\n"
            "  -d | --debug          Debug level for the generated code. Each time increases level. [Default: 0]\n"
            "  -D | --cgdebug <type> Debug output during code generations; type can be scanner/parser/scopes.\n"
            "  -h | --help           Print usage information.\n"
            "  -I | --import <dir>   Add directory to import path.\n"
            "  -n | --no-validate    Do not validate resulting BinPAC++ or HILTI ASTs (for debugging only).\n"
            "  -o | --output <file>  Specify output file.                    [Default: stdout].\n"
            "  -p | --print          Just output all parsed BinPAC++ code again.\n"
            "  -W | --print-always   Like -p, but don't verify correctness first.\n"
            "  -v | --version        Print version information.\n"
            "\n";
}

void version()
{
    cerr << Name << " v" << binpac::version() << endl;
}

void error(const string& file, const string& msg)
{
    if ( file.size() )
        cerr << util::basename(file) << ": ";

    cerr << msg << endl;
    exit(1);
}

int main(int argc, char** argv)
{
    while ( true ) {
        int c = getopt_long(argc, argv, "AdD:o:nWpI:vh", long_options, 0);

        if ( c < 0 )
            break;

        switch (c) {
         case 'A':
            dump_ast = true;
            break;

         case 'd':
            ++debug;
            break;

         case 'D':
            if ( strcmp(optarg, "scanner") == 0 ) {
                dbg_scanner = true;
                break;
            }

            if ( strcmp(optarg, "parser") == 0 ) {
                dbg_parser = true;
                break;
            }

            if ( strcmp(optarg, "scopes") == 0 ) {
                dbg_scopes = true;
                break;
            }

            error(0, util::fmt("unknown debug type '%s'", optarg));
            break;

         case 'o':
            output = optarg;
            break;

         case 'n':
            verify = false;
            break;

         case 'W':
            output_binpac = true;
            verify = false;
            break;

         case 'p':
            output_binpac = true;
            break;

         case 'I':
            import_paths.push_back(optarg);
            break;

         case 'v':
            ::version();
            return 0;

         case 'h':
            usage();
            return 0;

         default:
            usage();
            return 1;
        }

    }

    if ( optind == argc )
        error("", "No input file given.");

    if ( optind - argc > 1 )
        error("", "Two many input file given.");

    string input = argv[optind];

    ofstream out;
    out.open(output, ios::out | ios::trunc);

    if ( ! out.good() )
        error(output, "Cannot open file for output.");

    hilti::init();
    binpac::init();

    auto ctx = std::make_shared<binpac::CompilerContext>(import_paths);
    ctx->enableDebug(dbg_scanner, dbg_parser, dbg_scopes);

    auto module = ctx->load(input, verify);

    if ( ! module ) {
        error(input, "Aborting due to input error.");
        return 1;
    }

    if ( dump_ast )
        ctx->dump(module, cerr);

    if ( output_binpac )
        return ctx->print(module, out);

    auto hilti_module = ctx->compile(module, debug, verify);

    if ( ! hilti_module ) {
        error(input, "Aborting due to compilation error.");
        return 1;
    }

    if ( ! hilti::printAST(hilti_module, out) ) {
        error(input, "Aborting due to HILTI printer error.");
        return 1;
    }

    return 0;
}
/**
 * JIT compiler for HILTI and Spicy code.
 *
 * The compiler compiles all the *.spicy and *.evt files it finds and hooks
 * them into Bro's analyzer infrastructure. The process proceeds in several
 * stages:
 *
 * [TODO] I believe this needs updtating.
 *
 * 1. Once all Bro scripts have been read, we load and parse all *.spicy.
 *
 * 2. We load and parse all *.evt files. For each event in there that has a
 *    Bro script handler defined, we
 *
 *    - create in-memory HILTI code that interface with Bro the raise the
 *      event
 *
 *    - create in-memory Spicy code that adds a hook that at runtime will
 *      call that HILTI code to trigger the event.
 *
 *    - create a Bro-level event and ensure that if the user has any handlers
 *    defined, the types match.
 *
 * 3. We compile and link all *.hlt and *.spicy files (including those from
 *    step 2) into native code.
 *
 * \note: In the future the loader will also load further *.hlt files but we
 * don't support further HILTI-only functionality yet. So for now it just
 * compiles Spicy analyzers along with their event specifications.
 */

#ifndef BRO_PLUGIN_HILTI_MANAGER_H
#define BRO_PLUGIN_HILTI_MANAGER_H

#include <functional>
#include <istream>
#include <memory>

#include <analyzer/Analyzer.h>
#include <file_analysis/Analyzer.h>

class BroType;

struct __hlt_list;
struct __spicy_parser;

namespace hilti {
class CompilerContext;
class Expression;
class Type;
class Module;

namespace declaration {
class Function;
}

namespace builder {
class BlockBuilder;
class ModuleBuilder;
}
}

namespace spicy {
class Type;

namespace type {
namespace unit {
class Item;
}
}

namespace declaration {
class Function;
}
}

namespace llvm {
class Module;
}

namespace bro {
namespace hilti {

struct SpicyEventInfo;
struct SpicyAnalyzerInfo;
struct SpicyFileAnalyzerInfo;
struct SpicyModuleInfo;
struct SpicyExpressionAccessor;

namespace compiler {
class ModuleBuilder;
}

class Manager {
public:
    /**
     * Constructor.
     *
     */
    Manager();

    /**
      * Destructor.
      */
    ~Manager();

    /**
     * Adds one or more paths to find further *.spicy and *.hlt library
     * modules. The path will be passed to the compiler. Note that this
     * must be called only before InitPreScripts().
     *
     * paths: The directories to search. Multiple directories can be
     * given at once by separating them with a colon.
     */
    void AddLibraryPath(const char* dir);

    /**
     * Initializes parts of the Manager that don't require Bro's scripts
     * bein parsed yet. Note that all AddLibraryPath() calls must have
     * been finished at this time.
     *
     * @return False if an error occured.
     *
     */
    bool InitPreScripts();

    /**
     * Initializes the remaining parts of the Manager that require Bro's
     * scripts being parsed already.
     *
     * @return False if an error occured.
     */
    bool InitPostScripts();

    /**
     * Loaded all files still queued for doing so.
     *
     * @return True if all files have read and compiled successfully; if not,
     * error messages will have been written to the reporter.
     */
    bool FinishLoading();

    /**
     * Marks an *.spicy, *.evt, or *.hlt file for loading. Note that it
     * won't necessarily load them all immediately, but may queue some
     * for later compilation via LoadAll().
     */
    bool LoadFile(const std::string& file);

    /**
     * After user scripts have been read, compiles and links all
     * resulting HILTI code. This implements steps 3 to 5.
     *
     * Must be called before any packet processing starts.
     *
     * @return True if successful.
     */
    bool Compile();

    /**
     * Dumps a debug summary to stderr. This should be called only after
     * Compile().
     */

    /**
     * Returns the Spicy name for a given analyzer. Returns an error
     * string if the tag doesn't correspond to a Spicy analyzer.
     */
    std::string AnalyzerName(const analyzer::Tag& tag);

    /**
     * Returns the Spicy name for a given file analyzer. Returns an
     * error string if the tag doesn't correspond to a Spicy analyzer.
     */
    std::string FileAnalyzerName(const file_analysis::Tag& tag);

    /**
     * Returns the Spicy parser object for an analyzer.
     *
     * analyer: The requested analyzer.
     *
     * is_orig: True if the desired parser is for the originator side,
     * false for the respinder.
     *
     * Returns: The parser, or null if we don't have one for this tag.
     * Note that this is a HILTI ref'cnted object. When storing the
     * pointer, make sure to cctor it.
     */
    struct __spicy_parser* ParserForAnalyzer(const analyzer::Tag& tag, bool is_orig);

    /**
     * Returns the Spicy parser object for a file analyzer.
     *
     * analyer: The requested file analyzer.
     *
     * Returns: The parser, or null if we don't have one for this tag.
     * Note that this is a HILTI ref'cnted object. When storing the
     * pointer, make sure to cctor it.
     */
    struct __spicy_parser* ParserForFileAnalyzer(const file_analysis::Tag& tag);

    /**
     * Returns the analyzer tag that should be passed to script-land when
     * talking about an analyzer. This is normally the analyzer's
     * standard tag, but may be replaced with somethign else if the
     * analyzers substitutes for an existing one.
     *
     * tag: The original tag we query for how to pass it to script-land.
     *
     * Returns: The desired tag for passing to script-land.
     */
    analyzer::Tag TagForAnalyzer(const analyzer::Tag& tag);

    /**
     * Raises an event that has been compiled into HILTI code during
     * runtime. This must be called only during runtime after all event
     * handlers have been compiled and the JIT setup (i.e., after \a
     * Compile()).
     *
     * @param event The event to raise.
     *
     * @return True if successfull, i.e., the event exists. In that case,
     * the event will have taken ownership of the \a event.
     */
    bool RuntimeRaiseEvent(Event* event);

    /**
     * XXX
     */
    std::pair<bool, Val*> RuntimeCallFunction(const Func* func, Frame* parent, val_list* args);

    /**
     * XXX
     */
    void RegisterNativeFunction(const ::Func* func, void* native);

    /**
     * XXX
     */
    bool HaveCustomHandler(const ::Func* ev);

    /**
     * XXX
     */
    bool HaveCustomHiltiCode();

    /**
     * Returns true if either there's at least one handler defined for
     * the given event, or we're otherwise told to generate the code for
     * it even if not
     */
    bool WantEvent(std::shared_ptr<SpicyEventInfo> ev);

    /**
     * Returns true if either there's at least one handler defined for
     * the given event, or we're otherwise told to generate the code for
     * it even if not
     */
    bool WantEvent(SpicyEventInfo* ev);

    /** Dumps a summary all Spicy/HILTI analyzers/events/code to standard error.
     */
    void DumpDebug();

    /** Dumps generated code to standard error.
     *
     * \todo This is probably going to go aways; dumping the code into
     * separate files via the \c dump_* options seems more helpful.
     */
    void DumpCode(bool all);

    /**
     * Dumps HILTI memory statistics to stdandard errror.
     */
    void DumpMemoryStatistics();

protected:
    // We include the protected part only when compiling the hilti/
    // subsystem so that we can use C++11's shared_ptr.
    //
    /**
     * Initialized the HILTI and Spicy compiler subsystems.
     */
    void InitHILTI();

#if 0
	/** Implements the search logic for both LoadSpicyModules() and LoadSpicyEvents().
	 *
	 * @param ext The file extension to search.
	 *
	 * @param callback A callback to execute for files found.
	 *
	 * @return True if all events have been processed successfully; if
	 * not, error messages will have been written to the reporter.
	 */
	bool SearchFiles(const char*ext, std::function<bool (std::istream& in, const std::string& path)> const & callback);
#endif

    /**
     * Searches a file along the manager's library paths.
     *
     * file: The name of the file to search for. This can be an absolute or
     * relative path.
     *
     * relative_to: If given, the method chdirs to this directory before
     * starting to search *file*. It will chdir back to the original location
     * before return.
     *
     * Returns: The full absolute path to the file, or an empty string if not found.
     */
    std::string SearchFile(const std::string& file, const std::string& relative_to = "") const;

    /**
     * Loads one *.spicy file.
     *
     * @param path The full path to load the file from.
     *
     * @return True if successfull.
     */
    bool LoadSpicyModule(const std::string& path);

    /**
     * Loads one *.evt file.
     *
     * @param path The full path to load the file from.
     *
     * @return True if successfull.
     */
    bool LoadSpicyEvents(const std::string& path);

    /**
     * Parses a single event specification.
     *
     * chunk: The semicolon-separated specification; may contain newlines, which will be ignored.
     *
     * @return Returns the new event instance if parsing was sucessful;
     * passes ownership. Null if there was an error.
     */
    std::shared_ptr<SpicyEventInfo> ParseSpicyEventSpec(const std::string& chunk);

    /**
     * Parses and registers a single analyzer specification.
     *
     * chunk: The semicolon-separated specification; may contain newlines, which will be ignored.
     *
     * @return Returns the analyzer info with parsed values filled, or
     * null if an error occurred.
     */
    std::shared_ptr<SpicyAnalyzerInfo> ParseSpicyAnalyzerSpec(const std::string& chunk);

    /**
     * Parses and registers a single file analyzer specification.
     *
     * chunk: The semicolon-separated specification; may contain newlines, which will be ignored.
     *
     * @return Returns the analyzer info with parsed values filled, or
     * null if an error occurred.
     */
    std::shared_ptr<SpicyFileAnalyzerInfo> ParseSpicyFileAnalyzerSpec(const std::string& chunk);

    /**
     * Registers a Bro event for a Spicy event.
     *
     * ev: The event to register. The corresponding Bro event must not
     * yet exist.
     */
    void RegisterBroEvent(std::shared_ptr<SpicyEventInfo> ev);

    /**
     * XXXX
     */
    void BuildBroEventSignature(std::shared_ptr<SpicyEventInfo> ev);

    /**
     * XXX
     */
    bool PopulateEvent(std::shared_ptr<SpicyEventInfo> ev);

    /**
     * XXX
     */
    bool CompileBroScripts();

    /**
     * Registers a Bro analyzer defined in an analyzer specification.
     *
     * a: The analyzer to register.
     */
    void RegisterBroAnalyzer(std::shared_ptr<SpicyAnalyzerInfo> a);

    /**
     * Registers a Bro analyzer defined in an analyzer specification.
     *
     * a: The analyzer to register.
     */
    void RegisterBroFileAnalyzer(std::shared_ptr<SpicyFileAnalyzerInfo> a);

    /**
     * Creates the Spicy hook for an event.
     *
     * @param event The event to create the code for.
     *
     * @return True if successful.
     */
    bool CreateSpicyHook(SpicyEventInfo* ev);

    /**
     * XXX
     */
    bool CreateExpressionAccessors(std::shared_ptr<SpicyEventInfo> ev);

    /**
     * Creates a Spicy function for an event argument expression that
     * extracts the corresponding value from the parse object.
     */
    std::shared_ptr<::spicy::declaration::Function> CreateSpicyExpressionAccessor(
        std::shared_ptr<SpicyEventInfo> ev, int nr, const std::string& expr);

    /**
     * Create a HILTI prototype for the Spicy generated by
     * CreateSpicyItemAccessor().
     */
    std::shared_ptr<::hilti::declaration::Function> DeclareHiltiExpressionAccessor(
        std::shared_ptr<SpicyEventInfo> ev, int nr, std::shared_ptr<::hilti::Type> rtype);

    /**
     * Creates the HILTI raise() for an event.
     *
     * @param event The event to create the code for.
     *
     * @return True if successful.
     */
    bool CreateHiltiEventFunction(SpicyEventInfo* ev);

    /**
     * XXX
     */
    bool CreateHiltiEventFunctionBodyForBro(SpicyEventInfo* ev);

    /**
     * XXX
     */
    bool CreateHiltiEventFunctionBodyForHilti(SpicyEventInfo* ev);

    /**
     * XXX
     */
    void AddHiltiTypesForEvent(std::shared_ptr<SpicyEventInfo> ev);

    /**
     * XXX
     */
    void AddHiltiTypesForModule(std::shared_ptr<SpicyModuleInfo> minfo);

    /**
     * Adds information from Spicy+s spicy_parsers() list to our
     * analyzer data structures.
     */
    void ExtractParsers(__hlt_list* parsers);

    /**
     * JIT's and executes the final linked module.
     *
     * @param llvm_module The code to jit and run.
     */
    bool RunJIT(std::unique_ptr<llvm::Module> llvm_module);

    /**
     * XXX
     */
    bool CompileHiltiModule(std::shared_ptr<::hilti::Module> m);

    /**
     * XXX
     */
    void EventSignatureMismatch(const string& name, const ::BroType* have, const ::BroType* want,
                                int arg);

private:
    void InitMembers();
    ::Val* RuntimeCallFunctionInternal(const ::Func* func, val_list* args);
    void InstallTypeMappings(std::shared_ptr<compiler::ModuleBuilder> mbuilder, ::BroType* t1,
                             ::BroType* t2);

    bool pre_scripts_init_run;
    bool post_scripts_init_run;

    // We pimpl to avoid having to declare the internal types here.
    struct PIMPL;
    PIMPL* pimpl;
};
}
}

#endif

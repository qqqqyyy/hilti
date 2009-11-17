/* $Id$
 * 
 * Exception handling functions. 
 * 
 * 
 * Todo: Organize exceptions in a hierarchy.
 * 
 */

#include <string.h>
#include <stdio.h>

#include "hilti.h"

extern const hlt_type_info hlt_type_info_int_32;
extern const hlt_type_info hlt_type_info_string;

// The mother of all exceptions.
hlt_exception_type hlt_exception_unspecified = { "Unspecified", 0, 0 };

hlt_exception_type hlt_exception_division_by_zero = { "DivisionByZero", &hlt_exception_unspecified, 0 };
hlt_exception_type hlt_exception_value_error = { "ValueError", &hlt_exception_unspecified, 0 };
hlt_exception_type hlt_exception_out_of_memory = { "OutOfMemory", 0, 0 };
hlt_exception_type hlt_exception_wrong_arguments = { "WrongArguments", &hlt_exception_unspecified, 0 };
hlt_exception_type hlt_exception_undefined_value = { "UndefinedValue", &hlt_exception_unspecified, 0 };
hlt_exception_type hlt_channel_full = { "ChannelFull", &hlt_exception_unspecified, 0 };
hlt_exception_type hlt_channel_empty = { "ChannelEmpty", &hlt_exception_unspecified, 0 };
hlt_exception_type hlt_exception_decoding_error = { "DecodingError", &hlt_exception_unspecified, 0 };
hlt_exception_type hlt_exception_worker_thread_threw_exception = { "WorkerThreadThrewException", &hlt_exception_unspecified, 0 };
hlt_exception_type hlt_exception_internal_error = { "InternalError", &hlt_exception_unspecified, &hlt_type_info_string };
hlt_exception_type hlt_exception_os_error = { "OSError", &hlt_exception_unspecified, 0 };
hlt_exception_type hlt_exception_overlay_not_attached = { "OverlayNotAttached", &hlt_exception_unspecified, 0 };
hlt_exception_type hlt_exception_index_error = { "IndexError", &hlt_exception_unspecified, 0 };
hlt_exception_type hlt_exception_underflow = { "Underflow", &hlt_exception_unspecified, 0 };
hlt_exception_type hlt_exception_invalid_iterator = { "InvalidIterator", &hlt_exception_unspecified, 0 };
hlt_exception_type hlt_exception_not_implemented = { "NotImplemented", &hlt_exception_unspecified, 0 };
hlt_exception_type hlt_exception_pattern_error = { "PatternError", &hlt_exception_unspecified, 0 };
hlt_exception_type hlt_exception_assertion_error = { "AssertionError", &hlt_exception_unspecified, &hlt_type_info_string };

hlt_exception_type hlt_exception_resumable = { "Resumable", &hlt_exception_unspecified, 0 };
hlt_exception_type hlt_exception_yield = { "Yield", &hlt_exception_resumable, &hlt_type_info_int_32 };

hlt_exception* __hlt_exception_new(hlt_exception_type* type, void* arg, const char* location)
{
    hlt_exception* excpt = hlt_gc_malloc_non_atomic(sizeof(hlt_exception));
    excpt->type = type;
    excpt->cont = 0;
    excpt->arg = arg;
    excpt->location = location;
    excpt->frame = 0;
    return excpt;
}

hlt_exception* __hlt_exception_new_yield(hlt_continuation* cont, int32_t arg, const char* location)
{
    int32_t *arg_copy = hlt_gc_malloc_atomic(sizeof(int32_t));
    *arg_copy = arg;
    hlt_exception *excpt = __hlt_exception_new(&hlt_exception_yield, arg_copy, location);
    excpt->cont = cont;
    return excpt;
}

void __hlt_set_exception(hlt_exception** dst, hlt_exception_type* type, void* arg, const char* location)
{
    assert(dst);
    *dst = __hlt_exception_new(type, arg, location);
}

static void __exception_print(const char* prefix, hlt_exception* exception)
{
    hlt_exception* excpt = 0;
    
    fprintf(stderr, "%s%s", prefix, exception->type->name);
    
    if ( exception->arg ) {
        hlt_string arg = hlt_string_from_object(exception->type->argtype, &exception->arg, &excpt);
        fprintf(stderr, " with argument ");
        __hlt_string_print(stderr, arg, 0, &excpt);
    }

    if ( exception->cont )
        fprintf(stderr, ", resumable");
    
    if ( exception->location )
        fprintf(stderr, " (from %s)", exception->location);
    
    fprintf(stderr, "\n");
}

void hlt_exception_print(hlt_exception* exception) 
{
    __exception_print("", exception);
}

void hlt_exception_print_uncaught(hlt_exception* exception) 
{
    __exception_print("hilti: uncaught exception, ", exception);
}

void __hlt_exception_print_uncaught_abort(hlt_exception* exception) 
{
    __exception_print("hilti: uncaught exception, ", exception);
    abort();
}

void __hlt_exception_save_frame(hlt_exception* excpt, void* frame)
{
    excpt->frame = frame;
}

void* __hlt_exception_restore_frame(hlt_exception* excpt)
{
    return excpt->frame;
}

hlt_continuation* __hlt_exception_get_continuation(hlt_exception* excpt)
{
    return excpt->cont;
}

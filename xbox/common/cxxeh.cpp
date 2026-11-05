// Minimal C++ exception runtime (MSVC x86 ABI) for nxdk.
//
// nxdk only ships SEH (_except_handler3). clang with -fexceptions for
// i386-pc-win32 emits FuncInfo tables and references _CxxThrowException and
// __CxxFrameHandler3, which are implemented here on top of the Xbox kernel's
// RtlRaiseException/RtlUnwind.
//
// Known limitations (acceptable for OpenMoHAA, which is single-threaded on Xbox):
//  - the "current exception" state (for `throw;`) is global, not per thread
//  - if a new exception escapes a catch block, the original object is not destroyed
//  - exception specifications are not checked and /EHs vs /EHa is not distinguished
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <xboxkrnl/xboxkrnl.h>

#ifdef CXXEH_TRACE
#include <hal/debug.h>
#define TRACE(...) debugPrint(__VA_ARGS__)
#else
#define TRACE(...) ((void)0)
#endif

namespace {

constexpr DWORD CXX_EXCEPTION = 0xE06D7363; // 'msc' | 0xE0000000
constexpr DWORD CXX_FRAME_MAGIC_VC6 = 0x19930520;
constexpr DWORD CXX_FRAME_MAGIC_VC8 = 0x19930522;

constexpr DWORD DISPOSITION_CONTINUE_SEARCH = 1;

// HandlerType::adjectives
constexpr DWORD HT_IsConst = 0x01;
constexpr DWORD HT_IsVolatile = 0x02;
constexpr DWORD HT_IsReference = 0x08;

// CatchableType::properties
constexpr DWORD CT_IsSimpleType = 0x01;
constexpr DWORD CT_ByReferenceOnly = 0x02;
constexpr DWORD CT_HasVirtualBase = 0x04;

// ThrowInfo::attributes
constexpr DWORD TI_IsConst = 0x01;
constexpr DWORD TI_IsVolatile = 0x02;

struct TypeDescriptor {
    const void *vftable;
    void *spare;
    char name[1];
};

struct PMD {
    int mdisp; // `this` displacement
    int pdisp; // vbtable pointer displacement (-1 if there is no virtual base)
    int vdisp; // displacement inside the vbtable
};

typedef void(__thiscall *CopyCtor)(void *self, const void *src);
typedef void(__thiscall *CopyCtorVB)(void *self, const void *src, int mostDerived);
typedef void(__thiscall *Dtor)(void *self);

struct CatchableType {
    DWORD properties;
    const TypeDescriptor *type;
    PMD thisDisplacement;
    int sizeOrOffset;
    void *copyFunction;
};

struct CatchableTypeArray {
    int count;
    const CatchableType *types[1];
};

struct ThrowInfo {
    DWORD attributes;
    Dtor unwind;
    void *forwardCompat;
    const CatchableTypeArray *catchableTypes;
};

struct UnwindMapEntry {
    int toState;
    void *action;
};

struct HandlerType {
    DWORD adjectives;
    const TypeDescriptor *type;
    int dispCatchObj;
    void *handler;
};

struct TryBlockMapEntry {
    int tryLow;
    int tryHigh;
    int catchHigh;
    int nCatches;
    const HandlerType *handlers;
};

struct FuncInfo {
    DWORD magic : 29;
    DWORD bbtFlags : 3;
    int maxState;
    const UnwindMapEntry *unwindMap;
    DWORD nTryBlocks;
    const TryBlockMapEntry *tryBlockMap;
    DWORD nIPMapEntries;
    void *ipToStateMap;
    // VC7+ / VC8+:
    void *esTypeList;
    int ehFlags;
};

// Registration node that clang/MSVC place right below EBP:
//   [ebp-16] savedEsp, [ebp-12] next, [ebp-8] handler, [ebp-4] state
// The dispatcher hands us a pointer to `next`.
struct CxxRegistration {
    CxxRegistration *next;
    void *handler;
    int state;
};

inline DWORD savedEsp(CxxRegistration *frame) { return reinterpret_cast<DWORD *>(frame)[-1]; }
inline void *frameEbp(CxxRegistration *frame) { return reinterpret_cast<char *>(frame) + 12; }

// Exception currently being handled inside a catch block (for `throw;`).
void *g_currentObject;
const ThrowInfo *g_currentInfo;

// Calls a funclet (unwind or catch) with EBP pointing at the parent frame.
// Funclets may clobber EBX/ESI/EDI, so they are preserved.
void *callFunclet(void *func, void *ebp)
{
    void *result;
    asm volatile("pushl %%ebx\n\t"
                 "pushl %%esi\n\t"
                 "pushl %%edi\n\t"
                 "pushl %%ebp\n\t"
                 "movl %%ecx, %%ebp\n\t"
                 "call *%%eax\n\t"
                 "popl %%ebp\n\t"
                 "popl %%edi\n\t"
                 "popl %%esi\n\t"
                 "popl %%ebx\n\t"
                 : "=a"(result)
                 : "a"(func), "c"(ebp)
                 : "edx", "memory", "cc");
    return result;
}

// RtlUnwind does not preserve EBX/ESI/EDI/EBP (same as on NT, where MSVC wraps
// it in _global_unwind2), so they are saved by hand.
void globalUnwind(void *frame, EXCEPTION_RECORD *rec)
{
    asm volatile("pushl %%ebx\n\t"
                 "pushl %%esi\n\t"
                 "pushl %%edi\n\t"
                 "pushl %%ebp\n\t"
                 "pushl $0\n\t"   // ReturnValue
                 "pushl %%edx\n\t" // ExceptionRecord
                 "pushl $0\n\t"   // TargetIp
                 "pushl %%ecx\n\t" // TargetFrame
                 "call *%%eax\n\t" // stdcall: pops its own arguments
                 "popl %%ebp\n\t"
                 "popl %%edi\n\t"
                 "popl %%esi\n\t"
                 "popl %%ebx\n\t"
                 :
                 : "a"(&RtlUnwind), "c"(frame), "d"(rec)
                 : "memory", "cc");
}

[[noreturn]] void continueAfterCatch(DWORD esp, void *ebp, void *target)
{
    // Fixed registers: with "r" the compiler could pick EBP/ESP for an operand
    // and overwrite it before the jump.
    asm volatile("movl %%edx, %%esp\n\t"
                 "movl %%ecx, %%ebp\n\t"
                 "jmp *%%eax\n\t"
                 :
                 : "d"(esp), "c"(ebp), "a"(target)
                 : "memory");
    __builtin_unreachable();
}

// Runs the unwind actions from the current state down to `target`.
void localUnwind(CxxRegistration *frame, const FuncInfo *info, int target)
{
    int state = frame->state;
    while (state != target && state >= 0 && state < info->maxState) {
        const UnwindMapEntry &e = info->unwindMap[state];
        state = e.toState;
        frame->state = state;
        if (e.action)
            callFunclet(e.action, frameEbp(frame));
    }
    frame->state = state;
}

void *adjustThis(const PMD &pmd, void *obj)
{
    if (!obj)
        return nullptr;
    char *p = static_cast<char *>(obj);
    if (pmd.pdisp >= 0) {
        const int *vbtable = *reinterpret_cast<int **>(p + pmd.pdisp);
        p += pmd.pdisp + vbtable[pmd.vdisp / sizeof(int)];
    }
    return p + pmd.mdisp;
}

bool typeMatches(const HandlerType &h, const CatchableType &ct, const ThrowInfo *ti)
{
    if (!h.type || !h.type->name[0])
        return true; // catch (...)
    if (h.type != ct.type && strcmp(h.type->name, ct.type->name) != 0)
        return false;
    if ((ct.properties & CT_ByReferenceOnly) && !(h.adjectives & HT_IsReference))
        return false;
    if ((ti->attributes & TI_IsConst) && !(h.adjectives & HT_IsConst))
        return false;
    if ((ti->attributes & TI_IsVolatile) && !(h.adjectives & HT_IsVolatile))
        return false;
    return true;
}

void copyCatchObject(const HandlerType &h, const CatchableType *ct, void *obj, CxxRegistration *frame)
{
    if (!h.type || !h.type->name[0] || !h.dispCatchObj || !ct)
        return;
    void *dest = static_cast<char *>(frameEbp(frame)) + h.dispCatchObj;

    if (h.adjectives & HT_IsReference) {
        *static_cast<void **>(dest) = adjustThis(ct->thisDisplacement, obj);
    } else if (ct->properties & CT_IsSimpleType) {
        memmove(dest, obj, ct->sizeOrOffset);
        if (ct->sizeOrOffset == sizeof(void *)) // pointer: adjust to the base
            *static_cast<void **>(dest) = adjustThis(ct->thisDisplacement, *static_cast<void **>(dest));
    } else {
        void *src = adjustThis(ct->thisDisplacement, obj);
        if (!ct->copyFunction)
            memmove(dest, src, ct->sizeOrOffset);
        else if (ct->properties & CT_HasVirtualBase)
            reinterpret_cast<CopyCtorVB>(ct->copyFunction)(dest, src, 1);
        else
            reinterpret_cast<CopyCtor>(ct->copyFunction)(dest, src);
    }
}

[[noreturn]] void callCatch(EXCEPTION_RECORD *rec, CxxRegistration *frame, const FuncInfo *info,
                            const TryBlockMapEntry &tb, const HandlerType &h, const CatchableType *ct)
{
    void *obj = reinterpret_cast<void *>(rec->ExceptionInformation[1]);
    const ThrowInfo *ti = reinterpret_cast<const ThrowInfo *>(rec->ExceptionInformation[2]);

    TRACE("  eh: catch try=[%d,%d] handler=%x esp=%x\n", tb.tryLow, tb.tryHigh,
          (unsigned)h.handler, (unsigned)savedEsp(frame));
    copyCatchObject(h, ct, obj, frame);

    // Unwind the SEH frames above ours (calling their handlers with
    // EXCEPTION_UNWINDING), then this frame's local destructors.
    globalUnwind(frame, rec);
    TRACE("  eh: RtlUnwind ok\n");
    localUnwind(frame, info, tb.tryLow);
    TRACE("  eh: localUnwind ok\n");
    frame->state = tb.tryHigh + 1;

    void *prevObj = g_currentObject;
    const ThrowInfo *prevInfo = g_currentInfo;
    g_currentObject = obj;
    g_currentInfo = ti;

    // clang's catch funclets store their own ESP in the savedEsp slot (so that
    // nested exceptions inside the catch can be caught), so it has to be read
    // before calling the funclet and restored afterwards.
    DWORD esp = savedEsp(frame);
    void *cont = callFunclet(h.handler, frameEbp(frame));
    reinterpret_cast<DWORD *>(frame)[-1] = esp;
    TRACE("  eh: funclet ok cont=%x\n", (unsigned)cont);

    g_currentObject = prevObj;
    g_currentInfo = prevInfo;

    if (ti && ti->unwind && obj)
        ti->unwind(obj);

    continueAfterCatch(esp, frameEbp(frame), cont);
}

void findHandler(EXCEPTION_RECORD *rec, CxxRegistration *frame, const FuncInfo *info)
{
    const ThrowInfo *ti = reinterpret_cast<const ThrowInfo *>(rec->ExceptionInformation[2]);
    if (!ti)
        return;
    int state = frame->state;

    for (DWORD i = 0; i < info->nTryBlocks; i++) {
        const TryBlockMapEntry &tb = info->tryBlockMap[i];
        if (state < tb.tryLow || state > tb.tryHigh)
            continue;

        for (int j = 0; j < tb.nCatches; j++) {
            const HandlerType &h = tb.handlers[j];
            if (!h.type || !h.type->name[0])
                callCatch(rec, frame, info, tb, h, nullptr);

            const CatchableTypeArray *cta = ti->catchableTypes;
            for (int k = 0; k < cta->count; k++) {
                const CatchableType *ct = cta->types[k];
                if (typeMatches(h, *ct, ti))
                    callCatch(rec, frame, info, tb, h, ct);
            }
        }
    }
}

} // namespace

extern "C" DWORD __cdecl cxxFrameHandlerImpl(const FuncInfo *info, EXCEPTION_RECORD *rec,
                                             CxxRegistration *frame, void * /*context*/,
                                             void * /*dispatcher*/)
{
    DWORD magic = info->magic;
    TRACE("  eh: code=%x at=%x fl=%x fr=%x st=%d\n", (unsigned)rec->ExceptionCode,
          (unsigned)rec->ExceptionAddress, (unsigned)rec->ExceptionFlags, (unsigned)frame,
          frame->state);
    if (magic < CXX_FRAME_MAGIC_VC6 || magic > CXX_FRAME_MAGIC_VC8)
        return DISPOSITION_CONTINUE_SEARCH;

    if (rec->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) {
        if (info->maxState > 0)
            localUnwind(frame, info, -1);
        return DISPOSITION_CONTINUE_SEARCH;
    }

    if (info->nTryBlocks == 0)
        return DISPOSITION_CONTINUE_SEARCH;

    if (rec->ExceptionCode == CXX_EXCEPTION && rec->NumberParameters >= 3 &&
        rec->ExceptionInformation[0] >= CXX_FRAME_MAGIC_VC6 &&
        rec->ExceptionInformation[0] <= CXX_FRAME_MAGIC_VC8)
        findHandler(rec, frame, info); // does not return if a catch matches

    return DISPOSITION_CONTINUE_SEARCH;
}

// The __ehhandler$<func> thunk does `mov eax, FuncInfo; jmp ___CxxFrameHandler3`.
asm(".text\n"
    ".globl ___CxxFrameHandler3\n"
    "___CxxFrameHandler3:\n"
    "    pushl 16(%esp)\n" // dispatcher
    "    pushl 16(%esp)\n" // context
    "    pushl 16(%esp)\n" // frame
    "    pushl 16(%esp)\n" // record
    "    pushl %eax\n"     // FuncInfo
    "    call _cxxFrameHandlerImpl\n"
    "    addl $20, %esp\n"
    "    ret\n");

// Called when an exception leaves a noexcept function.
extern "C" [[noreturn]] void __cdecl __std_terminate()
{
    abort();
    __builtin_unreachable();
}

extern "C" [[noreturn]] void __stdcall _CxxThrowException(void *object, const ThrowInfo *info)
{
    if (!info) { // `throw;`
        object = g_currentObject;
        info = g_currentInfo;
    }

    EXCEPTION_RECORD rec = {};
    rec.ExceptionCode = CXX_EXCEPTION;
    rec.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    rec.ExceptionAddress = __builtin_return_address(0);
    rec.NumberParameters = 3;
    rec.ExceptionInformation[0] = CXX_FRAME_MAGIC_VC6;
    rec.ExceptionInformation[1] = reinterpret_cast<ULONG_PTR>(object);
    rec.ExceptionInformation[2] = reinterpret_cast<ULONG_PTR>(info);
    TRACE("  eh: throw obj=%x ti=%x\n", (unsigned)object, (unsigned)info);
    RtlRaiseException(&rec);
    __builtin_unreachable();
}

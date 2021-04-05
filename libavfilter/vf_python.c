/*
 * Copyright (c) 2021 Dendygeeks
 */

/**
 * @file
 * Python filter
 */

#if defined(__CYGWIN__) || defined(__MINGW32__)
#define WINLIN
#endif

#if defined(_WIN32) || defined(WINLIN)
#include <windows.h>
#include <string.h>
#endif

#if defined(__CYGWIN__) || defined(__linux__)
#include <dlfcn.h>
#include <unistd.h>
#elif !defined(_WIN32) && !defined(WINLIN)
#error Unsupported platform
#endif

#include <signal.h>

#include "libavutil/pixdesc.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#include <stdio.h>
#include <stdlib.h>

#include <compat/w32dlfcn.h>

struct lib_handle_t;
typedef struct lib_handle_t {
    void* handle;
    char* libpath;
    void* (*get_sym)(struct lib_handle_t* self, const char* name);
    void (*close)(struct lib_handle_t* self);
} lib_handle_t;

#if defined(_WIN32) || defined(WINLIN)
static void* win32_getsym(struct lib_handle_t* self, const char* name) {
    return GetProcAddress((HANDLE)self->handle, name);
}
static void win32_closelib(struct lib_handle_t* self) {
    FreeLibrary((HANDLE)self->handle);
    self->handle = NULL;
    free(self->libpath);
}
#endif
#if defined(__linux__) || defined(WINLIN)
static void* libdl_getsym(struct lib_handle_t* self, const char* name) {
    return dlsym(self->handle, name);
}
static void libdl_closelib(struct lib_handle_t* self) {
    dlclose(self->handle);
    self->handle = NULL;
    free(self->libpath);
}
#endif

static int open_lib(const char* libname, lib_handle_t* out) {
    char *libpath = strdup(libname);
    if (libpath == NULL) {
        return 3;
    }
    if (!out) return 1;
    {
#if defined(WINLIN) || defined(_WIN32)
        HANDLE hLib = LoadLibraryA(libname);
        if (hLib == NULL) {
#ifdef WINLIN
            // try dlopen if LoadLibraryA failed
            void* lib = dlopen(libname, RTLD_LAZY);
            if (lib == NULL) {
                free(libpath);
                return 2;
            }
            out->handle = lib;
            out->get_sym = libdl_getsym;
            out->close = libdl_closelib;
            out->libpath = libpath;
            return 0;
#else
            free(libpath);
            return 2;
#endif
        }
        out->handle = (void*)hLib;
        out->get_sym = win32_getsym;
        out->close = win32_closelib;
        out->libpath = libpath;
        return 0;
#elif defined(__linux__)
        void* lib = dlopen(libname, RTLD_LAZY);
        if (lib == NULL) {
            free(libpath);
            return 2;
        }
        out->handle = lib;
        out->get_sym = libdl_getsym;
        out->close = libdl_closelib;
        out->libpath = libpath;
        return 0;
#else
#error Unsupported platform
#endif
    }
}

static char* get_ffmpeg_exe(void) {
#if defined(_WIN32) || defined(WINLIN)
    char buf[MAX_PATH + 1];
    memset(buf, 0, sizeof(buf));
    if ((GetModuleFileNameA(NULL, buf, sizeof(buf) - 1) > 0) && GetLastError() == ERROR_SUCCESS) {
        return strdup(buf);
    }
    return NULL;
#elif defined(__linux__)
    struct stat st;
    char *buf;
    if (lstat("/proc/self/exe", &st) != 0) {
        return NULL;
    }
    buf = calloc(st.st_size + 1, sizeof(char));
    if (buf == NULL) {
        return NULL;
    }
    if (readlink("/proc/self/exe", buf, st.st_size) != st.st_size) {
        free(buf);
        return NULL;
    }
    return buf;
#endif
}

typedef struct PyObject_ PyObject;
typedef struct PyCompilerFlags_ PyCompilerFlags;
typedef size_t Py_ssize_t;
typedef struct PyThreadState_ PyThreadState;
typedef void (*PyOS_sighandler_t)(int);

typedef struct py_embed {
    lib_handle_t hlib;
    char* ffmpeg_exe;

    // Python C API loaded dynamically
    void (*Py_InitializeEx)(int initsigs);
    int (*Py_FinalizeEx)(void);
    PyObject* (*Py_CompileStringObject)(const char *str, PyObject *filename, int start, PyCompilerFlags *flags, int optimize);
    PyObject* (*PyUnicode_DecodeFSDefault)(const char *s);
    PyObject* (*PyEval_EvalCode)(PyObject *co, PyObject *globals, PyObject *locals);
    PyObject* (*PyDict_New)(void);
    PyObject* (*PyDict_GetItemString)(PyObject *p, const char *key);
    PyObject* (*PyTuple_New)(Py_ssize_t len);
    int (*PyTuple_SetItem)(PyObject *p, Py_ssize_t pos, PyObject *o);
    PyObject* (*PyLong_FromLongLong)(long long v);
    PyObject* (*PyObject_CallObject)(PyObject *callable, PyObject *args);
    void (*Py_DecRef)(PyObject *o);
    PyObject* (*PyImport_ImportModule)(const char *name);
    PyObject* (*PyObject_GetAttrString)(PyObject *o, const char *attr_name);
    long (*PyLong_AsLong)(PyObject *obj);
    void (*PyErr_Print)(void);
    PyObject* (*PyDict_Copy)(PyObject *p);
    PyObject* (*PyEval_GetBuiltins)(void);
    PyObject* (*PyImport_ExecCodeModuleEx)(const char *name, PyObject *co, const char *pathname);

    wchar_t* (*Py_GetPath)(void);
    char* (*Py_EncodeLocale)(const wchar_t *text, size_t *error_pos);
    wchar_t* (*Py_GetPythonHome)(void);
    wchar_t* (*Py_GetProgramName)(void);
    void (*Py_SetProgramName)(const wchar_t *name);
    wchar_t* (*Py_DecodeLocale)(const char* arg, size_t *size);
    wchar_t* (*Py_GetExecPrefix)(void);
    void (*Py_SetPath)(const wchar_t *);
    void (*Py_SetPythonHome)(const wchar_t *home);
    void (*PySys_SetPath)(const wchar_t *path);

    PyThreadState* (*PyEval_SaveThread)(void);
    void (*PyEval_RestoreThread)(PyThreadState *tstate);
    void (*PyMem_RawFree)(void *p);
    void* (*PyMem_RawMalloc)(size_t n);
    void* (*PyMem_RawCalloc)(size_t nelem, size_t elsize);
    PyObject* (*PyMemoryView_FromMemory)(char *mem, Py_ssize_t size, int flags);
    int (*PySys_SetObject)(const char *name, PyObject *v);
    PyObject* (*PyErr_Occurred)(void);
    int (*PyErr_GivenExceptionMatches)(PyObject *given, PyObject *exc);

    int (*PySequence_Check)(PyObject *o);
    Py_ssize_t (*PySequence_Length)(PyObject *o);
    PyObject* (*PySequence_GetItem)(PyObject *o, Py_ssize_t i);
    PyObject* (*PyModule_New)(const char *name);
    int (*PyObject_SetAttrString)(PyObject *o, const char *attr_name, PyObject *v);
    int (*PyImport_AppendInittab)(const char *name, PyObject* (*initfunc)(void));
    PyObject* (*PySys_GetObject)(const char *name);
    int (*PyDict_SetItemString)(PyObject *p, const char *key, PyObject *val);
    void (*PyErr_Clear)(void);

    PyOS_sighandler_t (*PyOS_getsig)(int i);
    PyOS_sighandler_t (*PyOS_setsig)(int i, PyOS_sighandler_t h);

    // internal data for working with Python
    int file_input; // token for Python compile() telling that this is a module we're Python-compiling
    wchar_t* program_name; // what we set via Py_SetProgramName()
    wchar_t* python_home;
    PyThreadState* tstate;
    PyObject** PyExc_KeyboardInterrupt;
    PyObject** PyExc_AttributeError;
    PyOS_sighandler_t py_sigint, core_sigint;
    PyOS_sighandler_t py_sigterm, core_sigterm;
} py_embed_t;

static py_embed_t s_py = { NULL };

static int fill_pyembed(lib_handle_t hpylib) {
    if (hpylib.handle == NULL) return 1;
    if (s_py.hlib.handle != NULL) return 2;
    memset(&s_py, 0, sizeof(s_py));
    if ((s_py.ffmpeg_exe = get_ffmpeg_exe()) == NULL) return 3;
    s_py.hlib = hpylib;

#define GET_SYMBOL(name)                        \
    s_py.name = hpylib.get_sym(&hpylib, #name); \
    if (s_py.name == NULL) return 4;

    GET_SYMBOL(Py_InitializeEx);
    GET_SYMBOL(Py_FinalizeEx);
    GET_SYMBOL(Py_CompileStringObject);

    GET_SYMBOL(PyUnicode_DecodeFSDefault);
    GET_SYMBOL(PyEval_EvalCode);
    GET_SYMBOL(PyDict_New);
    GET_SYMBOL(PyDict_GetItemString);
    GET_SYMBOL(PyTuple_New);
    GET_SYMBOL(PyTuple_SetItem);
    GET_SYMBOL(PyLong_FromLongLong);
    GET_SYMBOL(PyObject_CallObject);
    GET_SYMBOL(Py_DecRef);
    GET_SYMBOL(PyImport_ImportModule);
    GET_SYMBOL(PyObject_GetAttrString);
    GET_SYMBOL(PyLong_AsLong);
    GET_SYMBOL(PyErr_Print);
    GET_SYMBOL(PyDict_Copy);
    GET_SYMBOL(PyEval_GetBuiltins);
    GET_SYMBOL(PyImport_ExecCodeModuleEx);

    GET_SYMBOL(Py_GetPath);
    GET_SYMBOL(Py_EncodeLocale);
    GET_SYMBOL(Py_GetPythonHome);
    GET_SYMBOL(Py_GetProgramName);
    GET_SYMBOL(Py_SetProgramName);
    GET_SYMBOL(Py_DecodeLocale);
    GET_SYMBOL(Py_GetExecPrefix);
    GET_SYMBOL(Py_SetPath);
    GET_SYMBOL(Py_SetPythonHome);
    GET_SYMBOL(PySys_SetPath);

    GET_SYMBOL(PyEval_SaveThread);
    GET_SYMBOL(PyEval_RestoreThread);
    GET_SYMBOL(PyMem_RawFree);
    GET_SYMBOL(PyMem_RawMalloc);
    GET_SYMBOL(PyMem_RawCalloc);
    GET_SYMBOL(PyMemoryView_FromMemory);
    GET_SYMBOL(PySys_SetObject);
    GET_SYMBOL(PyErr_Occurred);
    GET_SYMBOL(PyErr_GivenExceptionMatches);

    GET_SYMBOL(PySequence_Check);
    GET_SYMBOL(PySequence_Length);
    GET_SYMBOL(PySequence_GetItem);
    GET_SYMBOL(PyModule_New);
    GET_SYMBOL(PyObject_SetAttrString);
    GET_SYMBOL(PyImport_AppendInittab);
    GET_SYMBOL(PySys_GetObject);
    GET_SYMBOL(PyDict_SetItemString);
    GET_SYMBOL(PyErr_Clear);

    GET_SYMBOL(PyOS_getsig);
    GET_SYMBOL(PyOS_setsig);

    GET_SYMBOL(PyExc_KeyboardInterrupt);
    GET_SYMBOL(PyExc_AttributeError);

#undef GET_SYMBOL
    return 0;
}

#define ACQUIRE_PYGIL() {                       \
    s_py.PyEval_RestoreThread(s_py.tstate);     \
    s_py.PyOS_setsig(SIGINT, s_py.py_sigint);   \
    s_py.PyOS_setsig(SIGTERM, s_py.py_sigterm); \
}
#define RELEASE_PYGIL() {                           \
    s_py.py_sigint = s_py.PyOS_getsig(SIGINT);      \
    s_py.py_sigterm = s_py.PyOS_getsig(SIGTERM);    \
                                                    \
    s_py.PyOS_setsig(SIGINT, s_py.core_sigint);     \
    s_py.PyOS_setsig(SIGTERM, s_py.core_sigterm);   \
                                                    \
    s_py.tstate = s_py.PyEval_SaveThread();         \
}

// stolen from Python/Include/object.h
#define PyBUF_READ  0x100
#define PyBUF_WRITE 0x200

static wchar_t* _PyMem_RawWcsdup(const wchar_t *str)
{
    size_t len = wcslen(str), size;
    wchar_t* str2;
    if (len > (size_t)MAXINT / sizeof(wchar_t) - 1) {
        return NULL;
    }

    size = (len + 1) * sizeof(wchar_t);
    str2 = s_py.PyMem_RawMalloc(size);
    if (str2 == NULL) {
        return NULL;
    }

    memcpy(str2, str, size);
    return str2;
}


static wchar_t* get_dir_name(wchar_t* file_name) {
    wchar_t* stop = file_name + wcslen(file_name) - 1;

#if defined(_WIN32) || defined(__CYGWIN__)
    for (; stop >= file_name && (*stop != L'\\' && *stop != L'/'); stop--);
#else
    for (; stop >= file_name && *stop != L'/'; stop--);
#endif
    if (stop < file_name) {
        // no slash found
        return _PyMem_RawWcsdup(L"");
    } else if (stop == file_name) {
        // case of file_name == "\python.dll"
#if defined(_WIN32) || defined(__CYGWIN__)
        return _PyMem_RawWcsdup(L"\\");
#else
        return _PyMem_RawWcsdup(L"/");
#endif
    } else {
        wchar_t* udir = _PyMem_RawWcsdup(file_name);
        udir[stop - file_name] = 0;
        return udir;
    }
}

static PyObject* _create_pyapi_module(void) {
    PyObject *mod = NULL, *pyval = NULL;
    
    if ((mod = s_py.PyModule_New("_ffmpeg")) == NULL) return NULL;

#define SET_LONGLONG_ATTR2(value, name)                                                 \
    {                                                                                   \
        if ((pyval = s_py.PyLong_FromLongLong((long long)value)) == NULL) goto error;   \
        if (s_py.PyObject_SetAttrString(mod, name, pyval) != 0) goto error;             \
    }
#define SET_LONGLONG_ATTR(name) SET_LONGLONG_ATTR2(name, #name)

    SET_LONGLONG_ATTR(LIBAVUTIL_VERSION_MAJOR);
    SET_LONGLONG_ATTR(AV_PIX_FMT_NB);
    SET_LONGLONG_ATTR2(&av_pix_fmt_desc_next, "av_pix_fmt_desc_next");
    SET_LONGLONG_ATTR2(&av_pix_fmt_desc_get_id, "av_pix_fmt_desc_get_id");

#undef SET_LONGLONG_ATTR
#undef SET_LONGLONG_ATTR2

    return mod;
error:
    s_py.Py_DecRef(mod);
    s_py.Py_DecRef(pyval);
    return NULL;
}

static int init_embed_python(const char* pylib) {
    // set program name so on *nix Python can find its prefix from it
    size_t main_sz;
    s_py.tstate = NULL;
    s_py.program_name = s_py.Py_DecodeLocale(pylib, &main_sz);
    if (s_py.program_name == NULL) {
        fprintf(stderr, "cannot decode '%s' into wchar_t: %d\n", pylib, (int)main_sz);
        return 1;
    }
    s_py.Py_SetProgramName(s_py.program_name);

    if (s_py.Py_GetPythonHome() == NULL) {
        // flush path config cache, otherwise our manipulations with Python home won't be re-read by it
        s_py.Py_SetPath(NULL);
#if defined(_WIN32) || defined(__CYGWIN__)
        s_py.python_home = get_dir_name(s_py.program_name);
        if (s_py.python_home == NULL) {
            return 2;
        }
        s_py.Py_SetPythonHome(s_py.python_home);
#else
#error Implement me properly if needed
#endif
    }
    //if (s_py.PyImport_AppendInittab("_ffmpeg", _create_pyapi_module) != 0) return ENOMEM;

#ifdef _WIN32
    {
        // DEBUG things start
        wchar_t* uhome = s_py.Py_GetPythonHome();
        wchar_t* uprog = s_py.Py_GetProgramName();
        wchar_t* upref = s_py.Py_GetExecPrefix();
        wchar_t* upath = s_py.Py_GetPath();
        printf("home = %S\nprog = %S\nexec = %S\npath = %S\n", uhome, uprog, upref, upath);
        fflush(stdout);
        // DEBUG things end
    }
#endif
    s_py.core_sigint = s_py.PyOS_getsig(SIGINT);
    s_py.core_sigterm = s_py.PyOS_getsig(SIGTERM);

    s_py.Py_InitializeEx(0 /* 0 means do not install signal handlers */);

    {
        PyObject* modSymbol = s_py.PyImport_ImportModule("symbol");
        PyObject* pyFileInput;
        long file_input;
        if (modSymbol == NULL) {
            s_py.PyErr_Print();
            RELEASE_PYGIL();
            return 1001;
        }
        pyFileInput = s_py.PyObject_GetAttrString(modSymbol, "file_input");
        s_py.Py_DecRef(modSymbol);
        if (pyFileInput == NULL) {
            s_py.PyErr_Print();
            RELEASE_PYGIL();
            return 1002;
        }
        file_input = s_py.PyLong_AsLong(pyFileInput);
        s_py.Py_DecRef(pyFileInput);
        if (file_input == -1) {
            if (s_py.PyErr_Occurred()) {
                s_py.PyErr_Print();
            }
            RELEASE_PYGIL();
            return 1003;
        }
        s_py.file_input = file_input;
    }
    {
        PyObject* sys_modules = s_py.PySys_GetObject("modules");
        PyObject* _ffmpeg_mod = _create_pyapi_module();
        if (sys_modules != NULL && _ffmpeg_mod != NULL) {
            if (s_py.PyDict_SetItemString(sys_modules, "_ffmpeg", _ffmpeg_mod) != 0) {
                s_py.Py_DecRef(_ffmpeg_mod);
                s_py.Py_DecRef(sys_modules);
                RELEASE_PYGIL();
                return 1004;
            }
        } else {
            s_py.Py_DecRef(_ffmpeg_mod);
            s_py.Py_DecRef(sys_modules);
            RELEASE_PYGIL();
            return 1004;
        }
        s_py.Py_DecRef(_ffmpeg_mod);
        s_py.Py_DecRef(sys_modules);
    }

    // release the GIL, per Python control flow
    RELEASE_PYGIL();

    return 0;
}

static int update_sys_path(const char* script_file) {
    ACQUIRE_PYGIL();
    {
        // append path to the script
        wchar_t* sys_path = s_py.Py_GetPath();
        wchar_t* uscript_file = s_py.Py_DecodeLocale(script_file, NULL);
        wchar_t *script_dir, *new_path;
#if defined(_WIN32) || defined(__CYGWIN__)
        const wchar_t* path_sep = L";";
#else
        const wchar_t* path_sep = L":";
#endif
        if (uscript_file == NULL) {
            RELEASE_PYGIL();
            return 1;
        }
        script_dir = get_dir_name(uscript_file);
        s_py.PyMem_RawFree(uscript_file);
        if (script_dir == NULL) {
            RELEASE_PYGIL();
            return 2;
        }
        new_path = s_py.PyMem_RawCalloc(wcslen(sys_path) + wcslen(path_sep) + wcslen(script_dir) + 1, sizeof(wchar_t));
        if (new_path == NULL) {
            s_py.PyMem_RawFree(script_dir);
            RELEASE_PYGIL();
            return 3;
        }
        wcscat(wcscat(wcscpy(new_path, sys_path), path_sep), script_dir);
        s_py.PySys_SetPath(new_path);
        s_py.PyMem_RawFree(script_dir);
        s_py.PyMem_RawFree(new_path);
    }

    {
        // "import site" and run "site.main()" to override weird behaviour of site.py not being imported in some cases
        // TODO: understand why site.py is not imported in some cases
        PyObject* siteModule = s_py.PyImport_ImportModule("site");
        PyObject *siteMain, *noArgs, *siteMainRes;
        if (siteModule == NULL) {
            s_py.PyErr_Print();
            RELEASE_PYGIL();
            return 1001;
        }
        siteMain = s_py.PyObject_GetAttrString(siteModule, "main");
        s_py.Py_DecRef(siteModule);
        if (siteMain == NULL) {
            s_py.PyErr_Print();
            RELEASE_PYGIL();
            return 1002;
        }
        noArgs = s_py.PyTuple_New(0);
        if (noArgs == NULL) {
            s_py.Py_DecRef(siteMain);
            s_py.PyErr_Print();
            RELEASE_PYGIL();
            return 1003;
        }
        siteMainRes = s_py.PyObject_CallObject(siteMain, noArgs);
        s_py.Py_DecRef(siteMain);
        s_py.Py_DecRef(noArgs);
        if (siteMainRes == NULL) {
            s_py.PyErr_Print();
            RELEASE_PYGIL();
            return 1004;
        }
        s_py.Py_DecRef(siteMainRes);
    }

    RELEASE_PYGIL();
    return 0;
}

static int get_user_module(const char* script_file, PyObject** result) {
    FILE* fp;
    long long size, read;
    char* buf;
   
    if (script_file == NULL || result == NULL) return 1;

    fp = fopen(script_file, "rb");
    if (fp == NULL) return 2;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        //printf("cannot get size of '%s'\n", script_file);
        return 3;
    }
    size = ftell(fp);
    buf = calloc(size+1, 1);
    if (buf == NULL) {
        fclose(fp);
        return 4;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        free(buf);
        return 5;
    }
    read = fread(buf, 1, size, fp);
    if (read != size) {
        fclose(fp);
        free(buf);
        //printf("expected %d but read %d bytes\n", size, read);
        return 6;
    }
    fclose(fp);

    {
        PyObject *code, *res;
        PyObject* pName = s_py.PyUnicode_DecodeFSDefault(script_file);
        if (pName == NULL) {
            return 1001;
        }

        code = s_py.Py_CompileStringObject(buf, pName, s_py.file_input, NULL, 1);
        s_py.Py_DecRef(pName);
        if (code == NULL) {
            return 1002;
        }
        res = s_py.PyImport_ExecCodeModuleEx("__main__", code, script_file);
        s_py.Py_DecRef(code);
        if (res == NULL) {
            return 1003;
        }

        *result = res;
    }
    return 0;
}

static int init_pyfilter_class(PyObject* user_module, const char* cls_name, const char* arg, PyObject** result) {
    PyObject *tuple = NULL, *pyArg = NULL, *cls_type = NULL, *tmp;

    if ((cls_type = s_py.PyObject_GetAttrString(user_module, cls_name)) == NULL) goto error;

    if ((tuple = s_py.PyTuple_New(1)) == NULL) goto error;
    if ((pyArg = s_py.PyUnicode_DecodeFSDefault(arg)) == NULL) goto error;
    if (s_py.PyTuple_SetItem(tuple, 0, pyArg) != 0) goto error;
    pyArg = NULL; // now owned by `tuple`

    if ((tmp = s_py.PyObject_CallObject(cls_type, tuple)) == NULL) goto error;
    *result = tmp;

    s_py.Py_DecRef(tuple);
    s_py.Py_DecRef(pyArg);
    s_py.Py_DecRef(cls_type);
    return 0;
error:
    if (s_py.PyErr_Occurred()) {
        s_py.PyErr_Print();
    }
    s_py.Py_DecRef(tuple);
    s_py.Py_DecRef(pyArg);
    s_py.Py_DecRef(cls_type);
    return 1;
}

typedef struct PythonContext {
    const AVClass *class;

    char* python_library;
	char* script_filename;
	char* class_name;
	char* constructor_argument;

	PyObject* user_module;
    PyObject* filter_instance;
} PythonContext;

static int init_pycontext(PythonContext* ctx) {
    int res;
    ACQUIRE_PYGIL();

    res = get_user_module(ctx->script_filename, &(ctx->user_module));
    fprintf(stderr, "get_user_module returns %d\n", res);
    if (res != 0) {
        if (s_py.PyErr_Occurred()) {
            s_py.PyErr_Print();
        }
        RELEASE_PYGIL();
        return 2;
    }

    res = init_pyfilter_class(ctx->user_module, ctx->class_name, ctx->constructor_argument, &(ctx->filter_instance));
    if (res != 0) {
        RELEASE_PYGIL();
        return 3;
    }

    RELEASE_PYGIL();
    return 0;
}

static int uninit_pycontext(PythonContext* ctx) {
    ACQUIRE_PYGIL();

    s_py.Py_DecRef(ctx->filter_instance);
    s_py.Py_DecRef(ctx->user_module);

    RELEASE_PYGIL();
    return 0;
}

static int get_supported_formats(PyObject* filter_instance, enum AVPixelFormat** pix_fmts) {
    PyObject *method = NULL, *args = NULL, *res = NULL, *elem = NULL;
    enum AVPixelFormat* fmt = NULL;
    int result = 0;
    ACQUIRE_PYGIL();

    if ((method = s_py.PyObject_GetAttrString(filter_instance, "get_formats")) == NULL) {
        PyObject* exc = s_py.PyErr_Occurred();
        if (exc == NULL) goto error;
        if (s_py.PyErr_GivenExceptionMatches(exc, *s_py.PyExc_AttributeError)) {
            s_py.PyErr_Clear();
            // no attribute in a filter, default to rgb24
            fmt = calloc(2, sizeof(enum AVPixelFormat));
            if (fmt == NULL) {
                goto error;
            }
            fmt[0] = AV_PIX_FMT_RGB24;
            fmt[1] = AV_PIX_FMT_NONE;
            *pix_fmts = fmt;
        } else {
            goto error;
        }
    } else {
        Py_ssize_t size;
        if ((args = s_py.PyTuple_New(0)) == NULL) goto error;
        if ((res = s_py.PyObject_CallObject(method, args)) == NULL) goto error;
        if (!s_py.PySequence_Check(res)) goto error;
        if ((size = s_py.PySequence_Length(res)) == -1) goto error;
        if ((fmt = calloc(size + 1, sizeof(enum AVPixelFormat))) == NULL) goto error;
        
        for (Py_ssize_t i = 0; i < size; i++) {
            long elem_value;
            if ((elem = s_py.PySequence_GetItem(res, i)) == NULL) goto error;
            elem_value = s_py.PyLong_AsLong(elem);
            if (elem_value == -1 && s_py.PyErr_Occurred()) goto error;
            s_py.Py_DecRef(elem); // remember to decref the elem so we don't leak it on next iteration
            elem = NULL;
            if (elem_value < AV_PIX_FMT_NONE || elem_value > AV_PIX_FMT_NB) goto error;
            fmt[i] = elem_value;
        }
        // last element should be AV_PIX_FMT_NONE
        fmt[size] = AV_PIX_FMT_NONE;
        *pix_fmts = fmt;
    }

finish:
    s_py.Py_DecRef(elem);
    s_py.Py_DecRef(res);
    s_py.Py_DecRef(args);
    s_py.Py_DecRef(method);
    RELEASE_PYGIL();
    return result;
error:
    if (s_py.PyErr_Occurred()) {
        s_py.PyErr_Print();
    }
    result = 1;
    free(fmt);
    goto finish;
}

// Interface from python wrapper to the filter


static int init_python_library_and_script(const char* dllfile, const char* pyfile) {
    int res;

    if (s_py.hlib.handle == NULL) {
        // library wasn't already initialized
        lib_handle_t hlib;
        if (open_lib(dllfile, &hlib) != 0) {
            return 1;
        }

        if (fill_pyembed(hlib) != 0) {
            return 2;
        }
        if (init_embed_python(dllfile) != 0) {
            return 3;
        }

        if (update_sys_path(s_py.ffmpeg_exe) != 0) {
            return 4;
        }
    } else {
        if (strcmp(dllfile, s_py.hlib.libpath) != 0) {
            fprintf(stderr, "Python library '%s' already loaded, refusing to load '%s'", s_py.hlib.libpath, dllfile);
            fflush(stderr);
        }
    }
    res = update_sys_path(pyfile);
    if (res != 0) {
        fprintf(stderr, "sys.path update fail: %d\n", res);
        fflush(stderr);
        return 5;
    }

    return 0;
}

static int uninit_python_library_and_script(void) {
    if (s_py.tstate != NULL) {
        ACQUIRE_PYGIL();
    }
    s_py.Py_FinalizeEx();
    // TODO Check returning value

    s_py.PyMem_RawFree(s_py.program_name);
    s_py.PyMem_RawFree(s_py.python_home);
    s_py.hlib.close(&s_py.hlib);
    free(s_py.ffmpeg_exe);
    // TODO Check error??

    return 0;
}

typedef uint8_t ubool;

static inline ubool _pack_int(const int value, const int pos, PyObject* tup) {
    PyObject* boxed = s_py.PyLong_FromLongLong(value);
    if (boxed == NULL) return 0;
    if (s_py.PyTuple_SetItem(tup, pos, boxed) != 0) {
        s_py.Py_DecRef(boxed);
        return 0;
    }
    return 1;
}

static int python_call_filter(PyObject* filter_instance, AVFrame* in, AVFrame* out) {
    PyObject *args = NULL, *tmpview = NULL, *result = NULL;
    int retval = 0;
    ACQUIRE_PYGIL();

    if ((args = s_py.PyTuple_New(2)) == NULL) goto error;
    if ((tmpview = s_py.PyMemoryView_FromMemory((char*)in, sizeof(AVFrame), PyBUF_WRITE)) == NULL) goto error;
    if (s_py.PyTuple_SetItem(args, 0, tmpview) != 0) goto error;
    if ((tmpview = s_py.PyMemoryView_FromMemory((char*)out, sizeof(AVFrame), PyBUF_WRITE)) == NULL) goto error;
    if (s_py.PyTuple_SetItem(args, 1, tmpview) != 0) goto error;
    tmpview = NULL; // owned by args

    if ((result = s_py.PyObject_CallObject(filter_instance, args)) == NULL) goto error;

finish:
    s_py.Py_DecRef(args);
    s_py.Py_DecRef(tmpview);
    s_py.Py_DecRef(result);
    RELEASE_PYGIL();
    return retval;
error:
    {
        PyObject* exc = s_py.PyErr_Occurred();
        if (exc != NULL && s_py.PyErr_GivenExceptionMatches(exc, *s_py.PyExc_KeyboardInterrupt)) {
            retval = AVERROR_EXIT;
        } else {
            retval = 1;
            if (exc != NULL) {
                s_py.PyErr_Print();
            }
        }
    }
    goto finish;
}



#define OFFSET(x) offsetof(PythonContext, x)
#define A AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption python_options[] = {
    { "pylib",   "Python runtime library (libpython3.X.so on Linux, python3.X.dll on Windows)",
    		OFFSET(python_library), AV_OPT_TYPE_STRING, {.str=""}, 0, 0, A
    },
    { "script",   "Python 3 script to call",
    		OFFSET(script_filename), AV_OPT_TYPE_STRING, {.str=""}, 0, 0, A
    },
    { "class",   "Python 3 class that implements the filter",
    		OFFSET(class_name), AV_OPT_TYPE_STRING, {.str=""}, 0, 0, A
    },
    { "init_arg",   "Constructor argument to pass to the __init__ of the class",
    		OFFSET(constructor_argument), AV_OPT_TYPE_STRING, {.str=""}, 0, 0, A
    },
    { NULL }
};

AVFILTER_DEFINE_CLASS(python);



typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int pythonCallProcess(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
	PythonContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;

    //const int slice_start = (in->height * jobnr) / nb_jobs;
    //const int slice_end = (in->height * (jobnr+1)) / nb_jobs;

    // Currently only one thread is supported here, so jobnr has to be 0 and nb_jobs should be 1
    if (s->user_module) {
        /*int pycall_res = python_call(s->user_module, s->class_name, 
            in->format, in->width, in->height,
            in->linesize, out->linesize, in->data, out->data);*/
        //int pycall_res = python_call_av(s->user_module, s->class_name, in, out);
        int pycall_res = python_call_filter(s->filter_instance, in, out);
        switch (pycall_res) {
            case 0: break;
            case AVERROR_EXIT:
                // ctrl-c was hit, re-raise
#ifdef HAVE_SETCONSOLECTRLHANDLER
                return GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0) ? 0 : AVERROR_EXTERNAL;
#else
                raise(SIGINT);
#endif
                return 0;
            default:
                fprintf(stderr, "python_call returns %d\n", pycall_res);
                fflush(stderr);
                return AVERROR_EXTERNAL;
        }

        fflush(stdout);	// Flushes the python output
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    /*
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_RGBA, AV_PIX_FMT_BGRA, AV_PIX_FMT_ARGB, AV_PIX_FMT_ABGR,
        AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGB565BE, AV_PIX_FMT_BGR565BE, AV_PIX_FMT_RGB555BE, AV_PIX_FMT_BGR555BE,
        AV_PIX_FMT_RGB565LE, AV_PIX_FMT_BGR565LE, AV_PIX_FMT_RGB555LE, AV_PIX_FMT_BGR555LE,
        AV_PIX_FMT_NONE
    };
    */
	PythonContext *s = ctx->priv;
    enum AVPixelFormat* pix_fmts;
    AVFilterFormats *fmts_list;

    if (get_supported_formats(s->filter_instance, &pix_fmts) != 0) {
        return AVERROR_EXTERNAL;
    }
    
    fmts_list = ff_make_format_list(pix_fmts);
    free(pix_fmts);
    if (!fmts_list) {
        return AVERROR(ENOMEM);
    }
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_input(AVFilterLink *inlink)
{
    //PythonContext *s = inlink->dst->priv;

    // TODO Here we should call a function that takes (w_in, h_in) and returns (w_out, h_out)

    /*s->hi_pixel_mask   = 0xFEFEFEFE;
    s->lo_pixel_mask   = 0x01010101;
    s->q_hi_pixel_mask = 0xFCFCFCFC;
    s->q_lo_pixel_mask = 0x03030303;
    s->bpp  = 4;

    switch (inlink->format) {
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_BGR24:
        s->bpp = 3;
        break;

    case AV_PIX_FMT_RGB565BE:
    case AV_PIX_FMT_BGR565BE:
        s->is_be = 1;
    case AV_PIX_FMT_RGB565LE:
    case AV_PIX_FMT_BGR565LE:
        s->hi_pixel_mask   = 0xF7DEF7DE;
        s->lo_pixel_mask   = 0x08210821;
        s->q_hi_pixel_mask = 0xE79CE79C;
        s->q_lo_pixel_mask = 0x18631863;
        s->bpp = 2;
        break;

    case AV_PIX_FMT_BGR555BE:
    case AV_PIX_FMT_RGB555BE:
        s->is_be = 1;
    case AV_PIX_FMT_BGR555LE:
    case AV_PIX_FMT_RGB555LE:
        s->hi_pixel_mask   = 0x7BDE7BDE;
        s->lo_pixel_mask   = 0x04210421;
        s->q_hi_pixel_mask = 0x739C739C;
        s->q_lo_pixel_mask = 0x0C630C63;
        s->bpp = 2;
        break;
    }*/

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterLink *inlink = outlink->src->inputs[0];

    outlink->w = inlink->w*4;
    outlink->h = inlink->h*4;

    av_log(inlink->dst, AV_LOG_VERBOSE, "fmt:%s size:%dx%d -> size:%dx%d\n",
           av_get_pix_fmt_name(inlink->format),
           inlink->w, inlink->h, outlink->w, outlink->h);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    int res;
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData td;
    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);
    out->width  = outlink->w;
    out->height = outlink->h;

    td.in = in, td.out = out;
    //fprintf(stderr, "HERE2\n"); fflush(stderr);
    res = pythonCallProcess(ctx, &td, 1, 1);
    av_frame_free(&in);
    return res == 0 ? ff_filter_frame(outlink, out) : res;
}

static const AVFilterPad python_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad python_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

static av_cold int init(AVFilterContext *ctx)
{
    int res;
    PythonContext *s = ctx->priv;

    fprintf(stderr, "python_library: %s\n", s->python_library); fflush(stderr);
    fprintf(stderr, "script_filename: %s\n", s->script_filename); fflush(stderr);
    fprintf(stderr, "class_name: %s\n", s->class_name); fflush(stderr);
    fprintf(stderr, "constructor_argument: %s\n", s->constructor_argument); fflush(stderr);

    res = init_python_library_and_script(s->python_library, s->script_filename);
    fprintf(stderr, "init_python_library_and_script returns %d\n", res);
    if (res != 0) {
        return AVERROR(EIO);
    }
    res = init_pycontext(s);
    fprintf(stderr, "init_pycontext returns %d\n", res);
    if (res != 0) {
        return AVERROR_EXTERNAL;
    }
    fflush(stderr);

    return 0;
}

static void uninit(AVFilterContext *ctx)
{
    int res;
	PythonContext *s = ctx->priv;

    res = uninit_pycontext(s);
	fprintf(stderr, "uninit_pycontext returns %d\n", res);

	res = uninit_python_library_and_script();
	fprintf(stderr, "uninit_python_library_and_script returns %d\n", res);
}


AVFilter ff_vf_python = {
    .name          = "python",
    .description   = NULL_IF_CONFIG_SMALL("Process the input using a python script."),
    .priv_size     = sizeof(PythonContext),
    .priv_class    = &python_class,
    .query_formats = query_formats,
	.init          = init,
	.uninit        = uninit,
    .inputs        = python_inputs,
    .outputs       = python_outputs,
    .flags         = 0/*AVFILTER_FLAG_SLICE_THREADS*/,
};

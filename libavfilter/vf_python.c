/*
 * Copyright (c) 2021 Dendygeeks
 */

/**
 * @file
 * Python filter
 */

#if defined(_WIN32)
#include <windows.h>
#include <string.h>
#elif defined(__CYGWIN__) || defined(__linux__)
#include <dlfcn.h>
#else
#error Unsupported platform
#endif


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

#if defined(__CYGWIN__) || defined(__MINGW32__)
#define WINLIN
#endif

struct lib_handle_t;
typedef struct lib_handle_t {
    void* handle;
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
}
#endif
#if defined(__linux__) || defined(WINLIN)
static void* libdl_getsym(struct lib_handle_t* self, const char* name) {
    return dlsym(self->handle, name);
}
static void libdl_closelib(struct lib_handle_t* self) {
    dlclose(self->handle);
    self->handle = NULL;
}
#endif

static int open_lib(const char* libname, lib_handle_t* out) {
    if (!out) return 1;
#if defined(WINLIN) || defined(_WIN32)
    {
        HANDLE hLib = LoadLibraryA(libname);
        if (hLib == NULL) {
#ifdef WINLIN
            // try dlopen if LoadLibraryA failed
            void* lib = dlopen(libname, RTLD_LAZY);
            if (lib == NULL) return 2;
            out->handle = lib;
            out->get_sym = libdl_getsym;
            out->close = libdl_closelib;
            return 0;
#else
            return 2;
#endif
        }
        out->handle = (void*)hLib;
        out->get_sym = win32_getsym;
        out->close = win32_closelib;
        return 0;
    }
#elif defined(__linux__)
    void* lib = dlopen(libname, RTLD_LAZY);
    if (lib == NULL) return 2;
    out->handle = lib;
    out->get_sym = libdl_getsym;
    out->close = libdl_closelib;
    return 0;
#else
#error Unsupported platform
#endif
}

typedef struct PyObject_ PyObject;
typedef struct PyCompilerFlags_ PyCompilerFlags;
typedef size_t Py_ssize_t;

typedef struct py_embed {
    lib_handle_t hlib;

    // Python C API loaded dynamically
    void (*Py_Initialize)();
    int (*Py_FinalizeEx)();
    PyObject* (*Py_CompileStringObject)(const char *str, PyObject *filename, int start, PyCompilerFlags *flags, int optimize);
    PyObject* (*PyUnicode_DecodeFSDefault)(const char *s);
    PyObject* (*PyEval_EvalCode)(PyObject *co, PyObject *globals, PyObject *locals);
    PyObject* (*PyDict_New)();
    PyObject* (*PyDict_GetItemString)(PyObject *p, const char *key);
    PyObject* (*PyTuple_New)(Py_ssize_t len);
    int (*PyTuple_SetItem)(PyObject *p, Py_ssize_t pos, PyObject *o);
    PyObject* (*PyLong_FromLongLong)(long long v);
    PyObject* (*PyObject_CallObject)(PyObject *callable, PyObject *args);
    void (*Py_DecRef)(PyObject *o);
    PyObject* (*PyImport_ImportModule)(const char *name);
    PyObject* (*PyObject_GetAttrString)(PyObject *o, const char *attr_name);
    long (*PyLong_AsLong)(PyObject *obj);
    void (*PyErr_Print)();
    PyObject* (*PyDict_Copy)(PyObject *p);
    PyObject* (*PyEval_GetBuiltins)();
    PyObject* (*PyImport_ExecCodeModuleEx)(const char *name, PyObject *co, const char *pathname);

    wchar_t* (*Py_GetPath)();
    char* (*Py_EncodeLocale)(const wchar_t *text, size_t *error_pos);
    wchar_t* (*Py_GetPythonHome)();
    wchar_t* (*Py_GetProgramName)();
    void (*Py_SetProgramName)(const wchar_t *name);
    wchar_t* (*Py_DecodeLocale)(const char* arg, size_t *size);
    wchar_t* (*Py_GetExecPrefix)();
    void (*Py_SetPath)(const wchar_t *);
    void (*Py_SetPythonHome)(const wchar_t *home);
    void (*PySys_SetPath)(const wchar_t *path);

    // internal data for working with Python
    int file_input; // token for Python compile() telling that this is a module we're Python-compiling
    wchar_t* program_name; // what we set via Py_SetProgramName()
    wchar_t* python_home;

    // fields for executing the filter
    PyObject* user_module;	// the user module object
} py_embed_t;

static py_embed_t s_py = {NULL};

static int fill_pyembed(lib_handle_t hpylib) {
    if (hpylib.handle == NULL) return 1;
    if (s_py.hlib.handle != NULL) return 2;
    memset(&s_py, 0, sizeof(s_py));
    s_py.hlib = hpylib;

#define GET_FUNC(name)                          \
    s_py.name = hpylib.get_sym(&hpylib, #name); \
    if (s_py.name == NULL) return 3;

    GET_FUNC(Py_Initialize);
    GET_FUNC(Py_FinalizeEx);
    GET_FUNC(Py_CompileStringObject);

    GET_FUNC(PyUnicode_DecodeFSDefault);
    GET_FUNC(PyEval_EvalCode);
    GET_FUNC(PyDict_New);
    GET_FUNC(PyDict_GetItemString);
    GET_FUNC(PyTuple_New);
    GET_FUNC(PyTuple_SetItem);
    GET_FUNC(PyLong_FromLongLong);
    GET_FUNC(PyObject_CallObject);
    GET_FUNC(Py_DecRef);
    GET_FUNC(PyImport_ImportModule);
    GET_FUNC(PyObject_GetAttrString);
    GET_FUNC(PyLong_AsLong);
    GET_FUNC(PyErr_Print);
    GET_FUNC(PyDict_Copy);
    GET_FUNC(PyEval_GetBuiltins);
    GET_FUNC(PyImport_ExecCodeModuleEx);

    GET_FUNC(Py_GetPath);
    GET_FUNC(Py_EncodeLocale);
    GET_FUNC(Py_GetPythonHome);
    GET_FUNC(Py_GetProgramName);
    GET_FUNC(Py_SetProgramName);
    GET_FUNC(Py_DecodeLocale);
    GET_FUNC(Py_GetExecPrefix);
    GET_FUNC(Py_SetPath);
    GET_FUNC(Py_SetPythonHome);
    GET_FUNC(PySys_SetPath);

#undef GET_FUNC
    return 0;
}


static wchar_t* get_dir_name(wchar_t* file_name) {
    // TODO: use PyMem-based allocators
    wchar_t* stop = file_name + wcslen(file_name) - 1;

#if defined(_WIN32) || defined(__CYGWIN__)
    for (; stop >= file_name && (*stop != L'\\' && *stop != L'/'); stop--);
#else
    for (; stop >= file_name && *stop != L'/'; stop--);
#endif
    if (stop < file_name) {
        // no slash found
        return wcsdup(L"");
    } else if (stop == file_name) {
        // case of file_name == "\python.dll"
#if defined(_WIN32) || defined(__CYGWIN__)
        return wcsdup(L"\\");
#else
        return wcsdup(L"/");
#endif
    } else {
        wchar_t* udir = wcsdup(file_name);
        udir[stop - file_name] = 0;
        return udir;
    }
}

static int init_embed_python(const char* pylib) {
    // set program name so on *nix Python can find its prefix from it
    size_t main_sz;
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

#ifdef _WIN32
    {
        // DEBUG things start
        wchar_t* uhome = s_py.Py_GetPythonHome();
        wchar_t* uprog = s_py.Py_GetProgramName();
        wchar_t* upref = s_py.Py_GetExecPrefix();
        wchar_t* upath = s_py.Py_GetPath();
        printf("home = %S\nprog = %S\nexec = %S\npath=%S\n", uhome, uprog, upref, upath);
        fflush(stdout);
        // DEBUG things end
    }
#endif

    s_py.Py_Initialize();
    {
        PyObject* modSymbol = s_py.PyImport_ImportModule("symbol");
        PyObject* pyFileInput;
        long file_input;
        if (modSymbol == NULL) {
            s_py.PyErr_Print();
            return 77;
        }
        pyFileInput = s_py.PyObject_GetAttrString(modSymbol, "file_input");
        s_py.Py_DecRef(modSymbol);
        if (pyFileInput == NULL) {
            s_py.PyErr_Print();
            return 77;
        }
        file_input = s_py.PyLong_AsLong(pyFileInput);
        s_py.Py_DecRef(pyFileInput);
        if (file_input == -1) {
            s_py.PyErr_Print();
            return 77;
        }
        s_py.file_input = file_input;
    }

    return 0;
}

static int update_sys_path(const char* script_file) {
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
        return 1;
    }
    script_dir = get_dir_name(uscript_file);
    free(uscript_file);
    if (script_dir == NULL) {
        return 2;
    }
    new_path = calloc(wcslen(sys_path) + wcslen(path_sep) + wcslen(script_dir) + 1, sizeof(wchar_t));
    if (new_path == NULL) {
        free(script_dir);
        return 3;
    }
    wcscat(wcscat(wcscpy(new_path, sys_path), path_sep), script_dir);
    s_py.PySys_SetPath(new_path);
    free(script_dir);
    free(new_path);

    {
        // "import site" and run "site.main()" to override weird behaviour of site.py not being imported in some cases
        // TODO: understand why site.py is not imported in some cases
        PyObject* siteModule = s_py.PyImport_ImportModule("site");
        PyObject *siteMain, *noArgs, *siteMainRes;
        if (siteModule == NULL) {
            s_py.PyErr_Print();
            return -1;
        }
        siteMain = s_py.PyObject_GetAttrString(siteModule, "main");
        s_py.Py_DecRef(siteModule);
        if (siteMain == NULL) {
            s_py.PyErr_Print();
            return -2;
        }
        noArgs = s_py.PyTuple_New(0);
        if (noArgs == NULL) {
            s_py.Py_DecRef(siteMain);
            s_py.PyErr_Print();
            return -3;
        }
        siteMainRes = s_py.PyObject_CallObject(siteMain, noArgs);
        s_py.Py_DecRef(siteMain);
        s_py.Py_DecRef(noArgs);
        if (siteMainRes == NULL) {
            s_py.PyErr_Print();
            return -4;
        }
        s_py.Py_DecRef(siteMainRes);
    }

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
            return -1;
        }

        code = s_py.Py_CompileStringObject(buf, pName, s_py.file_input, NULL, 1);
        s_py.Py_DecRef(pName);
        if (code == NULL) {
            return -2;
        }
        res = s_py.PyImport_ExecCodeModuleEx("__main__", code, script_file);
        s_py.Py_DecRef(code);
        if (res == NULL) {
            return -3;
        }

        *result = res;
    }
    return 0;
}

// Interface from python wrapper to the filter


static int init_python_library_and_script(const char* dllfile, const char* pyfile) {
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
    } else {
        fprintf(stderr, "Python library was already loaded, refusing to load another");
        fflush(stderr);
    }
    if (update_sys_path(pyfile) != 0) {
        return 4;
    }

    return 0;
}

static int uninit_python_library_and_script() {
    s_py.Py_DecRef(s_py.user_module);
    s_py.Py_FinalizeEx();
    // TODO Check returning value

    free(s_py.program_name);
    free(s_py.python_home);
    s_py.hlib.close(&s_py.hlib);
    // TODO Check error??

    return 0;
}


static int python_call(const char* func) {


    PyObject* pyFunc = s_py.PyObject_GetAttrString(s_py.user_module, func);
    s_py.Py_DecRef(s_py.user_module);
    if (pyFunc == NULL) {
        s_py.PyErr_Print();
        return 77;
    }

    PyObject* args = s_py.PyTuple_New(2);
    if (args == NULL) {
        s_py.PyErr_Print();
        return 77;
    }
    PyObject* width = s_py.PyLong_FromLongLong(100);
    if (width == NULL) {
        s_py.PyErr_Print();
        return 77;
    }
    PyObject* height = s_py.PyLong_FromLongLong(200);
    if (height == NULL) {
        s_py.PyErr_Print();
        return 77;
    }
    if (s_py.PyTuple_SetItem(args, 0, width) != 0) {
        s_py.PyErr_Print();
        return 77;
    } else {
        width = NULL; // note: "args" now own "width", do not decref it
    }
    if (s_py.PyTuple_SetItem(args, 1, height) != 0) {
        s_py.PyErr_Print();
        return 77;
    } else {
        height = NULL; // note: "args" now own "height", too, do not decref it
    }

    PyObject* result = s_py.PyObject_CallObject(pyFunc, args);
    if (result == NULL) {
        // call failed, may be due to exception
        s_py.PyErr_Print();
        return 88;
    }

    return 0;
}





typedef struct PythonContext {
    const AVClass *class;

    char* python_library;
	char* script_filename;
	char* class_name;
	char* constructor_argument;

	PyObject* user_module;
} PythonContext;


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
    const uint8_t *src = in->data[0];
    uint8_t *dst = out->data[0];
    const int src_linesize = in->linesize[0];
    const int dst_linesize = out->linesize[0];
    const int width = in->width;
    const int height = in->height;

    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr+1)) / nb_jobs;

    // Currently only one thread is supported here, so jobnr has to be 0 and nb_jobs should be 1

    fprintf(stderr, "THREAD!!! %s\n", s->python_library); fflush(stderr);

    int pycall_res = python_call(s->class_name);
	fprintf(stderr, "python_call returns %d\n", pycall_res); fflush(stderr);

	fflush(stdout);	// Flushes the python output

    //pycall("d:\\Anaconda3\\envs\\tensorflow-cl\\python36.dll", "D:\\Projects\\ffmpeg-python-interop\\pyff\\foo.py", "foo");

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_RGBA, AV_PIX_FMT_BGRA, AV_PIX_FMT_ARGB, AV_PIX_FMT_ABGR,
        AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGB565BE, AV_PIX_FMT_BGR565BE, AV_PIX_FMT_RGB555BE, AV_PIX_FMT_BGR555BE,
        AV_PIX_FMT_RGB565LE, AV_PIX_FMT_BGR565LE, AV_PIX_FMT_RGB555LE, AV_PIX_FMT_BGR555LE,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_input(AVFilterLink *inlink)
{
    PythonContext *s = inlink->dst->priv;

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
    fprintf(stderr, "HERE2\n"); fflush(stderr);
    ctx->internal->execute(ctx, pythonCallProcess, &td, NULL, 1/*FFMIN(in->height, ff_filter_get_nb_threads(ctx))*/);



    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
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
        return 1;
    }
    res = get_user_module(s->script_filename, &(s->user_module));
    fprintf(stderr, "get_user_module returns %d\n", res);
    if (res != 0) {
        if (res < 0) {
            s_py.PyErr_Print();
        }
        return 2;
    }
    fflush(stderr);

    return 0;
}

static void uninit(AVFilterContext *ctx)
{
    int res;
	PythonContext *s = ctx->priv;

    s_py.Py_DecRef(s->user_module);

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

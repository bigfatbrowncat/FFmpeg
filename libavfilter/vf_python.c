/*
 * Copyright (c) 2010 Niel van der Westhuizen <nielkie@gmail.com>
 * Copyright (c) 2002 A'rpi
 * Copyright (c) 1997-2001 ZSNES Team ( zsknight@zsnes.com / _demo_@zsnes.com )
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
}
#endif
#if defined(__linux__) || defined(WINLIN)
static void* libdl_getsym(struct lib_handle_t* self, const char* name) {
    return dlsym(self->handle, name);
}
static void libdl_closelib(struct lib_handle_t* self) {
    dlclose(self->handle);
}
#endif

static int open_lib(const char* libname, lib_handle_t* out) {
    if (!out) return 1;
#if defined(WINLIN) || defined(_WIN32)
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

    int file_input; // token for Python compile() telling that this is a module we're Python-compiling
} py_embed_t;

static int fill_pyembed(lib_handle_t* pylib, py_embed_t* out) {
    if (out == NULL || pylib == NULL) return 1;

#define GET_FUNC(name)                          \
    out->name = pylib->get_sym(pylib, #name);   \
    if (out->name == NULL) return 2;

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

static int init_embed_python(py_embed_t* py, const char* pylib, const char* script_file) {
    // TODO: clean up memory properly

    // set program name so on *nix Python can find its prefix from it
    size_t main_sz;
    wchar_t* umain = py->Py_DecodeLocale(pylib, &main_sz);
    if (umain == NULL) {
        printf("cannot decode '%s' into wchar_t: %d\n", pylib, (int)main_sz);
        return 77;
    }
    py->Py_SetProgramName(umain); // do not free 'umain' until Py_FinalizeEx()!

    if (py->Py_GetPythonHome() == NULL && umain != NULL) {
        // flush path config cache, otherwise our manipulations with Python home won't be re-read by it
        py->Py_SetPath(NULL);
#if defined(_WIN32) || defined(__CYGWIN__)
        py->Py_SetPythonHome(get_dir_name(umain));
#else
#error Implement me properly if needed
#endif
    }

#ifdef _WIN32
    // DEBUG things start
    wchar_t* uhome = py->Py_GetPythonHome();
    wchar_t* uprog = py->Py_GetProgramName();
    wchar_t* upref = py->Py_GetExecPrefix();
    printf("home = %S\nprog = %S\nexec = %S\n", uhome, uprog, upref);
    fflush(stdout);
    // DEBUG things end

    // DEBUG things start
    wchar_t* upath = py->Py_GetPath();
    printf("path = %S\n", upath);
    fflush(stdout);
    // DEBUG things end
#endif

    py->Py_Initialize();
    PyObject* modSymbol = py->PyImport_ImportModule("symbol");
    if (modSymbol == NULL) {
        py->PyErr_Print();
        return 77;
    }
    PyObject* pyFileInput = py->PyObject_GetAttrString(modSymbol, "file_input");
    if (pyFileInput == NULL) {
        py->PyErr_Print();
        return 77;
    }
    long file_input = py->PyLong_AsLong(pyFileInput);
    py->Py_DecRef(pyFileInput);
    py->Py_DecRef(modSymbol);
    if (file_input == -1) {
        return 77;
    }

    {
        // append path to the script
        wchar_t* sys_path = py->Py_GetPath();
        wchar_t* uscript_file = py->Py_DecodeLocale(script_file, &main_sz);
        if (uscript_file == NULL) {
            return 99;
        }
        wchar_t* script_dir = get_dir_name(uscript_file);
        if (script_dir == NULL) {
            return 99;
        }
#if defined(_WIN32) || defined(__CYGWIN__)
        wchar_t* path_sep = L";";
#else
        wchar_t* path_sep = L":";
#endif
        wchar_t* new_path = calloc(wcslen(sys_path) + wcslen(path_sep) + wcslen(script_dir) + 1, sizeof(wchar_t));
        if (new_path == NULL) {
            return 99;
        }
        wcscat(wcscat(wcscpy(new_path, sys_path), path_sep), script_dir);
        py->PySys_SetPath(new_path);
    }

    // "import site" and run "site.main()" to override weird behaviour of site.py not being imported in some cases
    // TODO: understand why site.py is not imported in some cases
    PyObject* siteModule = py->PyImport_ImportModule("site");
    if (siteModule == NULL) {
        py->PyErr_Print();
        return 77;
    }
    PyObject* siteMain = py->PyObject_GetAttrString(siteModule, "main");
    if (siteMain == NULL) {
        py->PyErr_Print();
        return 77;
    }
    py->Py_DecRef(siteModule);
    PyObject* noArgs = py->PyTuple_New(0);
    if (noArgs == NULL) {
        py->PyErr_Print();
        return 77;
    }
    PyObject* siteMainRes = py->PyObject_CallObject(siteMain, noArgs);
    py->Py_DecRef(siteMain);
    py->Py_DecRef(noArgs);
    if (siteMainRes == NULL) {
        py->PyErr_Print();
        return 77;
    }
    py->Py_DecRef(siteMainRes);

    py->file_input = file_input;

    return 0;
}

static int get_user_module(py_embed_t* py, const char* script_file, PyObject** result) {
    if (py == NULL || script_file == NULL || result == NULL) return 1;

    FILE* fp = fopen(script_file, "rb");
    if (fp == NULL) return 3;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        //printf("cannot get size of '%s'\n", script_file);
        return 2;
    }
    long long size = ftell(fp);
    char* buf = calloc(size+1, 1);
    if (buf == NULL) {
        fclose(fp);
        return 3;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        free(buf);
        return 4;
    }
    long long read = fread(buf, 1, size, fp);
    if (read != size) {
        fclose(fp);
        free(buf);
        //printf("expected %d but read %d bytes\n", size, read);
        return 5;
    }
    fclose(fp);

    PyObject* pName = py->PyUnicode_DecodeFSDefault(script_file);
    if (pName == NULL) {
        return -1;
    }

    PyObject* code = py->Py_CompileStringObject(buf, pName, py->file_input, NULL, 1);
    py->Py_DecRef(pName);
    if (code == NULL) {
        return -2;
    }
    PyObject* res = py->PyImport_ExecCodeModuleEx("__main__", code, script_file);
    py->Py_DecRef(code);
    if (res == NULL) {
        return -3;
    }

    *result = res;
    return 0;
}

static int pycall(const char* dllfile, const char* pyfile, const char* func) {
    lib_handle_t hlib;
    if (open_lib(dllfile, &hlib) != 0) {
        return 1;
    }

    py_embed_t py_funcs;
    if (fill_pyembed(&hlib, &py_funcs) != 0) {
        hlib.close(&hlib);
        return 2;
    }

    if (init_embed_python(&py_funcs, dllfile, pyfile) != 0) {
        hlib.close(&hlib);
        return 3;
    }

    PyObject* res;
    int user_module_res = get_user_module(&py_funcs, pyfile, &res);
    if (user_module_res != 0) {
        if (user_module_res < 0) {
            py_funcs.PyErr_Print();
        }
        return 4;
    }

    PyObject* pyFunc = py_funcs.PyObject_GetAttrString(res, func);
    py_funcs.Py_DecRef(res);
    if (pyFunc == NULL) {
        py_funcs.PyErr_Print();
        return 77;
    }

    PyObject* args = py_funcs.PyTuple_New(2);
    if (args == NULL) {
        py_funcs.PyErr_Print();
        return 77;
    }
    PyObject* width = py_funcs.PyLong_FromLongLong(100);
    if (width == NULL) {
        py_funcs.PyErr_Print();
        return 77;
    }
    PyObject* height = py_funcs.PyLong_FromLongLong(200);
    if (height == NULL) {
        py_funcs.PyErr_Print();
        return 77;
    }
    if (py_funcs.PyTuple_SetItem(args, 0, width) != 0) {
        py_funcs.PyErr_Print();
        return 77;
    } else {
        width = NULL; // note: "args" now own "width", do not decref it
    }
    if (py_funcs.PyTuple_SetItem(args, 1, height) != 0) {
        py_funcs.PyErr_Print();
        return 77;
    } else {
        height = NULL; // note: "args" now own "height", too, do not decref it
    }

    PyObject* result = py_funcs.PyObject_CallObject(pyFunc, args);
    if (result == NULL) {
        // call failed, may be due to exception
        py_funcs.PyErr_Print();
        return 88;
    }

    py_funcs.Py_FinalizeEx();
    hlib.close(&hlib);
    return 0;
}





typedef struct PythonContext {
    const AVClass *class;

    char* python_library;
	char* script_filename;
	char* class_name;
	char* constructor_argument;
    /* masks used for two pixels interpolation */
//    uint32_t hi_pixel_mask;
//    uint32_t lo_pixel_mask;

    /* masks used for four pixels interpolation */
//    uint32_t q_hi_pixel_mask;
//    uint32_t q_lo_pixel_mask;

//    int bpp; ///< bytes per pixel, pixel stride for each (packed) pixel
//    int is_be;
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
    pycall(s->python_library, s->script_filename, s->class_name);
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
	PythonContext *s = ctx->priv;

    char* python_library;
	char* script_filename;
	char* class_name;
	char* constructor_argument;

	fprintf(stderr, "python_library: %s\n", s->python_library); fflush(stderr);
	fprintf(stderr, "script_filename: %s\n", s->script_filename); fflush(stderr);
	fprintf(stderr, "class_name: %s\n", s->class_name); fflush(stderr);
	fprintf(stderr, "constructor_argument: %s\n", s->constructor_argument); fflush(stderr);

	return 0;
}

AVFilter ff_vf_python = {
    .name          = "python",
    .description   = NULL_IF_CONFIG_SMALL("Process the input using a python script."),
    .priv_size     = sizeof(PythonContext),
    .priv_class    = &python_class,
    .query_formats = query_formats,
	.init          = init,
    .inputs        = python_inputs,
    .outputs       = python_outputs,
    .flags         = 0/*AVFILTER_FLAG_SLICE_THREADS*/,
};

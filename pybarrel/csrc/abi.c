#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdint.h>
#include <stdbool.h>

#include "barrel/barrel.h"

static PyObject* PyBRL_Error = NULL;

typedef struct {
    PyObject* compress_cb;
    PyObject* decompress_cb;
    PyObject* get_bound_cb;
} PyCompressorUserData;

static inline bool check_brl_error(BRL_Error err) {
    if (err == BRL_OK) {
        return true;
    }
    const char* msg = BRL_FormatError(err);
    PyObject* exc = PyObject_CallFunction(PyBRL_Error, "s", msg ? msg : "Unknown error");
    if (exc) {
        PyObject_SetAttrString(exc, "code", PyLong_FromLong((long)err));
        PyErr_SetObject(PyBRL_Error, exc);
        Py_DECREF(exc);
    } else {
        PyErr_Format(PyBRL_Error, "BRL Error [%d]: %s", err, msg ? msg : "Unknown error");
    }
    return false;
}

static void free_py_compressor_user_data(PyCompressorUserData* ud) {
    if (!ud) return;
    PyGILState_STATE gstate = PyGILState_Ensure();
    Py_XDECREF(ud->compress_cb);
    Py_XDECREF(ud->decompress_cb);
    Py_XDECREF(ud->get_bound_cb);
    PyMem_Free(ud);
    PyGILState_Release(gstate);
}

static void archive_capsule_destructor(PyObject* capsule) {
    BRL_Archive* arch = (BRL_Archive*)PyCapsule_GetPointer(capsule, "BRL_Archive");
    if (arch) {
        PyCompressorUserData* ud = (PyCompressorUserData*)PyCapsule_GetContext(capsule);
        if (ud) {
            free_py_compressor_user_data(ud);
        }
        BRL_Close(arch);
    }
}

static inline BRL_Archive* get_archive_handle(PyObject* capsule) {
    BRL_Archive* arch = (BRL_Archive*)PyCapsule_GetPointer(capsule, "BRL_Archive");
    if (!arch && !PyErr_Occurred()) {
        PyErr_SetString(PyExc_ValueError, "Archive handle is invalid or already closed");
    }
    return arch;
}

static uint64_t py_compress_trampoline(const void* src, uint64_t src_size, void* dst, uint64_t dst_capacity, uint64_t hash, void* user_data) {
    PyCompressorUserData* ud = (PyCompressorUserData*)user_data;
    if (!ud || !ud->compress_cb) return 0;

    PyGILState_STATE gstate = PyGILState_Ensure();

    PyObject* src_mv = PyMemoryView_FromMemory((char*)src, (Py_ssize_t)src_size, PyBUF_READ);
    PyObject* dst_mv = PyMemoryView_FromMemory((char*)dst, (Py_ssize_t)dst_capacity, PyBUF_WRITE);

    if (!src_mv || !dst_mv) {
        Py_XDECREF(src_mv);
        Py_XDECREF(dst_mv);
        PyGILState_Release(gstate);
        return 0;
    }

    PyObject* res = PyObject_CallFunction(ud->compress_cb, "OOK", src_mv, dst_mv, (unsigned long long)hash);
    Py_DECREF(src_mv);
    Py_DECREF(dst_mv);

    uint64_t written = 0;
    if (res) {
        written = (uint64_t)PyLong_AsUnsignedLongLong(res);
        Py_DECREF(res);
    } else {
        PyErr_Print();
    }

    PyGILState_Release(gstate);
    return written;
}

static uint64_t py_decompress_trampoline(const void* src, uint64_t src_size, void* dst, uint64_t dst_capacity, uint64_t hash, void* user_data) {
    PyCompressorUserData* ud = (PyCompressorUserData*)user_data;
    if (!ud || !ud->decompress_cb) return 0;

    PyGILState_STATE gstate = PyGILState_Ensure();

    PyObject* src_mv = PyMemoryView_FromMemory((char*)src, (Py_ssize_t)src_size, PyBUF_READ);
    PyObject* dst_mv = PyMemoryView_FromMemory((char*)dst, (Py_ssize_t)dst_capacity, PyBUF_WRITE);

    if (!src_mv || !dst_mv) {
        Py_XDECREF(src_mv);
        Py_XDECREF(dst_mv);
        PyGILState_Release(gstate);
        return 0;
    }

    PyObject* res = PyObject_CallFunction(ud->decompress_cb, "OOK", src_mv, dst_mv, (unsigned long long)hash);
    Py_DECREF(src_mv);
    Py_DECREF(dst_mv);

    uint64_t written = 0;
    if (res) {
        written = (uint64_t)PyLong_AsUnsignedLongLong(res);
        Py_DECREF(res);
    } else {
        PyErr_Print();
    }

    PyGILState_Release(gstate);
    return written;
}

static uint64_t py_get_bound_trampoline(uint64_t src_size, void* user_data) {
    PyCompressorUserData* ud = (PyCompressorUserData*)user_data;
    if (!ud || !ud->get_bound_cb) return src_size + (src_size / 8) + 256; // Fallback bound

    PyGILState_STATE gstate = PyGILState_Ensure();

    PyObject* res = PyObject_CallFunction(ud->get_bound_cb, "K", (unsigned long long)src_size);
    uint64_t bound = 0;
    if (res) {
        bound = (uint64_t)PyLong_AsUnsignedLongLong(res);
        Py_DECREF(res);
    } else {
        PyErr_Print();
    }

    PyGILState_Release(gstate);
    return bound;
}

static PyObject* py_brl_create(PyObject* self, PyObject* args) {
    const char* filepath;
    uint64_t hints, initial_cap, max_virt_cap;

    if (!PyArg_ParseTuple(args, "sKKK", &filepath, &hints, &initial_cap, &max_virt_cap)) {
        return NULL;
    }

    if (!check_brl_error(BRL_Create(filepath, hints, initial_cap, max_virt_cap))) {
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject* py_brl_open(PyObject* self, PyObject* args) {
    const char* filepath;
    uint32_t open_flags;

    if (!PyArg_ParseTuple(args, "sI", &filepath, &open_flags)) {
        return NULL;
    }

    BRL_Archive* arch = NULL;
    if (!check_brl_error(BRL_Open(filepath, open_flags, &arch))) {
        return NULL;
    }

    return PyCapsule_New(arch, "BRL_Archive", archive_capsule_destructor);
}

static PyObject* py_brl_close(PyObject* self, PyObject* args) {
    PyObject* capsule;
    if (!PyArg_ParseTuple(args, "O!", &PyCapsule_Type, &capsule)) {
        return NULL;
    }

    BRL_Archive* arch = (BRL_Archive*)PyCapsule_GetPointer(capsule, "BRL_Archive");
    if (!arch) {
        PyErr_Clear();
        Py_RETURN_NONE;
    }

    PyCompressorUserData* ud = (PyCompressorUserData*)PyCapsule_GetContext(capsule);
    if (ud) {
        free_py_compressor_user_data(ud);
        PyCapsule_SetContext(capsule, NULL);
    }

    PyCapsule_SetDestructor(capsule, NULL);
    PyCapsule_SetName(capsule, "BRL_Archive_Closed");

    if (!check_brl_error(BRL_Close(arch))) {
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject* py_brl_read(PyObject* self, PyObject* args) {
    PyObject* capsule;
    uint64_t hash;

    if (!PyArg_ParseTuple(args, "OK", &capsule, &hash)) {
        return NULL;
    }

    BRL_Archive* arch = get_archive_handle(capsule);
    if (!arch) return NULL;

    const uint8_t* out_data = NULL;
    uint64_t out_size = 0;

    if (!check_brl_error(BRL_Read(arch, hash, &out_data, &out_size))) {
        return NULL;
    }

    return PyMemoryView_FromMemory((char*)out_data, (Py_ssize_t)out_size, PyBUF_READ);
}

static PyObject* py_brl_read_copy(PyObject* self, PyObject* args) {
    PyObject* capsule;
    uint64_t hash, dst_capacity;

    if (!PyArg_ParseTuple(args, "OKK", &capsule, &hash, &dst_capacity)) {
        return NULL;
    }

    BRL_Archive* arch = get_archive_handle(capsule);
    if (!arch) return NULL;

    PyObject* bytearray_obj = PyByteArray_FromStringAndSize(NULL, (Py_ssize_t)dst_capacity);
    if (!bytearray_obj) return NULL;

    void* buf = PyByteArray_AsString(bytearray_obj);
    uint64_t written_size = 0;

    if (!check_brl_error(BRL_ReadCopy(arch, hash, buf, dst_capacity, &written_size))) {
        Py_DECREF(bytearray_obj);
        return NULL;
    }

    if (PyByteArray_Resize(bytearray_obj, (Py_ssize_t)written_size) < 0) {
        Py_DECREF(bytearray_obj);
        return NULL;
    }

    return bytearray_obj;
}

static PyObject* py_brl_write_ex(PyObject* self, PyObject* args) {
    PyObject* capsule;
    uint64_t hash;
    Py_buffer view;
    int use_compressor;

    if (!PyArg_ParseTuple(args, "OKy*p", &capsule, &hash, &view, &use_compressor)) {
        return NULL;
    }

    BRL_Archive* arch = get_archive_handle(capsule);
    if (!arch) {
        PyBuffer_Release(&view);
        return NULL;
    }

    BRL_Error err = BRL_WriteEx(arch, hash, view.buf, (uint64_t)view.len, (bool)use_compressor);
    PyBuffer_Release(&view);

    if (!check_brl_error(err)) {
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject* py_brl_delete(PyObject* self, PyObject* args) {
    PyObject* capsule;
    uint64_t hash;

    if (!PyArg_ParseTuple(args, "OK", &capsule, &hash)) {
        return NULL;
    }

    BRL_Archive* arch = get_archive_handle(capsule);
    if (!arch) return NULL;

    if (!check_brl_error(BRL_Delete(arch, hash))) {
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject* py_brl_sync(PyObject* self, PyObject* args) {
    PyObject* capsule;

    if (!PyArg_ParseTuple(args, "O", &capsule)) {
        return NULL;
    }

    BRL_Archive* arch = get_archive_handle(capsule);
    if (!arch) return NULL;

    if (!check_brl_error(BRL_Sync(arch))) {
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject* py_brl_hash_string(PyObject* self, PyObject* args) {
    const char* str;

    if (!PyArg_ParseTuple(args, "s", &str)) {
        return NULL;
    }

    return PyLong_FromUnsignedLongLong((unsigned long long)BRL_HashString(str));
}

static PyObject* py_brl_set_compressor(PyObject* self, PyObject* args) {
    PyObject *capsule, *compress_cb, *decompress_cb, *get_bound_cb;

    if (!PyArg_ParseTuple(args, "OOOO", &capsule, &compress_cb, &decompress_cb, &get_bound_cb)) {
        return NULL;
    }

    BRL_Archive* arch = get_archive_handle(capsule);
    if (!arch) return NULL;

    if (!PyCallable_Check(compress_cb) || !PyCallable_Check(decompress_cb) || !PyCallable_Check(get_bound_cb)) {
        PyErr_SetString(PyExc_TypeError, "Compressor callbacks must be callable objects");
        return NULL;
    }

    PyCompressorUserData* ud = (PyCompressorUserData*)PyCapsule_GetContext(capsule);
    if (ud) {
        free_py_compressor_user_data(ud);
    }

    ud = (PyCompressorUserData*)PyMem_Malloc(sizeof(PyCompressorUserData));
    if (!ud) return PyErr_NoMemory();

    Py_INCREF(compress_cb);
    Py_INCREF(decompress_cb);
    Py_INCREF(get_bound_cb);

    ud->compress_cb = compress_cb;
    ud->decompress_cb = decompress_cb;
    ud->get_bound_cb = get_bound_cb;

    BRL_Compressor comp = {
        .valid = true,
        .compress = py_compress_trampoline,
        .decompress = py_decompress_trampoline,
        .get_bound = py_get_bound_trampoline,
        .user_data = ud
    };

    if (!BRL_SetCompressor(arch, &comp)) {
        free_py_compressor_user_data(ud);
        PyErr_SetString(PyBRL_Error, "Failed to register compressor on archive handle");
        return NULL;
    }

    PyCapsule_SetContext(capsule, ud);
    Py_RETURN_NONE;
}

static PyObject* py_brl_resize_offline(PyObject* self, PyObject* args) {
    const char* filepath;
    uint64_t new_cap;

    if (!PyArg_ParseTuple(args, "sK", &filepath, &new_cap)) {
        return NULL;
    }

    if (!check_brl_error(BRL_ResizeOffline(filepath, new_cap))) {
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject* py_brl_pack(PyObject* self, PyObject* args) {
    PyObject* capsule;

    if (!PyArg_ParseTuple(args, "O", &capsule)) {
        return NULL;
    }

    BRL_Archive* arch = get_archive_handle(capsule);
    if (!arch) return NULL;

    if (!check_brl_error(BRL_Pack(arch))) {
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyMethodDef AbiMethods[] = {
    {"create",          py_brl_create,          METH_VARARGS, "Creates a new empty Barrel file."},
    {"open",            py_brl_open,            METH_VARARGS, "Opens and mmaps a Barrel file."},
    {"close",           py_brl_close,           METH_VARARGS, "Flushes and unmaps archive."},
    {"read",            py_brl_read,            METH_VARARGS, "Returns zero-copy memoryview of entry."},
    {"read_copy",       py_brl_read_copy,       METH_VARARGS, "Decompresses and reads an entry into a bytearray."},
    {"write_ex",        py_brl_write_ex,        METH_VARARGS, "Writes or overwrites entry data."},
    {"delete",          py_brl_delete,          METH_VARARGS, "Deletes entry by hash."},
    {"sync",            py_brl_sync,            METH_VARARGS, "Synchronizes mmap memory to disk."},
    {"hash_string",     py_brl_hash_string,     METH_VARARGS, "Hash string key using FNV algorithm."},
    {"set_compressor",  py_brl_set_compressor,  METH_VARARGS, "Sets compression callbacks on archive."},
    {"resize_offline",  py_brl_resize_offline,  METH_VARARGS, "Resizes a Barrel archive offline."},
    {"pack",            py_brl_pack,            METH_VARARGS, "Packs archive to strip dead space and truncate file."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef abi_module = {
    PyModuleDef_HEAD_INIT,
    "_abi",
    "CPython binding interface for libbarrel",
    -1,
    AbiMethods
};

#define EXPORT_ENUM(m, val) PyModule_AddIntConstant(m, #val, val)

PyMODINIT_FUNC PyInit__abi(void) {
    PyObject* m = PyModule_Create(&abi_module);
    if (!m) return NULL;

    if (PyModule_AddIntConstant(m, "__BRL_VERSION_HEX__", BRL_VERSION) < 0) {
        Py_DECREF(m);
        return NULL;
    }

    char brl_ver_str[16];
    snprintf(brl_ver_str, sizeof(brl_ver_str), "%d.%d.%d", 
             (BRL_VERSION >> 8) & 0xFF, 
             (BRL_VERSION >> 4) & 0x0F, 
             BRL_VERSION & 0x0F);

    if (PyModule_AddStringConstant(m, "__BRL_VERSION__", brl_ver_str) < 0) {
        Py_DECREF(m);
        return NULL;
    }

    EXPORT_ENUM(m, BRL_OK);
    EXPORT_ENUM(m, BRL_INVALID_PARAM);
    EXPORT_ENUM(m, BRL_INVALID_FD);
    EXPORT_ENUM(m, BRL_INVALID_FILE_SIZE);
    EXPORT_ENUM(m, BRL_READ_FAIL);
    EXPORT_ENUM(m, BRL_WRITE_FAIL);
    EXPORT_ENUM(m, BRL_INVALID_HEADER);
    EXPORT_ENUM(m, BRL_INVALID_MAGIC);
    EXPORT_ENUM(m, BRL_INVALID_VERSION);
    EXPORT_ENUM(m, BRL_INVALID_IDX_CAPACITY);
    EXPORT_ENUM(m, BRL_INVALID_IDX_OFFSET);
    EXPORT_ENUM(m, BRL_HI_WATER_MARK_LESS_THAN_IDX_OFFSET);
    EXPORT_ENUM(m, BRL_HI_WATER_MARK_MORE_THAN_FILE_SIZE);
    EXPORT_ENUM(m, BRL_HI_WATER_MARK_MORE_THAN_VIRTUAL_CAPACITY);
    EXPORT_ENUM(m, BRL_INVALID_INDEX_BYTES);
    EXPORT_ENUM(m, BRL_VIRTUAL_CAPACITY_MORE_THAN_MAX);
    EXPORT_ENUM(m, BRL_HEADER_WRITE_FAIL);
    EXPORT_ENUM(m, BRL_ENTRY_READ_FAIL);
    EXPORT_ENUM(m, BRL_ENTRY_WRITE_FAIL);
    EXPORT_ENUM(m, BRL_SPARSE_ALLOC_FAIL);
    EXPORT_ENUM(m, BRL_ALLOC_FAIL);
    EXPORT_ENUM(m, BRL_MMAP_FAIL);
    EXPORT_ENUM(m, BRL_MSYNC_FAIL);
    EXPORT_ENUM(m, BRL_ENTRY_NOT_FOUND);
    EXPORT_ENUM(m, BRL_NO_SLOT_AVAILABLE);
    EXPORT_ENUM(m, BRL_NO_DECOMPRESSOR);
    EXPORT_ENUM(m, BRL_DECOMPRESSOR_CALLBACK_FAILED);
    EXPORT_ENUM(m, BRL_REQUIRES_DECOMPRESSION);
    EXPORT_ENUM(m, BRL_BUFFER_TOO_SMALL);
    EXPORT_ENUM(m, BRL_RESIZE_SIZE_TOO_SMALL);
    EXPORT_ENUM(m, BRL_RESIZE_DATA_TRUNCATION);
    EXPORT_ENUM(m, BRL_UNKNOWN);

    EXPORT_ENUM(m, BRL_OPEN_NORMAL);
    EXPORT_ENUM(m, BRL_OPEN_ENABLE_COMPRESSOR_LRU_CACHE);

    EXPORT_ENUM(m, BRL_ENTRY_ACTIVE);
    EXPORT_ENUM(m, BRL_ENTRY_COMPRESSED);

    EXPORT_ENUM(m, BRL_ARCHIVE_NORMAL);
    EXPORT_ENUM(m, BRL_ARCHIVE_PACKED);

    PyBRL_Error = PyErr_NewException("pybarrel.BarrelError", NULL, NULL);
    Py_XINCREF(PyBRL_Error);
    if (PyModule_AddObject(m, "BarrelError", PyBRL_Error) < 0) {
        Py_XDECREF(PyBRL_Error);
        Py_DECREF(m);
        return NULL;
    }

    return m;
}

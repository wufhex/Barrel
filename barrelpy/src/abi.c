#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdbool.h>
#include "barrel/barrel.h"

typedef struct {
    PyObject_HEAD
    BRL_Archive* handle;
    PyObject* py_compress_cb;
    PyObject* py_decompress_cb;
    PyObject* py_get_bound_cb;
} PyBrlArchive;

static void PyBrlArchive_dealloc(PyBrlArchive* self) {
    Py_XDECREF(self->py_compress_cb);
    Py_XDECREF(self->py_decompress_cb);
    Py_XDECREF(self->py_get_bound_cb);

    if (self->handle) {
        BRL_Close(self->handle);
        self->handle = NULL;
    }
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject PyBrlArchiveType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "barrel.Archive",
    .tp_doc = "Wrapper around a native BRL_Archive pointer",
    .tp_basicsize = sizeof(PyBrlArchive),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_dealloc = (destructor)PyBrlArchive_dealloc,
};

static PyObject* PyBrlArchive_FromPointer(BRL_Archive* arch) {
    PyBrlArchive* obj = (PyBrlArchive*)PyObject_New(PyBrlArchive, &PyBrlArchiveType);
    if (!obj) return NULL;
    obj->handle = arch;
    obj->py_compress_cb = NULL;
    obj->py_decompress_cb = NULL;
    obj->py_get_bound_cb = NULL;
    return (PyObject*)obj;
}

/*
 * Native C Callbacks bridging C API -> CPython GIL -> Python Functions
 */

static uint64_t c_compress_cb(const void* src, uint64_t src_size, void* dst, uint64_t dst_capacity, uint64_t hash, void* user_data) {
    PyBrlArchive* self = (PyBrlArchive*)user_data;
    if (!self || !self->py_compress_cb) return 0;

    PyGILState_STATE gstate = PyGILState_Ensure();

    PyObject* src_bytes = PyBytes_FromStringAndSize((const char*)src, (Py_ssize_t)src_size);
    PyObject* py_hash = PyLong_FromUnsignedLongLong((unsigned long long)hash);

    PyObject* result = PyObject_CallFunctionObjArgs(self->py_compress_cb, src_bytes, py_hash, NULL);

    Py_DECREF(src_bytes);
    Py_DECREF(py_hash);

    uint64_t bytes_written = 0;
    if (result && PyBytes_Check(result)) {
        Py_ssize_t res_size = PyBytes_Size(result);
        if ((uint64_t)res_size <= dst_capacity) {
            memcpy(dst, PyBytes_AsString(result), (size_t)res_size);
            bytes_written = (uint64_t)res_size;
        } else {
            PyErr_SetString(PyExc_BufferError, "Compressed output exceeds buffer capacity");
        }
    }

    Py_XDECREF(result);
    if (PyErr_Occurred()) PyErr_Print();

    PyGILState_Release(gstate);
    return bytes_written;
}

static uint64_t c_decompress_cb(const void* src, uint64_t src_size, void* dst, uint64_t dst_capacity, uint64_t hash, void* user_data) {
    PyBrlArchive* self = (PyBrlArchive*)user_data;
    if (!self || !self->py_decompress_cb) return 0;

    PyGILState_STATE gstate = PyGILState_Ensure();

    PyObject* src_bytes = PyBytes_FromStringAndSize((const char*)src, (Py_ssize_t)src_size);
    PyObject* py_hash = PyLong_FromUnsignedLongLong((unsigned long long)hash);
    PyObject* py_cap = PyLong_FromUnsignedLongLong((unsigned long long)dst_capacity);

    PyObject* result = PyObject_CallFunctionObjArgs(self->py_decompress_cb, src_bytes, py_hash, py_cap, NULL);

    Py_DECREF(src_bytes);
    Py_DECREF(py_hash);
    Py_DECREF(py_cap);

    uint64_t bytes_written = 0;
    if (result && PyBytes_Check(result)) {
        Py_ssize_t res_size = PyBytes_Size(result);
        if ((uint64_t)res_size <= dst_capacity) {
            memcpy(dst, PyBytes_AsString(result), (size_t)res_size);
            bytes_written = (uint64_t)res_size;
        }
    }

    Py_XDECREF(result);
    if (PyErr_Occurred()) PyErr_Print();

    PyGILState_Release(gstate);
    return bytes_written;
}

static uint64_t c_get_bound_cb(uint64_t src_size, void* user_data) {
    PyBrlArchive* self = (PyBrlArchive*)user_data;
    if (!self || !self->py_get_bound_cb) return src_size * 2; // Conservative default

    PyGILState_STATE gstate = PyGILState_Ensure();

    PyObject* py_size = PyLong_FromUnsignedLongLong((unsigned long long)src_size);
    PyObject* result = PyObject_CallFunctionObjArgs(self->py_get_bound_cb, py_size, NULL);

    Py_DECREF(py_size);

    uint64_t bound = 0;
    if (result && PyLong_Check(result)) {
        bound = PyLong_AsUnsignedLongLong(result);
    }

    Py_XDECREF(result);
    if (PyErr_Occurred()) PyErr_Print();

    PyGILState_Release(gstate);
    return bound;
}

/*
 * Native C Binding Methods
 */

// Register compressor callbacks on an open Archive
static PyObject* py_brl_register_compressor(PyObject* self, PyObject* args) {
    PyBrlArchive* arch_obj;
    PyObject *comp_cb, *decomp_cb, *bound_cb;

    if (!PyArg_ParseTuple(args, "O!OOO", &PyBrlArchiveType, &arch_obj, &comp_cb, &decomp_cb, &bound_cb)) {
        return NULL;
    }

    if (!arch_obj->handle) {
        PyErr_SetString(PyExc_ValueError, "Archive handle is closed");
        return NULL;
    }

    if (!PyCallable_Check(comp_cb) || !PyCallable_Check(decomp_cb) || !PyCallable_Check(bound_cb)) {
        PyErr_SetString(PyExc_TypeError, "Compressor arguments must be callable functions");
        return NULL;
    }

    Py_XINCREF(comp_cb);
    Py_XDECREF(arch_obj->py_compress_cb);
    arch_obj->py_compress_cb = comp_cb;

    Py_XINCREF(decomp_cb);
    Py_XDECREF(arch_obj->py_decompress_cb);
    arch_obj->py_decompress_cb = decomp_cb;

    Py_XINCREF(bound_cb);
    Py_XDECREF(arch_obj->py_get_bound_cb);
    arch_obj->py_get_bound_cb = bound_cb;

    arch_obj->handle->compressor.valid = true;
    arch_obj->handle->compressor.compress = c_compress_cb;
    arch_obj->handle->compressor.decompress = c_decompress_cb;
    arch_obj->handle->compressor.get_bound = c_get_bound_cb;
    arch_obj->handle->compressor.user_data = arch_obj;

    Py_RETURN_NONE;
}

// BRL_GetHeader(BRL_Archive* arch) -> dict representing BRL_DiskHeader
static PyObject* py_brl_get_header(PyObject* self, PyObject* args) {
    PyBrlArchive* arch_obj;

    if (!PyArg_ParseTuple(args, "O!", &PyBrlArchiveType, &arch_obj)) {
        return NULL;
    }

    if (!arch_obj->handle || !arch_obj->handle->header) {
        PyErr_SetString(PyExc_ValueError, "Invalid or closed Archive header");
        return NULL;
    }

    BRL_DiskHeader* hdr = arch_obj->handle->header;

    PyObject* dict = PyDict_New();
    char sig[3] = { hdr->signature[0], hdr->signature[1], '\0' };

    PyDict_SetItemString(dict, "signature", PyUnicode_FromString(sig));
    PyDict_SetItemString(dict, "version", PyLong_FromUnsignedLong((unsigned long)hdr->version));
    PyDict_SetItemString(dict, "flags", PyLong_FromUnsignedLong((unsigned long)hdr->flags));
    PyDict_SetItemString(dict, "file_count", PyLong_FromUnsignedLongLong((unsigned long long)hdr->file_count));
    PyDict_SetItemString(dict, "virtual_capacity", PyLong_FromUnsignedLongLong((unsigned long long)hdr->virtual_capacity));
    PyDict_SetItemString(dict, "high_water_mark", PyLong_FromUnsignedLongLong((unsigned long long)hdr->high_water_mark));
    PyDict_SetItemString(dict, "index_offset", PyLong_FromUnsignedLongLong((unsigned long long)hdr->index_offset));
    PyDict_SetItemString(dict, "index_capacity", PyLong_FromUnsignedLong((unsigned long)hdr->index_capacity));
    PyDict_SetItemString(dict, "hints", PyLong_FromUnsignedLongLong((unsigned long long)hdr->hints));

    return dict;
}

// BRL_GetEntryMeta(BRL_Archive* arch, uint64_t slot_index) -> dict representing BRL_EntryMeta
static PyObject* py_brl_get_entry_meta(PyObject* self, PyObject* args) {
    PyBrlArchive* arch_obj;
    unsigned int slot_idx;

    if (!PyArg_ParseTuple(args, "O!I", &PyBrlArchiveType, &arch_obj, &slot_idx)) {
        return NULL;
    }

    if (!arch_obj->handle || !arch_obj->handle->metadata) {
        PyErr_SetString(PyExc_ValueError, "Invalid or closed Archive metadata");
        return NULL;
    }

    if (slot_idx >= arch_obj->handle->index_capacity) {
        PyErr_SetString(PyExc_IndexError, "Slot index out of bounds");
        return NULL;
    }

    BRL_EntryMeta* meta = &arch_obj->handle->metadata[slot_idx];

    PyObject* dict = PyDict_New();
    PyDict_SetItemString(dict, "hash", PyLong_FromUnsignedLongLong((unsigned long long)arch_obj->handle->hashes[slot_idx]));
    PyDict_SetItemString(dict, "offset", PyLong_FromUnsignedLongLong((unsigned long long)meta->offset));
    PyDict_SetItemString(dict, "size", PyLong_FromUnsignedLongLong((unsigned long long)meta->size));
    PyDict_SetItemString(dict, "compressed_size", PyLong_FromUnsignedLongLong((unsigned long long)meta->compressed_size));
    PyDict_SetItemString(dict, "allocated_size", PyLong_FromUnsignedLongLong((unsigned long long)meta->allocated_size));
    PyDict_SetItemString(dict, "flags", PyLong_FromUnsignedLong((unsigned long)meta->flags));

    return dict;
}

static PyObject* py_brl_create(PyObject* self, PyObject* args) {
    const char* filepath;
    unsigned long long hints, initial_index_capacity, max_virtual_capacity;

    if (!PyArg_ParseTuple(args, "sKKK", &filepath, &hints, &initial_index_capacity, &max_virtual_capacity)) {
        return NULL;
    }

    BRL_Error err = BRL_Create(filepath, (uint64_t)hints, (uint64_t)initial_index_capacity, (uint64_t)max_virtual_capacity);
    return PyLong_FromLong((long)err);
}

static PyObject* py_brl_open(PyObject* self, PyObject* args) {
    const char* filepath;
    unsigned int open_flags;

    if (!PyArg_ParseTuple(args, "sI", &filepath, &open_flags)) {
        return NULL;
    }

    BRL_Archive* arch = NULL;
    BRL_Error err = BRL_Open(filepath, (uint32_t)open_flags, &arch);

    PyObject* arch_obj = Py_None;
    if (err == BRL_OK && arch != NULL) {
        arch_obj = PyBrlArchive_FromPointer(arch);
        if (!arch_obj) return NULL;
    } else {
        Py_INCREF(Py_None);
    }

    return Py_BuildValue("(iO)", (int)err, arch_obj);
}

static PyObject* py_brl_close(PyObject* self, PyObject* args) {
    PyBrlArchive* arch_obj;

    if (!PyArg_ParseTuple(args, "O!", &PyBrlArchiveType, &arch_obj)) {
        return NULL;
    }

    if (!arch_obj->handle) {
        return PyLong_FromLong((long)BRL_OK);
    }

    BRL_Error err = BRL_Close(arch_obj->handle);
    arch_obj->handle = NULL;
    return PyLong_FromLong((long)err);
}

static PyObject* py_brl_read(PyObject* self, PyObject* args) {
    PyBrlArchive* arch_obj;
    unsigned long long hash;

    if (!PyArg_ParseTuple(args, "O!K", &PyBrlArchiveType, &arch_obj, &hash)) {
        return NULL;
    }

    if (!arch_obj->handle) {
        PyErr_SetString(PyExc_ValueError, "Archive handle is closed");
        return NULL;
    }

    const uint8_t* out_data = NULL;
    uint64_t out_size = 0;
    BRL_Error err = BRL_Read(arch_obj->handle, (uint64_t)hash, &out_data, &out_size);

    if (err == BRL_OK && out_data != NULL) {
        PyObject* py_data = PyMemoryView_FromMemory((char*)out_data, (Py_ssize_t)out_size, PyBUF_READ);
        PyObject* tuple = Py_BuildValue("(iO)", (int)err, py_data);
        Py_DECREF(py_data);
        return tuple;
    }

    return Py_BuildValue("(iO)", (int)err, Py_None);
}

static PyObject* py_brl_read_copy(PyObject* self, PyObject* args) {
    PyBrlArchive* arch_obj;
    unsigned long long hash, dst_capacity;

    if (!PyArg_ParseTuple(args, "O!KK", &PyBrlArchiveType, &arch_obj, &hash, &dst_capacity)) {
        return NULL;
    }

    if (!arch_obj->handle) {
        PyErr_SetString(PyExc_ValueError, "Archive handle is closed");
        return NULL;
    }

    void* dst_buffer = PyMem_Malloc((size_t)dst_capacity);
    if (!dst_buffer) return PyErr_NoMemory();

    uint64_t out_written_size = 0;
    BRL_Error err = BRL_ReadCopy(arch_obj->handle, (uint64_t)hash, dst_buffer, (uint64_t)dst_capacity, &out_written_size);

    if (err == BRL_OK) {
        PyObject* py_bytes = PyBytes_FromStringAndSize((const char*)dst_buffer, (Py_ssize_t)out_written_size);
        PyMem_Free(dst_buffer);
        PyObject* tuple = Py_BuildValue("(iO)", (int)err, py_bytes);
        Py_DECREF(py_bytes);
        return tuple;
    }

    PyMem_Free(dst_buffer);
    return Py_BuildValue("(iO)", (int)err, Py_None);
}

static PyObject* py_brl_write_ex(PyObject* self, PyObject* args) {
    PyBrlArchive* arch_obj;
    unsigned long long hash;
    Py_buffer view;
    int use_compressor;

    if (!PyArg_ParseTuple(args, "O!Ky*p", &PyBrlArchiveType, &arch_obj, &hash, &view, &use_compressor)) {
        return NULL;
    }

    if (!arch_obj->handle) {
        PyBuffer_Release(&view);
        PyErr_SetString(PyExc_ValueError, "Archive handle is closed");
        return NULL;
    }

    BRL_Error err = BRL_WriteEx(arch_obj->handle, (uint64_t)hash, view.buf, (uint64_t)view.len, (bool)use_compressor);
    PyBuffer_Release(&view);

    return PyLong_FromLong((long)err);
}

static PyObject* py_brl_write(PyObject* self, PyObject* args) {
    PyBrlArchive* arch_obj;
    unsigned long long hash;
    Py_buffer view;

    if (!PyArg_ParseTuple(args, "O!Ky*", &PyBrlArchiveType, &arch_obj, &hash, &view)) {
        return NULL;
    }

    if (!arch_obj->handle) {
        PyBuffer_Release(&view);
        PyErr_SetString(PyExc_ValueError, "Archive handle is closed");
        return NULL;
    }

    BRL_Error err = BRL_Write(arch_obj->handle, (uint64_t)hash, view.buf, (uint64_t)view.len);
    PyBuffer_Release(&view);

    return PyLong_FromLong((long)err);
}

static PyObject* py_brl_delete(PyObject* self, PyObject* args) {
    PyBrlArchive* arch_obj;
    unsigned long long hash;

    if (!PyArg_ParseTuple(args, "O!K", &PyBrlArchiveType, &arch_obj, &hash)) {
        return NULL;
    }

    if (!arch_obj->handle) {
        PyErr_SetString(PyExc_ValueError, "Archive handle is closed");
        return NULL;
    }

    BRL_Error err = BRL_Delete(arch_obj->handle, (uint64_t)hash);
    return PyLong_FromLong((long)err);
}

static PyObject* py_brl_sync(PyObject* self, PyObject* args) {
    PyBrlArchive* arch_obj;

    if (!PyArg_ParseTuple(args, "O!", &PyBrlArchiveType, &arch_obj)) {
        return NULL;
    }

    if (!arch_obj->handle) {
        PyErr_SetString(PyExc_ValueError, "Archive handle is closed");
        return NULL;
    }

    BRL_Error err = BRL_Sync(arch_obj->handle);
    return PyLong_FromLong((long)err);
}

static PyObject* py_brl_hash_string(PyObject* self, PyObject* args) {
    const char* str;

    if (!PyArg_ParseTuple(args, "s", &str)) return NULL;

    uint64_t hash = BRL_HashString(str);
    return PyLong_FromUnsignedLongLong((unsigned long long)hash);
}

static PyObject* py_brl_format_error(PyObject* self, PyObject* args) {
    int err;

    if (!PyArg_ParseTuple(args, "i", &err)) return NULL;

    const char* err_str = BRL_FormatError((BRL_Error)err);
    return PyUnicode_FromString(err_str ? err_str : "Unknown Error");
}

/* 
 * Module Registration
 */

static PyMethodDef BarrelMethods[] = {
    {"create", py_brl_create, METH_VARARGS, "Creates a new empty Barrel file on disk."},
    {"open", py_brl_open, METH_VARARGS, "Opens and mmaps a Barrel file. Returns (err, Archive)."},
    {"close", py_brl_close, METH_VARARGS, "Flushes changes and unmaps the archive."},
    {"read", py_brl_read, METH_VARARGS, "Returns direct mapped memory view to data. Returns (err, memoryview)."},
    {"read_copy", py_brl_read_copy, METH_VARARGS, "Read an entry and copy it in a buffer. Returns (err, bytes)."},
    {"write_ex", py_brl_write_ex, METH_VARARGS, "Writes data into existing hole, overwrites in-place, or appends with compressor option."},
    {"write", py_brl_write, METH_VARARGS, "Writes data into existing hole, overwrites in-place, or appends."},
    {"delete", py_brl_delete, METH_VARARGS, "Deletes a file entry and marks its region as an orphan hole."},
    {"sync", py_brl_sync, METH_VARARGS, "Synchronizes mmap memory to disk."},
    {"hash_string", py_brl_hash_string, METH_VARARGS, "Hash a string, used to locate entries by original name."},
    {"format_error", py_brl_format_error, METH_VARARGS, "Formats the error code into a string."},
    {"register_compressor", py_brl_register_compressor, METH_VARARGS, "Registers python callback functions for compression."},
    {"get_header", py_brl_get_header, METH_VARARGS, "Inspects header fields of an open Archive."},
    {"get_entry_meta", py_brl_get_entry_meta, METH_VARARGS, "Inspects metadata for an entry slot index."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef barrelmodule = {
    PyModuleDef_HEAD_INIT,
    "barrel",
    "CPython C-API bindings for libbarrel archive library",
    -1,
    BarrelMethods
};

#define EXPOSE_ENUM_VAL(module, name) PyModule_AddIntConstant(module, #name, name)

PyMODINIT_FUNC PyInit__barrel(void) {
    if (PyType_Ready(&PyBrlArchiveType) < 0) return NULL;

    PyObject* m = PyModule_Create(&barrelmodule);
    if (!m) return NULL;

    Py_INCREF(&PyBrlArchiveType);
    if (PyModule_AddObject(m, "Archive", (PyObject*)&PyBrlArchiveType) < 0) {
        Py_DECREF(&PyBrlArchiveType);
        Py_DECREF(m);
        return NULL;
    }

    /* Open & Header Flags */
    EXPOSE_ENUM_VAL(m, BRL_ARCHIVE_NORMAL);
    EXPOSE_ENUM_VAL(m, BRL_OPEN_NORMAL);
    EXPOSE_ENUM_VAL(m, BRL_OPEN_ENABLE_COMPRESSOR_LRU_CACHE);

    /* Entry Flags */
    EXPOSE_ENUM_VAL(m, BRL_ENTRY_ACTIVE);
    EXPOSE_ENUM_VAL(m, BRL_ENTRY_COMPRESSED);

    /* Sentinels & Constants */
    EXPOSE_ENUM_VAL(m, BRL_EMPTY_HASH);
    EXPOSE_ENUM_VAL(m, BRL_TOMBSTONE_HASH);

    /* Errors */
    EXPOSE_ENUM_VAL(m, BRL_OK);
    EXPOSE_ENUM_VAL(m, BRL_INVALID_PARAM);
    EXPOSE_ENUM_VAL(m, BRL_INVALID_FD);
    EXPOSE_ENUM_VAL(m, BRL_INVALID_FILE_SIZE);
    EXPOSE_ENUM_VAL(m, BRL_READ_FAIL);
    EXPOSE_ENUM_VAL(m, BRL_WRITE_FAIL);
    EXPOSE_ENUM_VAL(m, BRL_INVALID_HEADER);
    EXPOSE_ENUM_VAL(m, BRL_INVALID_MAGIC);
    EXPOSE_ENUM_VAL(m, BRL_INVALID_VERSION);
    EXPOSE_ENUM_VAL(m, BRL_INVALID_IDX_CAPACITY);
    EXPOSE_ENUM_VAL(m, BRL_INVALID_IDX_OFFSET);
    EXPOSE_ENUM_VAL(m, BRL_HI_WATER_MARK_LESS_THAN_IDX_OFFSET);
    EXPOSE_ENUM_VAL(m, BRL_HI_WATER_MARK_MORE_THAN_FILE_SIZE);
    EXPOSE_ENUM_VAL(m, BRL_HI_WATER_MARK_MORE_THAN_VIRTUAL_CAPACITY);
    EXPOSE_ENUM_VAL(m, BRL_INVALID_INDEX_BYTES);
    EXPOSE_ENUM_VAL(m, BRL_VIRTUAL_CAPACITY_MORE_THAN_MAX);
    EXPOSE_ENUM_VAL(m, BRL_HEADER_WRITE_FAIL);
    EXPOSE_ENUM_VAL(m, BRL_ENTRY_READ_FAIL);
    EXPOSE_ENUM_VAL(m, BRL_ENTRY_WRITE_FAIL);
    EXPOSE_ENUM_VAL(m, BRL_SPARSE_ALLOC_FAIL);
    EXPOSE_ENUM_VAL(m, BRL_ALLOC_FAIL);
    EXPOSE_ENUM_VAL(m, BRL_MMAP_FAIL);
    EXPOSE_ENUM_VAL(m, BRL_MSYNC_FAIL);
    EXPOSE_ENUM_VAL(m, BRL_ENTRY_NOT_FOUND);
    EXPOSE_ENUM_VAL(m, BRL_NO_SLOT_AVAILABLE);
    EXPOSE_ENUM_VAL(m, BRL_NO_DECOMPRESSOR);
    EXPOSE_ENUM_VAL(m, BRL_DECOMPRESSOR_CALLBACK_FAILED);
    EXPOSE_ENUM_VAL(m, BRL_REQUIRES_DECOMPRESSION);
    EXPOSE_ENUM_VAL(m, BRL_BUFFER_TOO_SMALL);
    EXPOSE_ENUM_VAL(m, BRL_UNKNOWN);

    return m;
}

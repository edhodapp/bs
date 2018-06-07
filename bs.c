#include <Python.h>

/*
  bs.c

  Defines the Python C extension type, BitStrm.
  Initialize by calling bs.BitStrm(<buffer protocol object>)
  Get bits by calling getbits(<number of bits to get>) method on
  BitStrm object.  Maximum number of bits to get at one time is 64.
  The buffer protocol object that initializes BitStrm should be a
  binary type like 'bytes', 'bytearray', or 'memoryview' or a type
  exception will be raised.  You may also specify a non-byte aligned
  buffer by using the optional 'size' keyword argument like this:

  spam = bs.BitStrm(b'\x12\x34', size=15)

  which reduces the number of bits available from 16 to 15.  The
  least significant bits are the ones that get ignored when doing
  this.
*/


#define MIN(a, b) ((a)>(b)?(b):(a))


typedef struct {
    PyObject_HEAD
    Py_buffer pybuf;
    unsigned char *pybuf_ptr; /* current pointer into pybuf */
    unsigned long bitbuf;
    int size;
    int bitcount; /* number of bits left in bitbuf */
} BitStrm;


static void
BitStrm_dealloc(BitStrm *self) {
    PyBuffer_Release(&self->pybuf);
    Py_TYPE(self)->tp_free((PyObject*)self);
}


static PyObject *
BitStrm_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    BitStrm *self;
    self = (BitStrm *)type->tp_alloc(type, 0);
    self->size = 0;
    return (PyObject *)self;
}


static int
BitStrm_init(BitStrm *self, PyObject *args, PyObject *kwds) {
    static char *kwlist[] = {"bitstream", "size", NULL};

    if (!PyArg_ParseTupleAndKeywords(
            args, kwds, "y*|i", kwlist, &self->pybuf, &self->size)) {
        return -1;
    }

    self->bitbuf = 0;
    self->bitcount = 0;
    self->pybuf_ptr = (unsigned char *)self->pybuf.buf;

    if (!self->size) {
        self->size = 8 * (self->pybuf.len);
    }

    return 0;
}


static int
reload_bitbuf(BitStrm *self)
{
    void *pybuf_ptr = (void *)self->pybuf_ptr;
    unsigned long bitbuf = 0;

    /* read largest type-aligned value possible */
    if (((unsigned long)pybuf_ptr & 7) == 0 && self->size >= 64) {
        asm ("movq (%[pybuf_ptr]), %%rax\n\t"
             "bswap %%rax\n\t"
             "add $8, %[pybuf_ptr]\n\t"
             : [bitbuf] "=a"(bitbuf),
               [pybuf_ptr] "+D"(pybuf_ptr));
        self->bitcount = 64;
        self->size -= 64;
    } else if (((unsigned long)pybuf_ptr & 3) == 0 && self->size >= 32) {
        asm ("xor %%rax, %%rax\n\t"
             "movl (%[pybuf_ptr]), %%eax\n\t"
             "bswap %%rax\n\t"
             "add $4, %[pybuf_ptr]\n\t"
             : [bitbuf] "=a"(bitbuf),
               [pybuf_ptr] "+D"(pybuf_ptr));
        self->bitcount = 32;
        self->size -= 32;
    } else if (((unsigned long)pybuf_ptr & 1) == 0 && self->size >= 16) {
        asm ("xor %%rax, %%rax\n\t"
             "movw (%[pybuf_ptr]), %%ax\n\t"
             "bswap %%rax\n\t"
             "add $2, %[pybuf_ptr]\n\t"
             : [bitbuf] "=a"(bitbuf),
               [pybuf_ptr] "+D"(pybuf_ptr));
        self->bitcount = 16;
        self->size -= 16;
    } else if (self->size) {
        asm ("xor %%rax, %%rax\n\t"
             "movb (%[pybuf_ptr]), %%al\n\t"
             "bswap %%rax\n\t"
             "add $1, %[pybuf_ptr]\n\t"
             : [bitbuf] "=a"(bitbuf),
               [pybuf_ptr] "+D"(pybuf_ptr));
        self->bitcount = MIN(self->size, 8);
        self->size -= MIN(self->size, 8);
    } else {
        return -1;
    }

    self->bitbuf = bitbuf;
    self->pybuf_ptr = (unsigned char *)pybuf_ptr;
    return 0;
}


static unsigned long
getbits(BitStrm *self, int num_bits)
{
    unsigned long bits = 0;
    unsigned long bitbuf = self->bitbuf;
    unsigned char nbits = (unsigned char)num_bits;

    asm ("shld %%cl, %[bitbuf], %[bits]\n\t"
         "shl %%cl, %[bitbuf]\n\t"
         : [bits] "+r"(bits), [bitbuf] "+r"(bitbuf)
         : [nbits] "c"(nbits));

    self->bitbuf = bitbuf;
    self->bitcount -= num_bits;
    return bits;
}


static PyObject *
BitStrm_getbits(BitStrm *self, PyObject *args)
{
    PyObject *result;
    unsigned long bits = 0;
    int num_bits;

    if (!PyArg_ParseTuple(args, "i", &num_bits)) {
        return NULL;
    }

    if (num_bits > (8 * sizeof(bits))) {
        char excstr[128];
        sprintf(excstr, "%d bits exceeds maximum bit size (%ld)",
                num_bits, 8*sizeof(bits));
        PyErr_SetString(PyExc_ValueError, excstr);
        return NULL;
    }

    while (num_bits != 0) {
        if (num_bits > self->bitcount) {
            num_bits -= self->bitcount;
            bits <<= self->bitcount;
            bits |= getbits(self, self->bitcount);
            if (reload_bitbuf(self) == -1) {
                PyErr_SetString(PyExc_RuntimeError,
                                "BitStrm buffer ran out of bits");
                return NULL;
            }
        } else {
            bits <<= num_bits;
            bits |= getbits(self, num_bits);
            num_bits = 0;
        }
    }
    
    result = PyLong_FromUnsignedLong(bits);
    return result;
}


static PyMethodDef BitStrm_methods[] = {
    {"getbits", (PyCFunction)BitStrm_getbits, METH_VARARGS,
     "Get the next N bits from the BitStrm, where N is the getbits argument"
    },
    {NULL}
};


static PyTypeObject BitStrmType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "bs.BitStrm",                /* tp_name */
    sizeof(BitStrm),             /* tp_basicsize */
    0,                           /* tp_itemsize */
    (destructor)BitStrm_dealloc, /* tp_dealloc */
    0,                           /* tp_print */
    0,                           /* tp_getattr */
    0,                           /* tp_setattr */
    0,                           /* tp_reserved */
    0,                           /* tp_repr */
    0,                           /* tp_as_number */
    0,                           /* tp_as_sequence */
    0,                           /* tp_as_mapping */
    PyObject_HashNotImplemented, /* tp_hash  */
    0,                           /* tp_call */
    0,                           /* tp_str */
    0,                           /* tp_getattro */
    0,                           /* tp_setattro */
    0,                           /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,          /* tp_flags */
    "BitStrm objects",           /* tp_doc */
    0,		                 /* tp_traverse */
    0,		                 /* tp_clear */
    0,		                 /* tp_richcompare */
    0,		                 /* tp_weaklistoffset */
    0,		                 /* tp_iter */
    0,		                 /* tp_iternext */
    BitStrm_methods,             /* tp_methods */
    0,                           /* tp_members */
    0,                           /* tp_getset */
    0,                           /* tp_base */
    0,                           /* tp_dict */
    0,                           /* tp_descr_get */
    0,                           /* tp_descr_set */
    0,                           /* tp_dictoffset */
    (initproc)BitStrm_init,      /* tp_init */
    0,                           /* tp_alloc */
    (newfunc)BitStrm_new,        /* tp_new */
};


static PyModuleDef bsmodule = {
    PyModuleDef_HEAD_INIT,
    "bs",
    "Bit stream module for Python.",
    -1,
    NULL, NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC
PyInit_bs(void)
{
    PyObject *m;

    BitStrmType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&BitStrmType) < 0) {
        return NULL;
    }

    m = PyModule_Create(&bsmodule);
    if (m == NULL) {
        return NULL;
    }

    Py_INCREF(&BitStrmType);
    PyModule_AddObject(m, "BitStrm", (PyObject *)&BitStrmType);
    return m;
}

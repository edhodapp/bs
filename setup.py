from distutils.core import setup, Extension

module1 = Extension('bs', sources = ['bs.c'])

setup(name='BitStrm',
      version='1.0',
      description='Bit streams for Python.',
      ext_modules=[module1])

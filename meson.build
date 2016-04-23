project('unzip', 'c')

zdep = dependency('zlib')
#bzdep = dependency('bz2')

add_global_arguments(
  '-DUSE_DEFLATE64=1',
  '-DNO_EXCEPT_SIGNALS=1',
  language : 'c')


unziplib = static_library('unziplib',
  'crc32.c',
  'crypt.c',
  'envargs.c',
  'explode.c',
  'extract.c',
  'fileio.c',
  'globals.c',
  'inflate.c',
  'list.c',
  'match.c',
  'process.c',
  'ttyio.c',
  'ubz2err.c',
  'unreduce.c',
  'unshrink.c',
  'zipinfo.c',
  'unix/unix.c',
)

unzip = executable('unzip', 'unzip.c',
  link_with : unziplib)

uzexe = '@0@/unzip'.format(meson.build_root())
testzip = '@0@/testmake.zip'.format(meson.source_root())

test('unzipping', find_program('unziptester.py'), args : [uzexe, testzip])

  
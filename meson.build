project('unzip', 'c')

zdep = dependency('zlib')
#bzdep = dependency('bz2')

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

file(READ "${CONFIG_HEADER}" _config)
string(REPLACE "#define HAVE_MMAP 1" "#define HAVE_MMAP 0" _config
    "${_config}")
string(REPLACE "#define HAVE_SYSCTL 1" "#define HAVE_SYSCTL 0" _config
    "${_config}")
string(REPLACE "#define HAVE_SYSCONF 1" "#define HAVE_SYSCONF 0" _config
    "${_config}")
string(REPLACE "#define HAVE_POSIX_MEMALIGN 1"
    "#define HAVE_POSIX_MEMALIGN 0" _config "${_config}")
string(REPLACE "#define HAVE_GETENTROPY 1" "#define HAVE_GETENTROPY 0"
    _config "${_config}")
string(REPLACE "#define HAVE_ARC4RANDOM_BUF 1"
    "#define HAVE_ARC4RANDOM_BUF 0" _config "${_config}")
file(WRITE "${CONFIG_HEADER}" "${_config}")

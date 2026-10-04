sigfm_opencv_dep = dependency(
    'opencv4',
    required: true,
    version: '>=4.5.0',
)

libsigfm = static_library(
    'sigfm',
    [
        'sigfm.cpp',
    ],
    dependencies: [
        sigfm_opencv_dep,
    ],
    pic: true,
    install: false,
)

sigfm_dep = declare_dependency(
    link_with: libsigfm,
    dependencies: [
        sigfm_opencv_dep,
    ],
    include_directories: include_directories('.'),
)

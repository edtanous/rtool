project('backtrace',
    'cpp',
    license : 'BSD-3Clause'
)

mod = import('unstable-external_project')

p = mod.add_project('configure',
  configure_options : [
                       '--prefix=@PREFIX@',
                       '--libdir=@PREFIX@/@LIBDIR@',
                       '--includedir=@PREFIX@/@INCLUDEDIR@',
                      ],
)

backtrace_dep = p.dependency('backtrace')
meson.override_dependency('backtrace', backtrace_dep)
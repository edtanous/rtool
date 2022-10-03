project('rtool', 'cpp',
        version : '1.0',
        meson_version: '>=0.57.0',
        default_options: [
            'b_lto_mode=default',
            'b_lto_threads=0',
            'b_lto=true',
            'b_ndebug=if-release',
            'cpp_rtti=false',
            'cpp_std=c++20',
            'warning_level=3',
            'werror=true',
           ])

# Project related links
project_pretty_name = 'rtool'
project_url = 'https://github.com/edtanous/rtool'
project_issues_url = project_url + '/issues/new'

summary('Issues',
  project_issues_url,
  section: 'Report Issues'
)

# Validate the c++ Standard
if get_option('cpp_std') != 'c++20'
    error('This project requires c++20 support')
endif

cxx = meson.get_compiler('cpp')
add_project_arguments(
  cxx.get_supported_arguments([
    '-Wno-non-virtual-dtor'
  ]),
  language: 'cpp'
)

rtool_dependencies = []

# Boost configuration
add_global_arguments(
  # Use no libraries
  '-DBOOST_ALL_NO_LIB',

  # remove some warnings
  '-DBOOST_ASIO_NO_DEPRECATED',
  '-DBOOST_BEAST_NO_DEPRECATED',

  # Separately compile asio elements to reduce intermediate compile times
  '-DBOOST_ASIO_SEPARATE_COMPILATION',
  '-DBOOST_BEAST_SEPARATE_COMPILATION',

  # Prefer std::string_view in beast APIs
  '-DBOOST_BEAST_USE_STD_STRING_VIEW',

  # Make the build reproducible by removing filename logging
  '-DBOOST_URL_NO_SOURCE_LOCATION',
  '-DOPENSSL_NO_FILENAMES',
  language : 'cpp'
)

threads = dependency('threads')
rtool_dependencies += threads

cli11 = dependency('cli11', version: '>=2.2.0', required : false, include_type: 'system')
if not cli11.found()
  cli11 = subproject('cli11', required: true).get_variable('CLI11_dep')
endif
rtool_dependencies += cli11

fmt = dependency('fmt', version: '>=9.0.0', required : false, include_type: 'system')
if not cli11.found()
  fmt = subproject('fmt', required: true).get_variable('fmt_dep')
endif
rtool_dependencies += fmt

openssl = dependency('openssl', version: '>=3.0', required : false, include_type: 'system')
if not openssl.found()
  openssl = subproject('openssl', required: true)
endif
rtool_dependencies += openssl


boost = dependency('boost', version : '>=1.80.0', required : false, include_type: 'system')
if not boost.found()
  boost = subproject('boost', required: true).get_variable('boost_dep')
endif
rtool_dependencies += boost

if cxx.has_header('boost/url/url_view.hpp')
  boost_url = declare_dependency()
else
  boost_url = subproject('boost-url', required: true).get_variable('boost_url_dep')
endif
rtool_dependencies += boost_url

summary(
  {
    'prefix' : get_option('prefix'),
  },
  section : 'Directories'
)

# Source files
srcfiles_rtool= [
  'src/boost_asio_ssl.cpp',
  'src/boost_asio.cpp',
  'src/boost_beast.cpp',
  'src/http_client.cpp',
  'src/rtool.cpp',
]

# Generate the rtool executable
executable(
  'rtool',
  srcfiles_rtool,
  dependencies: rtool_dependencies,
  install: true,
)


project('rtool', 'cpp',
        version : '1.0',
        meson_version: '>=1.2.0',
        default_options: [
            'b_lto_mode=default',
            'b_lto_threads=0',
            'b_pie=false',
            'cpp_rtti=false',
            'cpp_std=c++20',
            'warning_level=3',
            'werror=false',
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

if cxx.get_id() == 'msvc'
	add_global_arguments('/wd4133', language : ['c', 'cpp'])
  add_global_arguments('/D_ITERATOR_DEBUG_LEVEL=0', language: ['c', 'cpp'])
  # Windows 10
  add_global_arguments('/D_WIN32_WINNT=0x0A00 ', language : ['c', 'cpp'])
endif
if cxx.get_id() == 'gcc'
  if (cxx.version().version_compare('<13.0'))
    error('This project requires gcc-13 or higher')
  endif
  add_project_arguments('-Wno-non-virtual-dtor', language: ['c', 'cpp'])
endif

rtool_dependencies = []

# Boost configuration
add_global_arguments(
  # Use no libraries
  '-DBOOST_ALL_NO_LIB',

  # remove some warnings
  '-DBOOST_ASIO_NO_DEPRECATED',
  '-DBOOST_BEAST_NO_DEPRECATED',

  # Make the build reproducible by removing filename logging
  '-DBOOST_URL_NO_SOURCE_LOCATION',
  '-DOPENSSL_NO_FILENAMES',

  # Enable boost spirit options for debug
  '-DBOOST_SPIRIT_X3_NO_RTTI',
  '-DBOOST_SPIRIT_DEBUG',
  '-DBOOST_SPIRIT_DEBUG_OUT',

  # Spdlog
  '-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG',
  '-DSPDLOG_USE_STD_FORMAT',

  '-DBOOST_STACKTRACE_LINK',
  #'-DBOOST_STACKTRACE_USE_ADDR2LINE',
  '-DBOOST_STACKTRACE_USE_BACKTRACE',
  language : 'cpp'
)

add_global_arguments('-g', '-fno-inline', language: 'cpp')
link_args = '-fno-pie'

# Threads
#rtool_dependencies += dependency('threads')

# CLI11
rtool_dependencies += dependency('cli11', version: '>=2.2.0',  include_type: 'system', default_options:  ['default_library=static'])

# openssl
rtool_dependencies += dependency('openssl', version: '>=3.0', include_type: 'system', default_options:  ['default_library=static'])

rtool_dependencies += dependency('spdlog', include_type: 'system', default_options:  ['default_library=static'])

rtool_dependencies += dependency('backtrace', required: true)
#rtool_dependencies += dependency('dl', required: true)

boost = dependency(
  'boost',
  modules: [
    'url',
    'json',
    ],
  version : '>=1.84.0',
  required : false,
  include_type: 'system'
)
if boost.found()
  rtool_dependencies += [boost]
else
  cmake = import('cmake')
  opt = cmake.subproject_options()

  opt.add_cmake_defines({
    'BOOST_INCLUDE_LIBRARIES': 'asio;beast;fusion;json;headers;url;stacktrace;spirit',
    'BOOST_EXCLUDE_LIBRARIES': 'context;coroutine',
    'BUILD_SHARED_LIBS': 'OFF',
    'CMAKE_CXX_FLAGS': '-no-pie',
    'CMAKE_BUILD_TYPE': get_option('debug') ? 'Debug' : 'Release',
  })

  boost = cmake.subproject('boost', required: true, options: opt)
  message('Boost targets:\n - ' + '\n - '.join(boost.target_list()))

  boost_asio = boost.dependency('boost_asio').as_system()
  boost_beast = boost.dependency('boost_beast').as_system()
  boost_fusion = boost.dependency('boost_fusion').as_system()
  boost_headers = boost.dependency('boost_headers').as_system()
  boost_stacktrace = boost.dependency('boost_stacktrace_backtrace').as_system()

  boost_json = boost.dependency('boost_json').as_system()
  boost_spirit = boost.dependency('boost_spirit').as_system()
  boost_url = boost.dependency('boost_url').as_system()

  rtool_dependencies += [
    boost_asio,
    boost_beast,
    boost_fusion,
    boost_headers,
    boost_stacktrace,
    boost_json,
    boost_spirit,
    boost_url,
  ]
endif

# Source files
srcfiles_rtool= [
  'src/http_client.cpp',
  'src/path_parser.cpp',
  'src/path_parser_ast.cpp',
]

rtoollib = static_library(
  'rtoollib',
  srcfiles_rtool,
  dependencies: rtool_dependencies,
  link_args: link_args,
)

# Generate the rtool executable
executable(
  'rtool',
  ['src/rtool.cpp'] + srcfiles_rtool,
  link_with: rtoollib,
  dependencies: rtool_dependencies,
  install: true,
  link_args: link_args,
)

if(get_option('tests').enabled())
  gtest = dependency('gtest', main: true,disabler: true, required : false)
  gmock = dependency('gmock', required : false)
  if not gtest.found() and get_option('tests').enabled()
    gtest_proj = subproject('gtest', required: true)
    gtest = gtest_proj.get_variable('gtest_main_dep')
    gmock = gtest_proj.get_variable('gmock_dep')
  endif
  gtest = gtest.as_system('system')
  gmock = gmock.as_system('system')
  test_bin = executable(
    'path_parser_test',
    'src/path_parser_test.cpp',
    link_with: rtoollib,
    dependencies: [
      rtool_dependencies,
      gtest,
      gmock,
    ],
    link_args: link_args,
  )
  test('path_parser', test_bin)
endif

## Copyright (C) 2024 by LH_Mouse <lh_mouse@126.com>
##
## Permission to use, copy, modify, and/or distribute this
## software for any purpose with or without fee is hereby granted.
##
## THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
## WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
## WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
## THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
## CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
## LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
## NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
## CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

project('k32',
        'cpp',
        version: '0.0.internal',
        license: 'BSD-3-Clause',
        default_options: [ 'cpp_std=c++17', 'warning_level=2' ],
        meson_version: '>= 1.3.0')

#===========================================================
# Configuration
#===========================================================
cxx = meson.get_compiler('cpp')
cxx_is_i386 = cxx.has_define('__i386__')
cxx_is_amd64 = cxx.has_define('__amd64__')

add_project_arguments(
    '-Winvalid-pch', '-D_FILE_OFFSET_BITS=64', '-D_POSIX_C_SOURCE=200809',
    '-D_DEFAULT_SOURCE', '-D_DARWIN_C_SOURCE',
    cxx.get_supported_arguments(
      '-fno-elide-constructors', '-fvisibility-inlines-hidden', '-fstrict-aliasing',
      '-fno-stack-protector', '-fstrict-overflow', '-fmerge-all-constants',
      '-frounding-math', '-fno-math-errno', '-fno-ident', '-fno-trapping-math',
      '-fno-devirtualize-speculatively', '-fno-semantic-interposition', '-fno-plt',
      '-fno-omit-frame-pointer', '-momit-leaf-frame-pointer', '-masm=intel',
      '-mno-outline-atomics', '-Wno-option-ignored',
      '-Werror=conversion', '-Werror=sign-compare', '-Werror=sign-conversion',
      '-Werror=write-strings', '-Werror=return-type', '-Werror=double-promotion',
      '-Werror=non-virtual-dtor', '-Wzero-as-null-pointer-constant',
      '-Wmissing-declarations', '-Wmissing-field-initializers', '-Wshadow',
      '-Wsuggest-attribute=noreturn', '-Wswitch-enum', '-Wunused-function',
      '-Wunused-label', '-Wunused-local-typedefs', '-Wunused-but-set-variable',
      '-Wunused-but-set-parameter', '-Wno-array-bounds', '-Wno-stringop-overflow',
      '-Wno-redundant-move', '-Wno-unqualified-std-cast-call', '-Wno-string-plus-int',
      '-Wno-bitwise-instead-of-logical', '-Wno-missing-braces'),
    language: 'cpp')

if get_option('enable-avx') and (cxx_is_i386 or cxx_is_amd64)
  add_project_arguments('-mavx', language: 'cpp')
endif

if get_option('enable-debug-checks')
  add_project_arguments('-D_GLIBCXX_DEBUG', '-D_LIBCPP_DEBUG', language: 'cpp')
endif

dep_openssl = dependency('openssl')
dep_poseidon = dependency('poseidon')
dep_asteria = dependency('asteria')
dep_taxon = dependency('taxon')

#===========================================================
# Modules
#===========================================================
k32_common = shared_library('k32_common',
    cpp_pch: 'k32/xprecompiled.hpp',
    sources: [
      'k32/common/service.cpp', 'k32/common/remote_service_information.cpp',
      'k32/common/service_future.cpp', 'k32/common/service_response.cpp',
      'k32/common/clock.cpp', 'k32/common/http_future.cpp', 'k32/common/http_requestor.cpp' ],
    soversion: meson.project_version().split('.')[0],
    dependencies: [ dep_openssl, dep_poseidon, dep_asteria, dep_taxon ],
    install: true)

shared_module('k32_agent',
    cpp_pch: 'k32/xprecompiled.hpp',
    sources: [
      'k32/agent/globals.cpp', 'k32/agent/user_information.cpp',
      'k32/agent/user_service.cpp' ],
    dependencies: [ dep_poseidon, dep_taxon ],
    link_with: k32_common,
    install: true)

shared_module('k32_logic',
    cpp_pch: 'k32/xprecompiled.hpp',
    sources: [
      'k32/logic/globals.cpp' ],
    dependencies: [ dep_poseidon, dep_taxon ],
    link_with: k32_common,
    install: true)

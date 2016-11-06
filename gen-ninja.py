#! /usr/bin/env python
# -*- mode: python; coding: utf-8 -*-
# Copyright 2016 Peter Williams and collaborators.
# Licensed under the MIT License.

from __future__ import absolute_import, division, print_function, unicode_literals

import os, sys
from pwkit import io, ninja_syntax


config = {
    'base_cflags': '-g -O0',
    # pkg-config --cflags fontconfig harfbuzz harfbuzz-icu freetype2 graphite2 libpng zlib icu-uc poppler
    'pkgconfig_cflags': '-I/usr/include/freetype2 -I/usr/include/libpng16 -I/usr/include/harfbuzz -I/usr/include/glib-2.0 -I/usr/lib64/glib-2.0/include -I/usr/include/freetype2 -I/usr/include/libpng16 -I/usr/include/poppler',
    'pkgconfig_libs': '-lfontconfig -lharfbuzz-icu -lharfbuzz -lfreetype -lgraphite2 -lpng16 -lz -licuuc -licudata -lpoppler',
}


def inner (top, w):
    # build.ninja gen rule.

    w.comment ('Automatically generated.')

    w.rule ('regen',
            command='./gen-ninja.py $in',
            description='GEN $out',
            generator=True)
    w.build ('build.ninja', 'regen', implicit='gen-ninja.py')

    # Base rules

    w.rule ('ensuredir',
            command='mkdir -p $out',
            description='MKDIR $out')

    w.rule ('cc',
            command='gcc -c -o $out -MT $out -MD -MP -MF $out.d $cflags $in',
            deps='gcc',
            depfile='$out.d',
            description='CC $out')

    w.rule ('cxx',
            command='g++ -c -o $out -MT $out -MD -MP -MF $out.d $cflags $in',
            description='CXX $out')

    w.rule ('staticlib',
            command='ar cru $out $in',
            description='AR $out')

    w.rule ('executable',
            command='g++ -o $out $in $libs',
            description='LINK $out')

    w.rule ('tie',
            command='WEBINPUTS=. BUILD/tie -c $out $in',
            description='TIE $out')

    w.rule ('otangle',
            command='WEBINPUTS=. BUILD/otangle $in && mv $basename.p $basename.pool $outdir',
            description='OTANGLE $out')

    # build dir

    builddir = top / 'BUILD'
    w.build (str(builddir), 'ensuredir')

    # kpathsea

    libkp = builddir / 'libkpathsea.a'
    cflags = '-DHAVE_CONFIG_H -DMAKE_KPSE_DLL -Ikpathsea -I. %(base_cflags)s' % config
    objs = []

    for src in (top / 'kpathsea').glob ('*.c'):
        obj = builddir / ('kpathsea_' + src.name.replace ('.c', '.o'))
        w.build (
            str(obj), 'cc',
            inputs = [str(src)],
            order_only = [str(builddir)],
            variables = {'cflags': cflags},
        )
        objs.append (str (obj))

    w.build (str(libkp), 'staticlib', inputs = objs)

    # teckit

    libtk = builddir / 'libteckit.a'
    cflags = '-DHAVE_CONFIG_H -Iteckit -DNDEBUG %(base_cflags)s' % config
    objs = []

    for src in (top / 'teckit').glob ('*.cpp'):
        obj = builddir / ('teckit_' + src.name.replace ('.cpp', '.o'))
        w.build (
            str(obj), 'cxx',
            inputs = [str(src)],
            order_only = [str(builddir)],
            variables = {'cflags': cflags},
        )
        objs.append (str (obj))

    w.build (str(libtk), 'staticlib', inputs = objs)

    # libmd5

    libmd5 = builddir / 'libmd5.a'
    cflags = '-DHAVE_CONFIG_H -Ilibmd5 %(base_cflags)s' % config
    objs = []

    for src in (top / 'libmd5').glob ('*.c'):
        obj = builddir / ('libmd5_' + src.name.replace ('.c', '.o'))
        w.build (
            str(obj), 'cc',
            inputs = [str(src)],
            order_only = [str(builddir)],
            variables = {'cflags': cflags},
        )
        objs.append (str (obj))

    w.build (str(libmd5), 'staticlib', inputs = objs)

    # lib / libbase

    libbase = builddir / 'libbase.a'
    cflags = '-DHAVE_CONFIG_H -Ilib -I. %(base_cflags)s' % config
    objs = []

    for src in (top / 'lib').glob ('*.c'):
        if src.name == 'texmfmp.c':
            continue # #included in xetexdir/xetexextra.c

        obj = builddir / ('baselib_' + src.name.replace ('.c', '.o'))
        w.build (
            str(obj), 'cc',
            inputs = [str(src)],
            order_only = [str(builddir)],
            variables = {'cflags': cflags},
        )
        objs.append (str (obj))

    w.build (str(libbase), 'staticlib', inputs = objs)

    # synctex

    libsynctex = builddir / 'libsynctex.a'
    cflags = '-DHAVE_CONFIG_H -Ixetexdir -I. -DU_STATIC_IMPLEMENTATION -D__SyncTeX__ -DSYNCTEX_ENGINE_H=\\"synctexdir/synctex-xetex.h\\" %(pkgconfig_cflags)s %(base_cflags)s' % config
    objs = []

    for src in (top / 'synctexdir').glob ('*.c'):
        obj = builddir / ('synctex_' + src.name.replace ('.c', '.o'))
        w.build (
            str(obj), 'cc',
            inputs = [str(src)],
            order_only = [str(builddir)],
            variables = {'cflags': cflags},
        )
        objs.append (str (obj))

    w.build (str(libsynctex), 'staticlib', inputs = objs)

    # tie

    cflags = '-DHAVE_CONFIG_H -DNOT_WEB2C -I. -Ixetexdir %(base_cflags)s' % config
    objs = []

    for src in (top / 'tiedir').glob ('*.c'):
        obj = builddir / ('tie_' + src.name.replace ('.c', '.o'))
        w.build (
            str(obj), 'cc',
            inputs = [str(src)],
            order_only = [str(builddir)],
            variables = {'cflags': cflags},
        )
        objs.append (str (obj))

    objs += map (str, [libbase, libkp])
    libs = ''
    tieprog = str(builddir / 'tie')

    w.build (tieprog, 'executable',
             inputs = objs,
             variables = {'libs': libs},
    )

    # otangle

    cflags = '-I. -Ilib -Ixetexdir %(base_cflags)s' % config
    objs = []

    for src in (top / 'otangle').glob ('*.c'):
        obj = builddir / ('otangle_' + src.name.replace ('.c', '.o'))
        w.build (
            str(obj), 'cc',
            inputs = [str(src)],
            order_only = [str(builddir)],
            variables = {'cflags': cflags},
        )
        objs.append (str (obj))

    objs += map (str, [libbase, libkp])
    libs = ''
    otangleprog = str(builddir / 'otangle')

    w.build (otangleprog, 'executable',
             inputs = objs,
             variables = {'libs': libs},
    )

    # "tie"d xetex.ch file. Not sure if the ordering of changefiles matters so
    # I'm being paranoid here and reproducing what the TeXLive build system
    # uses.

    xetex_ch = builddir / 'xetex.ch'

    w.build (str(xetex_ch), 'tie',
             inputs = map (str, [
                 top / 'xetexdir' / 'xetex.web',
                 top / 'xetexdir' / 'tex.ch0',
                 top / 'xetexdir' / 'tex.ch',
                 top / 'synctexdir' / 'synctex-xe-def.ch0',
                 top / 'synctexdir' / 'synctex-mem.ch0',
                 top / 'synctexdir' / 'synctex-e-mem.ch0',
                 top / 'synctexdir' / 'synctex-e-mem.ch1',
                 top / 'synctexdir' / 'synctex-rec.ch0',
                 top / 'synctexdir' / 'synctex-e-rec.ch0',
                 top / 'xetexdir' / 'xetex.ch',
                 top / 'synctexdir' / 'synctex-xe-rec.ch3',
                 top / 'xetexdir' / 'tex-binpool.ch',
             ]),
             order_only = [str(builddir), tieprog],
    )

    # "otangle"d Pascal source for XeTeX.

    xetex_p = builddir / 'xetex.p'
    xetex_pool = builddir / 'xetex.pool'

    w.build ([str(xetex_p), str(xetex_pool)], 'otangle',
             inputs = map (str, [
                 top / 'xetexdir' / 'xetex.web',
                 xetex_ch,
             ]),
             order_only = [str(builddir), otangleprog],
             variables = {
                 'basename': 'xetex',
                 'outdir': str(builddir),
             },
    )

    # xetex

    cflags = '-DHAVE_CONFIG_H -D__SyncTeX__ -Ixetexdir -I. -Ilibmd5 %(pkgconfig_cflags)s %(base_cflags)s' % config
    objs = []

    def xetex_c_sources ():
        for src in (top / 'xetexdir').glob ('*.c'):
            yield src
        for src in (top / 'xetexdir' / 'image').glob ('*.c'):
            yield src

    for src in xetex_c_sources ():
        obj = builddir / ('xetex_' + src.name.replace ('.c', '.o'))
        w.build (
            str(obj), 'cc',
            inputs = [str(src)],
            order_only = [str(builddir)],
            variables = {'cflags': cflags},
        )
        objs.append (str (obj))

    for src in (top / 'xetexdir').glob ('*.cpp'):
        obj = builddir / ('xetex_' + src.name.replace ('.cpp', '.o'))
        w.build (
            str(obj), 'cxx',
            inputs = [str(src)],
            order_only = [str(builddir)],
            variables = {'cflags': cflags},
        )
        objs.append (str (obj))

    objs += map (str, [libsynctex, libbase, libmd5, libtk, libkp])
    libs = '%(pkgconfig_libs)s -lz' % config

    w.build (str(builddir / 'xetex'), 'executable',
             inputs = objs,
             variables = {'libs': libs},
    )


def outer (args):
    top = io.Path ('')
    me = io.Path (sys.argv[0]).parent

    with (me / 'build.ninja').open ('wt') as f:
        w = ninja_syntax.Writer (f)
        inner (top, w)


if __name__ == '__main__':
    import sys
    outer (sys.argv[1:])

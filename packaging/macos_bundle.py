#!/usr/bin/env python3
"""Makes macOS programs and libraries self-contained (docs/MACOS.md): the libraries they load
from outside the system (the x86_64 Homebrew's, /usr/local) are copied into DIR/lib and loaded
through @rpath, rpaths become relative to each file, and the changed files are signed again (ad
hoc; an invalid signature stops a program on Apple silicon).

    python3 packaging/macos_bundle.py DIR FILE...

FILEs are Mach-O programs and libraries inside DIR; the libraries they use from inside DIR stay
where they are. A library is copied under the name it is loaded by (libvulkan.1.dylib, which the
GPU library also opens by that name). OTOOL, INSTALL_NAME_TOOL and CODESIGN override the tools;
an empty CODESIGN skips signing.
"""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

OTOOL = os.environ.get('OTOOL', 'otool')
INSTALL_NAME_TOOL = os.environ.get('INSTALL_NAME_TOOL', 'install_name_tool')
CODESIGN = os.environ.get('CODESIGN', 'codesign')
SYSTEM = ('/usr/lib/', '/System/')
DEPENDENCY_COMMANDS = {'LC_LOAD_DYLIB', 'LC_LOAD_WEAK_DYLIB', 'LC_REEXPORT_DYLIB',
                       'LC_LAZY_LOAD_DYLIB', 'LC_LOAD_UPWARD_DYLIB'}


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def load_commands(path):
    """Install name (None for a program), dependencies and rpaths of a Mach-O file."""
    ident, dependencies, rpaths, command = None, [], [], None
    for line in run(OTOOL, '-l', str(path)).splitlines():
        line = line.strip()
        if line.startswith('cmd '):
            command = line[4:]
            continue
        match = re.fullmatch(r'(name|path) (.*) \(offset \d+\)', line)
        if not match:
            continue
        value = match.group(2)
        if command == 'LC_ID_DYLIB':
            ident = value
        elif command in DEPENDENCY_COMMANDS and value not in dependencies:
            dependencies.append(value)
        elif command == 'LC_RPATH' and value not in rpaths:
            rpaths.append(value)
    return ident, dependencies, rpaths


def expand(path, loader_dir):
    """An rpath or dependency with @loader_path/@executable_path, from the loader's directory
    (the programs and the GPU library are in or under the program directory)."""
    for variable in ('@loader_path', '@executable_path'):
        if path == variable:
            return Path(loader_dir)
        if path.startswith(variable + '/'):
            return Path(loader_dir, path[len(variable) + 1:])
    return Path(path)


def resolve(dependency, origin, rpaths):
    """The file a dependency names, or None. origin: the original directory of the file that
    loads it; rpaths: (rpath, original directory of its file) of the loading chain."""
    if dependency.startswith('@rpath/'):
        leaf = dependency[len('@rpath/'):]
        for rpath, directory in rpaths:
            candidate = expand(rpath, directory) / leaf
            if candidate.is_file():
                return candidate.resolve()
        return None
    candidate = expand(dependency, origin)
    return candidate.resolve() if candidate.is_file() else None


def relative_rpath(target, directory):
    relative = os.path.relpath(target, directory)
    return '@loader_path' if relative == '.' else '@loader_path/' + relative


def bundle(root, files):
    lib = root / 'lib'
    lib.mkdir(exist_ok=True)
    copies = {}  # original library -> its copy in lib/
    names = {}   # file name in lib/ -> original library
    work = [(path, path.parent, []) for path in files]
    done = set()
    while work:
        path, origin, chain = work.pop(0)
        if path in done:
            continue
        done.add(path)
        ident, dependencies, rpaths = load_commands(path)
        chain = [(rpath, origin) for rpath in rpaths] + chain
        copied = path.parent == lib
        arguments, needed = [], set()
        for dependency in dependencies:
            if dependency.startswith(SYSTEM):
                continue
            real = resolve(dependency, origin, chain)
            if real is None:
                raise SystemExit(f'{path}: cannot find {dependency}')
            if root in real.parents and lib not in real.parents:
                # Ours (the GPU library): stays, found through an rpath to its directory.
                needed.add(real.parent)
                if dependency.startswith('@rpath/'):
                    work.append((real, real.parent, chain))
                    continue
                arguments += ['-change', dependency, '@rpath/' + real.name]
                work.append((real, real.parent, chain))
                continue
            if real not in copies:
                name = Path(dependency).name
                if name in names and names[name] != real:
                    raise SystemExit(f'two libraries named {name}: {names[name]} and {real}')
                copies[real] = lib / name
                names[name] = real
                shutil.copy2(real, copies[real])
                copies[real].chmod(0o755)
                work.append((copies[real], real.parent, chain))
            needed.add(lib)
            target = '@rpath/' + copies[real].name
            if dependency != target:
                arguments += ['-change', dependency, target]
        if copied:
            arguments += ['-id', '@rpath/' + path.name]
            needed.add(lib)
        # rpaths: relative ones that stay inside the bundle, plus the directories needed.
        keep = []
        for rpath in rpaths:
            target = expand(rpath, path.parent)
            if copied or not rpath.startswith('@') or root not in [target.resolve(), *target.resolve().parents]:
                arguments += ['-delete_rpath', rpath]
            else:
                keep.append(target.resolve())
        for directory in sorted(needed):
            if directory not in keep:
                arguments += ['-add_rpath', relative_rpath(directory, path.parent)]
        if arguments:
            run(INSTALL_NAME_TOOL, *arguments, str(path))
            if CODESIGN:
                run(CODESIGN, '--force', '--sign', '-', str(path))
    return copies


def check(root):
    """Every Mach-O file under root loads only system libraries and files of the bundle."""
    problems = []
    for path in sorted(root.rglob('*')):
        if not path.is_file() or path.is_symlink():
            continue
        with open(path, 'rb') as f:
            magic = f.read(4)
        if magic not in (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe'):
            continue
        ident, dependencies, rpaths = load_commands(path)
        for dependency in dependencies:
            if not dependency.startswith(SYSTEM + ('@rpath/', '@loader_path/', '@executable_path/')):
                problems.append(f'{path}: loads {dependency}')
        for rpath in rpaths:
            if not rpath.startswith('@'):
                problems.append(f'{path}: rpath {rpath}')
    if problems:
        raise SystemExit('\n'.join(problems))


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    root = Path(sys.argv[1]).resolve()
    files = [Path(f).resolve() for f in sys.argv[2:]]
    for path in files:
        if root not in path.parents:
            raise SystemExit(f'{path} is not inside {root}')
    copies = bundle(root, files)
    check(root)
    for original, copy in sorted(copies.items(), key=lambda item: item[1].name):
        print(f'{copy.relative_to(root)}  <-  {original}')


if __name__ == '__main__':
    main()

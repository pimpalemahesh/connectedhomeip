#!/usr/bin/env python
# Copyright (c) 2023 Project CHIP Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Wrapper script to run kotlinc command as an action with gn."""

import argparse
import json
import os
import subprocess
import sys

EXIT_SUCCESS = 0
EXIT_FAILURE = 1


def IsExecutable(path):
    """Returns whether file at |path| exists and is executable.

    Args:
      path: absolute or relative path to test.

    Returns:
      True if the file at |path| exists, False otherwise.
    """
    return os.path.isfile(path) and os.access(path, os.X_OK)


def FindCommand(command):
    """Looks up for |command| in PATH.

    Args:
      command: name of the command to lookup, if command is a relative or absolute
        path (i.e. contains some path separator) then only that path will be
        tested.

    Returns:
      Full path to command or None if the command was not found.

      On Windows, this respects the PATHEXT environment variable when the
      command name does not have an extension.
    """
    fpath, _ = os.path.split(command)
    if fpath:
        if IsExecutable(command):
            return command

    if sys.platform == 'win32':
        # On Windows, if the command does not have an extension, cmd.exe will
        # try all extensions from PATHEXT when resolving the full path.
        command, ext = os.path.splitext(command)
        if not ext:
            exts = os.environ.get('PATHEXT', '.exe').split(os.path.pathsep)
        else:
            exts = [ext]
    else:
        exts = ['']

    for path in os.environ.get('PATH', '').split(os.path.pathsep):
        for ext in exts:
            path = os.path.join(path, command) + ext
            if IsExecutable(path):
                return path

    return None


def FindChipRoot():
    """Find the CHIP repo root by walking up from cwd looking for third_party/java_deps."""
    cwd = os.getcwd()
    for _ in range(10):
        if cwd and os.path.isdir(os.path.join(cwd, 'third_party', 'java_deps')):
            return cwd
        parent = os.path.dirname(cwd)
        if parent == cwd:
            break
        cwd = parent
    return None


def FindKotlinc():
    """Find kotlinc: KOTLINC_PATH, KOTLINC_HOME/bin/kotlinc, PATH, or third_party/java_deps/kotlin_compiler/bin/kotlinc."""
    # 1. Explicit executable path
    path = os.environ.get('KOTLINC_PATH', '').strip()
    if path and IsExecutable(path):
        return path
    # 2. Kotlin home directory
    home = os.environ.get('KOTLINC_HOME', '').strip()
    if home:
        for name in ('bin/kotlinc', 'bin/kotlinc.bat'):
            p = os.path.join(home, name)
            if IsExecutable(p):
                return p
    # 3. PATH
    path = FindCommand('kotlinc')
    if path:
        return path
    # 4. Bundled under third_party/java_deps/kotlin_compiler (set up by set_up_java_deps.sh)
    root = FindChipRoot()
    if root:
        for subpath in ('third_party/java_deps/kotlin_compiler/bin/kotlinc',
                        'third_party/java_deps/kotlin_compiler/kotlinc/bin/kotlinc'):
            p = os.path.join(root, subpath)
            if IsExecutable(p):
                return p
    return None


def ReadBuildConfig(build_config):
    with open(build_config, 'r') as file:
        return json.load(file)


def ComputeClasspath(build_config_json):
    unique_jars = build_config_json['deps_info']['deps_jars']
    if sys.platform == 'win32':
        return ";".join(unique_jars)
    return ":".join(unique_jars)


def main():
    kotlin_path = FindKotlinc()
    if not kotlin_path:
        sys.stderr.write(
            'kotlinc: command not found\n'
            '  Install Kotlin and add kotlinc to PATH, e.g.:\n'
            '    sdk install kotlin   # or: https://kotlinlang.org/docs/command-line.html\n'
            '  Or set KOTLINC_HOME to your Kotlin install directory.\n'
            '  Or run from repo root: third_party/java_deps/set_up_java_deps.sh\n'
            '    (downloads Kotlin compiler into third_party/java_deps/kotlin_compiler)\n')
        sys.exit(EXIT_FAILURE)

    parser = argparse.ArgumentParser('Kotkinc runner')
    parser.add_argument(
        '--classdir',
        dest='classdir',
        required=True,
        help='Directory that will contain class files')
    parser.add_argument(
        '--outfile',
        dest='outfile',
        required=True,
        help='Output file containing a list of classes')
    parser.add_argument(
        '--build-config',
        dest='build_config',
        required=True,
        help='Build config')
    parser.add_argument(
        'rest', metavar='KOTLINC_ARGS', nargs='*', help='Argumets to pass to kotlinc')

    args = parser.parse_args()
    if not os.path.isdir(args.classdir):
        os.makedirs(args.classdir, exist_ok=True)

    build_config_json = ReadBuildConfig(args.build_config)
    classpath = ComputeClasspath(build_config_json)
    kotlin_args = [os.path.abspath(kotlin_path)]
    if classpath:
        kotlin_args += ["-classpath", classpath]

    kotlin_args += ["-J-Xms256m", "-J-Xmx4096m", "-J-XX:ReservedCodeCacheSize=225m",
                    "-J-XX:+UseCompressedOops"]

    # Remove -Werror to prevent warnings from failing build
    filtered_args = [arg for arg in args.rest if arg != "-Werror"]

    retcode = subprocess.check_call(kotlin_args + filtered_args)
    if retcode != EXIT_SUCCESS:
        return retcode

    with open(args.outfile, 'wt') as f:
        prefixlen = len(args.classdir) + 1
        for root, dirnames, filenames in os.walk(args.classdir):
            for filename in filenames:
                if filename.endswith('.class'):
                    f.write(os.path.join(root[prefixlen:], filename))
                    f.write('\n')

    return EXIT_SUCCESS


if __name__ == '__main__':
    sys.exit(main())


**Note:** at this point we only support Linux.

This installation documentation applies to the _packed_ version (a.k.a. a
standalong tarball). See also the `dev/pack` script and `MasterCMakeLists.txt`
file.

To generate csspp from its tarball, you need cmake and a C++ compiler
and a few other dependencies. We suggest to run the following commands:

    tar xf csspp-all_1.0.27.3~xenial.tar.gz
    sudo csspp/ubuntu-depdendencies.sh
    mkdir BUILD
    cd BUILD
    cmake -DCMAKE_BUILD_TYPE=Debug \
          -DCMAKE_PREFIX_PATH=../csspp/cmake/Modules/ \
              ../csspp
    make

The `ubuntu-depdendencies.sh` script will install all the necessary
dependencies.

The `CMAKE_BUILD_TYPE` can be set to `Release` as well.

The cmake command can also use a number of processors to run the make in
parallel:

    ... -DMAKEFLAGS=-j`nproc` ...

(Keep in mind that parallel compilation uses a lot of memory so you may want
to limit that number if you're low on RAM.)

The results will be under the "dist" directory. However, that is "installed"
meaning that it won't know how to find shared libraries (it would works under
cygwin since the DLL appears along the EXE, but I don't think it still compiles
on Win32). You may, however, immediately test the binary without installing
to a final destination with:

    BUILD/csspp/src/csspp --help

The dist directory is otherwise ready to be installed under `/usr`. Many files
are not really required, but that's up to you to decide how to handle that
part.


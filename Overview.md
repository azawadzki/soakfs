## About ##
soakfs provides the ability of remotely accessing SpiderOak by means of FUSE. Thanks to the local access, one is able to stack e.g. encfs on top of soakfs and browse the contents of remote disk without the need for downloading everything locally to access the data.

The library was sketched together in free time over a weekend so bugs are sure to exist. There is no caching, so long file access times are expected.

Currently known issues:
  * May not work with non-latin file names

## Setup ##

The main directory contains setup.sh which can be tweaked and used for setting up build environment.

The code is written in C++, using some C++11 features. Modern compiler and rebuilding all used libraries in C++11 mode are required. Tested with GCC 4.6.3, but should work with newer versions too.

Dependencies:
  * cpp-netlib (v0.9.4)
  * libjson (v.7)
  * Boost (tested with v1.50.0, but with no hard version dependency)
  * base-n (http://code.google.com/p/base-n/)


## Usage ##

Standard FUSE commands are available. Typical session starts with:
> ./soakfs path/to/mount\_point
You may unmount the file system with:
> fusermount -u path/to/mount\_point
# Flush + Flush
This repository contains several tools to perform **Flush+Flush** attacks (and related attacks).

The Flush+Flush attack only relies on the execution time of the flush instruction, which depends on whether data is cached or not.
Flush+Flush does not make any memory accesses, contrary to any other cache attack. Thus, it causes no cache misses at all and the
number of cache hits is reduced to a minimum due to the constant cache flushes. Therefore, Flush+Flush attacks are stealthy and
faster than existing cache attacks.

The "[Flush+Flush](https://scholar.google.at/citations?view_op=view_citation&hl=de&user=JmCg4uQAAAAJ&citation_for_view=JmCg4uQAAAAJ:l7t_Zn2s7bgC)" paper by Gruss, Maurice, Wagner, and Mangard will be published at DIMVA 2016.

## One note before starting

**Warning:** This code is provided as-is. You are responsible for protecting yourself, your property and data, and others from any risks caused by this code. This code may not detect vulnerabilities of your applications. This code is only for testing purposes. Use it only on test systems which contain no sensitive data.

The programs should work on x86-64 Intel CPUs with a recent Linux. Note that the code contains hardcoded addresses and thresholds that are specific to our test system. Please adapt these addresses and thresholds to your system.

## Getting started: calibration
Use the histogram tools in the histogram folder to find a thresholds distinguishing cache hits and cache misses based on different techniques:
* ff: using a Flush+Flush attack
* fr: using a Flush+Reload attack
* pp: using a Prime+Probe attack (in a side-channel scenario)
* ppc: using a Prime+Probe attack (in a covert channel scenario)

Each of these programs should print a histogram for cache hits and cache misses. Based on the histogram you can choose an appropriate value to distinguish cache hits and cache misses.

**Note:** In most programs I defined a constant MIN_CACHE_MISS_CYCLES. Change it based on your threshold, if necessary.

## Getting started: eavesdropping on something
You can eavesdrop on something (like function calls in a shared library to infer something like keypresses) using the different tools in the sc folder.
We assume you already know a suitable address, for instance from a [Cache Template Attack](https://github.com/IAIK/cache_template_attacks).

Run the spy like this:
```
cd sc
cd ff # or fr/pp
make
./spy /usr/lib/x86_64-linux-gnu/libgdk-3.so.0.1200.2 0x85ec0 # use a suitable address ;)
```
The tools will now print cache hits for key strokes. Note that these are our most simple ff/fr/pp spy tools.
To significantly reduce the noise you can monitor two adjacent addresses in the case of Flush+Flush or Prime+Probe.

## OpenSSL AES T-Table attack
This example requires a self-compiled OpenSSL library to enable it's T-Table-based AES implementation.
Place libcrypto.so or a symlink such that the programs use this shared library.

Determine the T-Table addresses and fix them in the source code.

Then run
```
cd aes
cd ff # or fr/pp
make
./spy
```
The spy tool triggers encryptions itself and can then trivially determine the upper 4 bits of each key byte after about 64 encryptions.

Of course, we know that OpenSSL does not use a T-Table-based AES implementation anymore. But you can use this tool to profile any (possibly closed-source) binary to find whether it contains a crypto algorithm which leaks key dependent information through the cache.


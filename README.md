# KOCL-Augmenting Operating Systems with OpenCL Accelerators

KOCL is a general purpose framework for accelerating Linux kernel software with OpenCL accelerators,
which is built by migrating the [KGPU](https://github.com/wbsun/kgpu) code to the OpenCL platforms.We further added the zero-copy 
scheme and the support of multiple devices for concurrent processing in KOCL to take advantage of the OpenCL standard. 


## How to use KOCL
1.We develop KOCL on Ubuntu 16.04 with Linux kernel 4.7, OpneCL 2.0 for Intel Platform and OpenCL 1.2 for Nvidia Platform.
In hardware, we use Intel i7-6700 CPU, Intel HD 530, Nvidia GTX 1080 Ti GPU and with 16GB Ram.

2.If you want to extend the platforms or devices, you should modify the `main.c helper.c and gpuops.c` file to fit your
system.

3.The default pinned memory size is 128MB, you can adjust the size in `kocl.h KOCL_BUF_SIZE`.

4.Test the kocl,
```
make
cd build/
sudo insmod kocl.ko
sudo insmod gaes_ecb.ko channel=1 
sudo ./helper -l `pwd`

/*Benchmark jhash*/
sudo insmod callgpu_async_jhash.ko channel=1

/*Benchmark gaes*/
sudo insmod testskcipher.ko
```
5. Test ecryptfs,
```
mkdir ~/crypt
cd gaes/ecryptfs_4.7_kocl/
sudo insmod ecryptfs.ko
sudo mount -t ecryptfs ~/crypt ~/crypt
sudo dd if=/dev/zero of=~/crypt/test1 bs=32M count=1
sudo umount ~/crypt

/*Remove kocl*/
sudo rmmod ecryptfs
sudo rmmod gaes_ecb
sudo rmmod kocl
```
Note: channel represent the target device you want to use. 


TeSheng Lin,<dslin1010@gmail.com>


 


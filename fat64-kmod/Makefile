obj-m := fat64.o
#fat64-y := nfs.o cache.o dir.o fatent.o file.o inode.o misc.o namei.o

fat64-y := nfs.o cache.o misc.o


all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

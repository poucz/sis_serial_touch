obj-m += sis_ser.o

all:
	#make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	make -C /media/data/kernel/linux-headers-4.9.51-pou M=$(PWD) modules
	scp sis_ser.ko 192.168.0.81:
	ssh 192.168.0.81 "sudo rmmod sis_ser && sudo insmod  sis_ser.ko"

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

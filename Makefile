obj-m += sis_ser.o
#obj-m += zk.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	#make -C /media/data/kernel/linux-source-4.9 M=$(PWD) modules
	#make -C /usr/src/linux-headers-4.9.0-0.bpo.4-amd64/ M=$(PWD) modules
	
	#make -C /home/pou/sis_serial_touch/linux-headers-4.9.0-0.bpo.4-amd64 M=$(PWD) modules
	
	#scp sis_ser.ko 192.168.0.81:
	#ssh 192.168.0.81 "sudo rmmod sis_ser && sudo insmod  sis_ser.ko"

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

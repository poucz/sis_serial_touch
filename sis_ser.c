/*
 * Touch Screen driver for SiS 9200 family I2C Touch panels
 *
 * Copyright (C) 2015 SiS, Inc.
 * Copyright (C) 2016 Nextfour Group
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */



#define MDEBUG(msg)  printk(KERN_ERR msg)


#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/serio.h>


#define DRIVER_DESC	"SiS touch controller driver over serial port"

#define SIS_CONTROLER_TYPE_1 0
#define SIS_I2C_CONTROLER_TYPE_2 1



MODULE_AUTHOR("Jan Opravil <jan.opravil@jopr.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");



/*
 * Constants.
 */
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! to usr/include/linux/serio.h
#define SERIO_SIS_TOUCH	0x45
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

#define SIS_MAX_CONTACT		6
#define SIS_PACKET_MAX_LENGTH	43
#define SIS_DATA_HEADER_OFFSET	5
#define SIS_CONTACT_DATA_OFFSET(id)	((6*id)+SIS_DATA_HEADER_OFFSET)
#define SIS_CONTACT_COUNT_OFFSET	41




/*
 * Per-Orb data.
 */

struct sis_touch {
	struct input_dev *dev;
	int data_cnt;
	unsigned char data[SIS_PACKET_MAX_LENGTH];
	char phys[32];
	bool pendown;
};


struct sis_contact_data{
	unsigned char status;
	unsigned char id;
	u16 x;
	u16 y;
};


static short controler_type = SIS_CONTROLER_TYPE_1;
static short ENABLE_DEBUG_MSG = 0;


module_param(controler_type, short, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(controler_type, "Type of controler.0-stary protokol,1-novy protokol");
module_param(ENABLE_DEBUG_MSG, short, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(ENABLE_DEBUG_MSG, "Debug message printed to dmesg");



/*
 * spaceorb_process_packet() decodes packets the driver receives from the
 * SpaceOrb.
 */

//Documentation/input/multi-touch-protocol.txt
//inspirace npar. drivers/input/touchscreen/egalax_ts.c
//asi pozij spise input_mt_slot a ne egalax_ts.priklad

static void sis_ser_process_packet(struct sis_touch *sis_touch)
{
	struct input_dev *dev = sis_touch->dev;
	unsigned char *data = sis_touch->data;
	unsigned char c = 0;
	int i;
	struct sis_contact_data *touch_data;
	int contact_cnt;

	if (sis_touch->data_cnt < 2) return;
	
	
	for (i = 0; i < sis_touch->data_cnt-1; i++){
		//unsigned char old_c=c;
		c += data[i];//Check sum
		//printk("Checksum(%d) :0x%x=0x%x^data:0x%x\n",i,c,old_c,data[i]);
		//printk("Checksum\t%d\t%d\t%d\t%d\n",i,c,old_c,data[i]);
	 }
	
	if (c!=data[sis_touch->data_cnt-1]){
		//printk("Checksum error :0x%x be:0x%x\n",c,data[sis_touch->data_cnt-1]);
		//return;
	}
		
	//printk("Status is : 0x%x\n",data[SIS_TOUCH_DATA_OFFSET]);
	
	contact_cnt=data[SIS_CONTACT_COUNT_OFFSET];
	
	/*
	printk("Contact cnt is : 0x%x\n",contact_cnt);
	
	for(i=0;i<SIS_MAX_CONTACT;i++){
		touch_data=(struct sis_contact_data *)(data+SIS_CONTACT_DATA_OFFSET(i));
		printk("Touch data1: status: 0x%x  id:%i coord: %i x %i \n",touch_data->status,touch_data->id,touch_data->x,touch_data->y);
	}*/
	
	
	for(i=0;i<SIS_MAX_CONTACT;i++){
		touch_data=(struct sis_contact_data *)(data+SIS_CONTACT_DATA_OFFSET(i));
		if(touch_data->id==0){
			if(ENABLE_DEBUG_MSG)printk("Touch data: status: 0x%x  id:%i coord: %i x %i \n",touch_data->status,touch_data->id,touch_data->x,touch_data->y);
			break;
		}
	}
	
	if(controler_type==SIS_I2C_CONTROLER_TYPE_2){
		if(sis_touch->pendown==false && touch_data->status==0x03){
			sis_touch->pendown=true;
			input_report_key(dev, BTN_TOUCH, sis_touch->pendown);
			if(ENABLE_DEBUG_MSG)printk("Pen is: 0x%x\n",sis_touch->pendown);
		}else if(touch_data->status==0x00){
			sis_touch->pendown=false;
			input_report_key(dev, BTN_TOUCH, sis_touch->pendown);
			if(ENABLE_DEBUG_MSG)printk("Pen is: 0x%x\n",sis_touch->pendown);
		}
	}else{//defaultne standardne:
		if(touch_data->status==0x02){
			sis_touch->pendown=!sis_touch->pendown;
			input_report_key(dev, BTN_TOUCH, sis_touch->pendown);
			if(ENABLE_DEBUG_MSG)printk("Pen is: 0x%x\n",sis_touch->pendown);
		}
	}
	
	input_report_abs(dev, ABS_X, touch_data->x);
	input_report_abs(dev, ABS_Y, touch_data->y);
	
	input_sync(dev);
}

static irqreturn_t sis_ser_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags)
{
	struct sis_touch* sis_touch = serio_get_drvdata(serio);


	//printk(KERN_INFO "Prislo : 0x%x, celkem mam %d",data,sis_touch->data_cnt);

	if(data!=0x02 && sis_touch->data_cnt == 0){//cekam na hlavicku
		printk(KERN_ERR "Data 0x%x is not coretct packet header.",data);
		return IRQ_HANDLED;
	}

	if(data!=0x05 && sis_touch->data_cnt == 1){
		printk(KERN_ERR "Data 0x%x is not coretct packet header2.",data);
		sis_touch->data_cnt =0;
		return IRQ_HANDLED;
	}


	if (sis_touch->data_cnt < SIS_PACKET_MAX_LENGTH)
		sis_touch->data[sis_touch->data_cnt++] = data & 0x7f;
	else
		printk(KERN_ERR "Full packet buffer:%i, packet len:%i, discart: 0x%x",sis_touch->data_cnt,SIS_PACKET_MAX_LENGTH,data);
		
		
		
	if (sis_touch->data_cnt == SIS_PACKET_MAX_LENGTH) {//mam cely paket
		//kontrola spravnosti hlavicky:
		if(sis_touch->data[0]!=0x02 || sis_touch->data[1]!=0x05){
			printk(KERN_ERR "Not SIS header.. 0x%x  0x%x",sis_touch->data[0],sis_touch->data[1]);
			sis_touch->data_cnt=0;
			return IRQ_HANDLED;
		}
		
		
		//MDEBUG("Zpracovavam paket\n");
		if (sis_touch->data_cnt) sis_ser_process_packet(sis_touch);
		sis_touch->data_cnt = 0;
	}
	
	return IRQ_HANDLED;
}

/*
 * spaceorb_disconnect() is the opposite of spaceorb_connect()
 */

static void sis_ser_disconnect(struct serio *serio)
{
	struct sis_touch* sis_touch = serio_get_drvdata(serio);

	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_unregister_device(sis_touch->dev);
	kfree(sis_touch);
}

/*
 * spaceorb_connect() is the routine that is called when someone adds a
 * new serio device that supports SpaceOrb/Avenger protocol and registers
 * it as an input device.
 */

static int sis_ser_connect(struct serio *serio, struct serio_driver *drv)
{
	struct sis_touch *sis_touch;
	struct input_dev *input_dev;
	int err = -ENOMEM;


MDEBUG ("Inzert pou driver 4");
printk (KERN_ALERT "Using sis protocol:%i",controler_type);

	sis_touch = kzalloc(sizeof(struct sis_touch), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!sis_touch || !input_dev)
		goto fail1;

	sis_touch->dev = input_dev;
	snprintf(sis_touch->phys, sizeof(sis_touch->phys), "%s/input0", serio->phys);

	input_dev->name = "SiS touch screen";
	input_dev->phys = sis_touch->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_SIS_TOUCH;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &serio->dev;

	//input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	
	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	input_set_capability(input_dev, EV_KEY, BTN_TOUCH);
	input_set_abs_params(input_dev, ABS_X, 0, 32767, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, 32767, 0, 0);



	serio_set_drvdata(serio, sis_touch);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	err = input_register_device(sis_touch->dev);
	if (err)
		goto fail3;

	return 0;

 fail3:	serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
 fail1:	input_free_device(input_dev);
	kfree(sis_touch);
	return err;
}


/*
 * The serio driver structure.
 */

static struct serio_device_id sis_ser_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_SIS_TOUCH,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, sis_ser_serio_ids);

static struct serio_driver sis_ser_drv = {
	.driver		= {
		.name	= "sis_ser",
	},
	.description	= DRIVER_DESC,
	.id_table	= sis_ser_serio_ids,
	.interrupt	= sis_ser_interrupt,
	.connect	= sis_ser_connect,
	.disconnect	= sis_ser_disconnect,
};

module_serio_driver(sis_ser_drv);




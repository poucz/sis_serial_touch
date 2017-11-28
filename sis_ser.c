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


MODULE_AUTHOR("Jan Opravil <jan.opravil@jopr.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Constants.
 */
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! to usr/include/linux/serio.h
#define SERIO_SIS_TOUCH	0x45
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!


#define SIS_PACKET_MAX_LENGTH	43

static int spaceorb_buttons[] = { BTN_TL, BTN_TR, BTN_Y, BTN_X, BTN_B, BTN_A };
static int spaceorb_axes[] = { ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ };

/*
 * Per-Orb data.
 */

struct sis_touch {
	struct input_dev *dev;
	int idx;
	unsigned char data[SIS_PACKET_MAX_LENGTH];
	char phys[32];
};

static unsigned char spaceorb_xor[] = "SpaceWare";

static unsigned char *spaceorb_errors[] = { "EEPROM storing 0 failed", "Receive queue overflow", "Transmit queue timeout",
		"Bad packet", "Power brown-out", "EEPROM checksum error", "Hardware fault" };

/*
 * spaceorb_process_packet() decodes packets the driver receives from the
 * SpaceOrb.
 */

static void sis_ser_process_packet(struct sis_touch *sis_touch)
{
	struct input_dev *dev = sis_touch->dev;
	unsigned char *data = sis_touch->data;
	unsigned char c = 0;
	int axes[6];
	int i;

	if (sis_touch->idx < 2) return;
	
	
	/*
	printk(KERN_INFO " data: %d",sis_touch->idx);
	for (i = 0; i < sis_touch->idx; i++) printk(" 0x%x",data[i]);
	printk("\n");
	*/ 
	
	for (i = 0; i < sis_touch->idx; i++) c ^= data[i];//Check sum
	
	printk("Checksum : 0x%x\n",c);
	if (c) return;

	switch (data[0]) {

		case 'R':				/* Reset packet */
			sis_touch->data[sis_touch->idx - 1] = 0;
			for (i = 1; i < sis_touch->idx && sis_touch->data[i] == ' '; i++);
			printk(KERN_INFO "input: %s [%s] is %s\n",
				 dev->name, sis_touch->data + i, sis_touch->phys);
			break;

		case 'D':				/* Ball + button data */
			if (sis_touch->idx != 12) return;
			for (i = 0; i < 9; i++) sis_touch->data[i+2] ^= spaceorb_xor[i];
			axes[0] = ( data[2]	 << 3) | (data[ 3] >> 4);
			axes[1] = ((data[3] & 0x0f) << 6) | (data[ 4] >> 1);
			axes[2] = ((data[4] & 0x01) << 9) | (data[ 5] << 2) | (data[4] >> 5);
			axes[3] = ((data[6] & 0x1f) << 5) | (data[ 7] >> 2);
			axes[4] = ((data[7] & 0x03) << 8) | (data[ 8] << 1) | (data[7] >> 6);
			axes[5] = ((data[9] & 0x3f) << 4) | (data[10] >> 3);
			for (i = 0; i < 6; i++)
				input_report_abs(dev, spaceorb_axes[i], axes[i] - ((axes[i] & 0x200) ? 1024 : 0));
			for (i = 0; i < 6; i++)
				input_report_key(dev, spaceorb_buttons[i], (data[1] >> i) & 1);
			break;

		case 'K':				/* Button data */
			if (sis_touch->idx != 5) return;
			for (i = 0; i < 6; i++)
				input_report_key(dev, spaceorb_buttons[i], (data[2] >> i) & 1);

			break;

		case 'E':				/* Error packet */
			if (sis_touch->idx != 4) return;
			printk(KERN_ERR "spaceorb: Device error. [ ");
			for (i = 0; i < 7; i++) if (data[1] & (1 << i)) printk("%s ", spaceorb_errors[i]);
			printk("]\n");
			break;
	}

	input_sync(dev);
}

static irqreturn_t sis_ser_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags)
{
	struct sis_touch* sis_touch = serio_get_drvdata(serio);


	printk(KERN_INFO "Prislo data: 0x%x, clekem mam %d",data,sis_touch->idx);

	if(data!=0x02 && sis_touch->idx == 0){//cekam na hlavicku
		printk(KERN_ERR "Data 0x%x is not coretct packet header.",data);
		return IRQ_HANDLED;
	}

	if (data==0x02 && sis_touch->idx == SIS_PACKET_MAX_LENGTH) {//prisla hlavicka zpracuji paket ktery mam v buferru
		MDEBUG("Hlavicka\n");
		if (sis_touch->idx) sis_ser_process_packet(sis_touch);
		sis_touch->idx = 0;
	}
	
	if (sis_touch->idx < SIS_PACKET_MAX_LENGTH)
		sis_touch->data[sis_touch->idx++] = data & 0x7f;
	else
		printk(KERN_ERR "Full packet buffer:%i, packet len:%i, discart: 0x%x",sis_touch->idx,SIS_PACKET_MAX_LENGTH,data);
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
	int i;


MDEBUG ("Inzert pou driver");


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

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	for (i = 0; i < 6; i++)
		set_bit(spaceorb_buttons[i], input_dev->keybit);

	for (i = 0; i < 6; i++)
		input_set_abs_params(input_dev, spaceorb_axes[i], -508, 508, 0, 0);

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



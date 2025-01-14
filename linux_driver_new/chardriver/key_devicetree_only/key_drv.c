﻿/*
 * Copyright (c) 2024 Zhang YiXu
 *
 * This code is for personal learning and open-source sharing purposes only.
 * Unauthorized use for commercial purposes is prohibited.
 *
 * Licensed under the MIT License. You may freely use, modify, and distribute
 * this code in compliance with the terms of the license.
 * 
 * Full text of the MIT License can be found at:
 * https://opensource.org/licenses/MIT
 *
 * Author: Zhang YiXu
 * Contact: zhangyixu02@gmail.com
 */

#include "linux/printk.h"
#include "linux/types.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/delay.h>


#define KBUF_MAX_SIZE      32
#define GPIO_NAME_MAX_LEN  128

struct irq_resource {
    unsigned int irq;
    struct irq_data * irq_data;
    u32 irq_type;
    struct tasklet_struct tasklet;
};


typedef struct chr_drv {
	int major;                 /*< Master device number */
	int minor;                 /*< Secondary device number */
	dev_t dev_num;             /*< Device number of type dev_t consisting of primary device number and secondary device number */
	struct cdev chr_cdev;      /*< Character device structure */
	struct class *chr_class;   /*< Device class */
    struct device *chr_device; /*< Device instance */
    struct irq_resource irq_res;
}chr_drv;

static chr_drv s_char_drv;

void key_tasklet_handler(unsigned long data)
{
    printk("mdelay before ;data: %ld\n",data);
    //msleep(100);
    mdelay(100);
    printk("mdelay after\n");
}

static irqreturn_t isr_test(int irq, void *dev_id)
{
    printk("isr_test before\n");
    tasklet_schedule(&s_char_drv.irq_res.tasklet);
    printk("isr_test after\n");
    return IRQ_HANDLED;
}

static int chrdev_open(struct inode *inode, struct file *file)
{
    int ret;
    file->private_data = &s_char_drv;

    ret = request_irq(s_char_drv.irq_res.irq,isr_test,s_char_drv.irq_res.irq_type,"my_isr_test",NULL);
    if (ret < 0) {
        printk("request_irq is error\n");
        return ret;
    }
	printk("%s line %d\n", __FUNCTION__, __LINE__);
	return 0;
}

static int chrdev_release(struct inode *inode, struct file *file)
{
    chr_drv *chrdev_private_data = (chr_drv *)file->private_data;
    free_irq(chrdev_private_data->irq_res.irq,NULL);
	printk("%s line %d\n", __FUNCTION__, __LINE__);
	return 0;
}

static struct file_operations chr_file_operations = {
	.owner = THIS_MODULE,
	.open = chrdev_open,
	.release = chrdev_release,
};

static int key_drver_probe(struct platform_device *pdev)
{
	int ret;
    struct device_node *np = pdev->dev.of_node;

	/*< 1. Request equipment number */
    ret = alloc_chrdev_region(&s_char_drv.dev_num,0,1,"chrdev_num");
    if (ret < 0) {
        printk("alloc_chrdev_region is error\n");
        goto err_chrdev;
    }
    printk("major is %d\n",MAJOR(s_char_drv.dev_num));
    printk("minor is %d\n",MINOR(s_char_drv.dev_num));
	/*< 2. Registered character device */
    cdev_init(&s_char_drv.chr_cdev,&chr_file_operations);
	s_char_drv.chr_cdev.owner = THIS_MODULE;
    ret = cdev_add(&s_char_drv.chr_cdev,s_char_drv.dev_num,1);
    if(ret < 0 ){
        printk("cdev_add is error\n");
        goto  err_chr_add;
    }
    printk("Registered character device is ok\n");
	/*< 3. Creating a Device Node */
    s_char_drv.chr_class = class_create(THIS_MODULE,"chr_class_key");
    if (IS_ERR(s_char_drv.chr_class)) {
        ret = PTR_ERR(s_char_drv.chr_device);
        printk("class_create failed with error code: %d\n", ret);
        goto err_class_create;
    }

    s_char_drv.chr_device = device_create(s_char_drv.chr_class,NULL,s_char_drv.dev_num,NULL,"chr_device_key");
    if (IS_ERR(s_char_drv.chr_device)) {
        ret = PTR_ERR(s_char_drv.chr_device);
        printk("device_create failed with error code: %d\n", ret);
        goto err_device_create;
    }
    printk("creating a device node is ok\n");
    /*< 4. Obtaining device resources */
    s_char_drv.irq_res.irq = irq_of_parse_and_map(np,0);
    if (s_char_drv.irq_res.irq == 0) {
        printk("irq_of_parse_and_map error\n");
        goto err_get_resource;
    }
    s_char_drv.irq_res.irq_data = irq_get_irq_data(s_char_drv.irq_res.irq);
    if (s_char_drv.irq_res.irq_data == NULL) {
        printk("irq_get_irq_data error\n");
        goto err_get_resource;
    }
    s_char_drv.irq_res.irq_type = irqd_get_trigger_type(s_char_drv.irq_res.irq_data);
    tasklet_init(&s_char_drv.irq_res.tasklet, key_tasklet_handler, 1);
    printk("Obtaining device resources is ok\n");

    return 0;

err_get_resource:
    device_destroy(s_char_drv.chr_class,s_char_drv.dev_num);
err_device_create:
    class_destroy(s_char_drv.chr_class);
err_class_create:
    cdev_del(&s_char_drv.chr_cdev);
err_chr_add:
    unregister_chrdev_region(s_char_drv.dev_num, 1);
err_chrdev:
    return ret;
}

static int key_drver_remove(struct platform_device *pdev)
{
    device_destroy(s_char_drv.chr_class,s_char_drv.dev_num);
    class_destroy(s_char_drv.chr_class);
    cdev_del(&s_char_drv.chr_cdev);
    unregister_chrdev_region(s_char_drv.dev_num,1);

    printk("module exit \n");
    return 0;
}

static const struct of_device_id dts_table[] = 
{
	{.compatible = "dts_key@1"},
    { /* Sentinel (end of array marker) */ }
};

static struct platform_driver key_driver = {
	.driver		= {
		.owner = THIS_MODULE,
        .name  = "key_driver",
		.of_match_table = dts_table,
	},
	.probe		= key_drver_probe,
	.remove		= key_drver_remove,
};

static int __init chr_drv_init(void)
{
	int ret = 0;
	ret = platform_driver_register(&key_driver);
	if(ret<0)
	{
		printk("platform_driver_register error \n");
		return ret;
	}
	printk("platform_driver_register ok \n");
    return 0;
}

static void __exit chr_drv_exit(void)
{
	platform_driver_unregister(&key_driver);
	printk("platform_driver_unregister ok \n");
}

module_init(chr_drv_init);
module_exit(chr_drv_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("CSDN:qq_63922192");
MODULE_DESCRIPTION("LED driver");
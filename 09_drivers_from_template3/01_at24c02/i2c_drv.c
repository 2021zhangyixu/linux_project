﻿/* 说明 ： 
 	*1，本代码是学习韦东山老师的驱动入门视频所写，增加了注释。
 	*2，采用的是UTF-8编码格式，如果注释是乱码，需要改一下。
 	*3，这是应用层代码
 * 作者 ： CSDN风正豪
*/

#include "asm/uaccess.h"
#include "linux/delay.h"
#include "linux/i2c.h"
#include <linux/module.h>
#include <linux/poll.h>

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/kmod.h>
#include <linux/gfp.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/timer.h>

/* 主设备号                                                                 */
static int major = 0;
static struct class *my_i2c_class;

static struct i2c_client *g_client;

static DECLARE_WAIT_QUEUE_HEAD(gpio_wait);
struct fasync_struct *i2c_fasync;


/* 实现对应的open/read/write等函数，填入file_operations结构体                   */
static ssize_t i2c_drv_read (struct file *file, char __user *buf, size_t size, loff_t *offset)
{
	int err;
	unsigned char *kern_buf;
	struct i2c_msg msgs[2];

	/* 从0读取size字节 */

	kern_buf = kmalloc(size, GFP_KERNEL);

	/* 初始化i2c_msg 
	 * 1. 发起一次写操作: 把0发给AT24C02, 表示要从0地址读数据
	 * 2. 发起一次读操作: 得到数据
	 */
	msgs[0].addr  = g_client->addr;
	msgs[0].flags = 0;  /* 写操作 */
	msgs[0].buf   = kern_buf;
	kern_buf[0]   = 0; /* 把数据0发给设备 */
	msgs[0].len   = 1;

	msgs[1].addr  = g_client->addr;
	msgs[1].flags = I2C_M_RD;  /* 写操作 */
	msgs[1].buf   = kern_buf;
	msgs[1].len   = size;

	err = i2c_transfer(g_client->adapter, msgs, 2);

	/* copy_to_user  */
	err = copy_to_user(buf, kern_buf, size);

	kfree(kern_buf);
	
	return size;
}

static ssize_t i2c_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
	int err;
	unsigned char kern_buf[9];
	struct i2c_msg msgs[1];
	int len;
	unsigned char addr = 0;

	/* 把size字节的数据写入地址0 */

	//kern_buf = kmalloc(size+1, GFP_KERNEL);

	while (size > 0)
	{
		if (size > 8)
			len = 8;
		else
			len = size;

		size -= len;

		/* copy_from_user  */
		err = copy_from_user(kern_buf+1, buf, len);
		buf += len;


		/* 初始化i2c_msg 
		* 1. 发起一次写操作: 把0发给AT24C02, 表示要从0地址读数据
		* 2. 发起一次读操作: 得到数据
		*/
		msgs[0].addr  = g_client->addr;
		msgs[0].flags = 0;  /* 写操作 */
		msgs[0].buf   = kern_buf;
		kern_buf[0]   = addr;  /* 写AT24C02的地址 */
		msgs[0].len   = len+1;
		addr += len;

		err = i2c_transfer(g_client->adapter, msgs, 1);

		mdelay(20);
	}

	//kfree(kern_buf);
	
	return size;    
}


static unsigned int i2c_drv_poll(struct file *fp, poll_table * wait)
{
	//printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	poll_wait(fp, &gpio_wait, wait);
	//return is_key_buf_empty() ? 0 : POLLIN | POLLRDNORM;
	return 0;
}

static int i2c_drv_fasync(int fd, struct file *file, int on)
{
	if (fasync_helper(fd, file, on, &i2c_fasync) >= 0)
		return 0;
	else
		return -EIO;
}


/* 定义自己的file_operations结构体                                              */
static struct file_operations i2c_drv_fops = {
	.owner	 = THIS_MODULE,
	.read    = i2c_drv_read,
	.write   = i2c_drv_write,
	.poll    = i2c_drv_poll,
	.fasync  = i2c_drv_fasync,
};


static int i2c_drv_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	// struct device_node *np = client->dev.of_node;
	// struct i2c_adapter *adapter = client->adapter;

	/* 记录client */
	g_client = client;

	/* 注册字符设备 */
	/* 注册file_operations 	*/
	major = register_chrdev(0, "100ask_i2c", &i2c_drv_fops);  /* /dev/gpio_desc */

	my_i2c_class = class_create(THIS_MODULE, "100ask_i2c_class");
	if (IS_ERR(my_i2c_class)) {
		printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
		unregister_chrdev(major, "100ask_i2c");
		return PTR_ERR(my_i2c_class);
	}

	device_create(my_i2c_class, NULL, MKDEV(major, 0), NULL, "myi2c"); /* /dev/myi2c */
	
	return 0;
}

static int i2c_drv_remove(struct i2c_client *client)
{
	/* 反注册字符设备 */
	device_destroy(my_i2c_class, MKDEV(major, 0));
	class_destroy(my_i2c_class);
	unregister_chrdev(major, "100ask_i2c");

	return 0;
}

static const struct of_device_id myi2c_dt_match[] = {
	{ .compatible = "100ask,i2cdev" },  //这个是通过设备树进行匹配设备
	{},
};

static const struct i2c_device_id at24c02_ids[] = {
	{ "xxxxyyy",	(kernel_ulong_t)NULL },
	{ /* END OF LIST */ }
};
static struct i2c_driver my_i2c_driver = {
	.driver = {
		   .name = "100ask_i2c_drv",   //根据这个名字，在c文件中找到设备
		   .owner = THIS_MODULE,
		   .of_match_table = myi2c_dt_match,
	},
	.probe = i2c_drv_probe,    //注册平台之后，内核如果发现支持某一个平台设备，这个函数就会被调用。入口函数
	.remove = i2c_drv_remove,  //出口函数
	.id_table = at24c02_ids,
};


static int __init i2c_drv_init(void)
{
	/* 注册i2c_driver */
	return i2c_add_driver(&my_i2c_driver);  //注意，这里是driver表示是驱动
}

static void __exit i2c_drv_exit(void)
{
	/* 反注册i2c_driver */
	i2c_del_driver(&my_i2c_driver);
}

/* 7. 其他完善：提供设备信息，自动创建设备节点                                     */

module_init(i2c_drv_init);  //确认入口函数
module_exit(i2c_drv_exit);  //确认出口函数

/*最后我们需要在驱动中加入 LICENSE 信息和作者信息，其中 LICENSE 是必须添加的，否则的话编译的时候会报错，作者信息可以添加也可以不添加
 *这个协议要求我们代码必须免费开源，Linux遵循GPL协议，他的源代码可以开放使用，那么你写的内核驱动程序也要遵循GPL协议才能使用内核函数
 *因为指定了这个协议，你的代码也需要开放给别人免费使用，同时可以根据这个协议要求很多厂商提供源代码
 *但是很多厂商为了规避这个协议，驱动源代码很简单，复杂的东西放在应用层
*/
MODULE_LICENSE("GPL"); //指定模块为GPL协议
MODULE_AUTHOR("CSDN:qq_63922192");  //表明作者，可以不写




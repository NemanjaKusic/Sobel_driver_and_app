#include <linux/dma-mapping.h>	// dma_alloc_coherent()
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/delay.h>      // usleep_range()
#include <linux/jiffies.h>    // jiffies, time_after(), msecs_to_jiffies()

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/device.h>

#include <linux/io.h> //iowrite ioread
#include <linux/slab.h>//kmalloc kfree
#include <linux/platform_device.h>//platform driver
#include <linux/ioport.h>//ioremap
#define BUFF_SIZE 20
#define BUFF_SIZE 20
#define DRIVER_NAME "sobel"

#define START_OFFSET 0
#define BASE_ADDR_OFFSET 4
#define READY_OFFSET 8

#define SOBEL_DMA_SIZE 1351088   // total bytes the IP needs

MODULE_LICENSE("Dual BSD/GPL");

struct sobel_info {
	unsigned long mem_start;	// start of AXI slave cntrl registers
	unsigned long mem_end;		// end of AXI slave cntrl registers
	void __iomem *base_addr;	// virtual addr of AXI slave cntrl resgisters
	void *dma_vaddr;		// CPU virtual addr of the DMA buffer region
	dma_addr_t dma_handle;		// physical address of the start of the DMA buffer region -> goes to base_address register
	size_t dma_size;		// DMA buffer region size
};

dev_t my_dev_id;
static struct class *my_class;
static struct device *my_device;
static struct cdev *my_cdev;
static struct sobel_info *lp = NULL;

int endRead = 0;


static int sobel_probe(struct platform_device *pdev);
static int sobel_remove(struct platform_device *pdev);
int sobel_open(struct inode *pinode, struct file *pfile);
int sobel_close(struct inode *pinode, struct file *pfile);
ssize_t sobel_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset);
ssize_t sobel_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset);
static int __init sobel_init(void);
static void __exit sobel_exit(void);

struct file_operations my_fops =
{
	.owner = THIS_MODULE,
	.open = sobel_open,
	.read = sobel_read,
	.write = sobel_write,
	.release = sobel_close,
};

static struct of_device_id sobel_of_match[] = {
	{ .compatible = "sobel", },
	{ /* end of list */ },
};

static struct platform_driver sobel_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= sobel_of_match,
	},
	.probe		= sobel_probe,
	.remove		= sobel_remove,
};


MODULE_DEVICE_TABLE(of, sobel_of_match);

static int sobel_probe(struct platform_device *pdev)
{
	struct resource *r_mem;
	int rc = 0;
	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r_mem) {
		printk(KERN_ALERT "Failed to get resource\n");
		return -ENODEV;
	}
	lp = (struct sobel_info *) kmalloc(sizeof(struct sobel_info), GFP_KERNEL);
	if (!lp) {
		printk(KERN_ALERT "Could not allocate sobel device\n");
		return -ENOMEM;
	}

	lp->mem_start = r_mem->start;
	lp->mem_end = r_mem->end;
	//printk(KERN_INFO "Start address:%x \t end address:%x", r_mem->start, r_mem->end);

	if (!request_mem_region(lp->mem_start,lp->mem_end - lp->mem_start + 1,	DRIVER_NAME))
	{
		printk(KERN_ALERT "Could not lock memory region at %p\n",(void *)lp->mem_start);
		rc = -EBUSY;
		goto error1;
	}

	lp->base_addr = ioremap(lp->mem_start, lp->mem_end - lp->mem_start + 1);
	if (!lp->base_addr) {
		printk(KERN_ALERT "Could not allocate memory\n");
		rc = -EIO;
		goto error2;
	}

	// DMA allocates the safe memory region inisde DDR
	lp->dma_size = SOBEL_DMA_SIZE;
	lp->dma_vaddr = dma_alloc_coherent(&pdev->dev, lp->dma_size, &lp->dma_handle, GFP_KERNEL);

	if (!lp->dma_vaddr) {
		printk(KERN_ERR "sobel: dma_alloc_coherent failed for %zu bytes\n", lp->dma_size);
		return -ENOMEM;
	}

	printk(KERN_INFO "sobel: DMA buffer allocated, size=%zu, phys=%pad, virt=%p\n", lp->dma_size, &lp->dma_handle, lp->dma_vaddr);

	//confirm the physical address is reachable by the HP port (must be below 0x20000000)
	if (lp->dma_handle >= 0x20000000) {
		printk(KERN_WARNING "sobel: WARNING - phys addr %pad may be outside HP0 range!\n", &lp->dma_handle);
	}

	printk(KERN_WARNING "sobel platform driver registered\n");
	return 0;//ALL OK

	error2:
		release_mem_region(lp->mem_start, lp->mem_end - lp->mem_start + 1);
	error1:
		return rc;
}

static int sobel_remove(struct platform_device *pdev)
{
	if (lp->dma_vaddr) {
		dma_free_coherent(&pdev->dev, lp->dma_size, lp->dma_vaddr, lp->dma_handle);
		printk(KERN_INFO "sobel: DMA buffer freed\n");
	}

	printk(KERN_WARNING "sobel platform driver removed\n");
	iowrite32(0, lp->base_addr);
	iounmap(lp->base_addr);
	release_mem_region(lp->mem_start, lp->mem_end - lp->mem_start + 1);
	kfree(lp);
	return 0;
}



int sobel_open(struct inode *pinode, struct file *pfile) 
{
	printk(KERN_INFO "Succesfully opened sobel\n");
	return 0;
}

int sobel_close(struct inode *pinode, struct file *pfile) 
{
	printk(KERN_INFO "Succesfully closed sobel\n");
	return 0;
}

ssize_t sobel_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset) 
{
	int ret;
	int len = 0;
	u32 ready_val = 0;
	char buff[BUFF_SIZE];
	if (endRead){
		endRead = 0;
		return 0;
	}

	ready_val = ioread32(lp->base_addr + READY_OFFSET);

	if((ready_val) & 0x01)
		buff[0] = '1';
	else
		buff[0] = '0';

	buff[1]= '\n';
	len=2;
	ret = copy_to_user(buffer, buff, len);
	if(ret)
		return -EFAULT;
	printk(KERN_INFO "Succesfully read\n");
	endRead = 1;

	return len;
}

ssize_t sobel_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset) 
{

	char buff[BUFF_SIZE];
	int ret = 0;
	u32 base_addr_val=0;
	//char *at;

	if (length >= BUFF_SIZE)
    		return -EINVAL;

	ret = copy_from_user(buff, buffer, length);
	if(ret)
		return -EFAULT;
	buff[length] = '\0';


	if (length > 0 && buff[length - 1] == '\n')
		buff[length - 1] = '\0';

	if (strcmp(buff, "start") == 0)
	{
		unsigned long timeout;
		u32 ready;

		// start IP
		base_addr_val = lp->dma_handle;
		iowrite32(base_addr_val, lp->base_addr + BASE_ADDR_OFFSET);
		printk(KERN_INFO "Sobel: succesfully wrote base address value %#x\n",base_addr_val);

                iowrite32(1, lp->base_addr + START_OFFSET);
                iowrite32(0, lp->base_addr + START_OFFSET);
                printk(KERN_INFO "Sobel: succesfully started IP\n");

		// Check if Sobel finished processing every 1~2ms
		// Wait until it reads 1 again.
		timeout = jiffies + msecs_to_jiffies(1000);  // 1 second max wait

		while (1)
		{
			ready = ioread32(lp->base_addr + READY_OFFSET) & 0x1;
			if (ready == 1)
				break;  // IP finished

			// 1 second timeout guard if IP gets stuck
			if (time_after(jiffies, timeout))
			{
				printk(KERN_ERR "Sobel: timeout waiting for IP to finish\n");
				return -ETIMEDOUT;
			}

		usleep_range(1000, 2000);  // sleep ~1-2ms, CPU free for other work
		}

		printk(KERN_INFO "Sobel: IP finished, output images ready\n");
	}
	else
	{
		printk(KERN_INFO "Sobel: wrong start command, should be 'start'\n");
	}

	//printk(KERN_INFO "Succesfully wrote into file\n");
	return length;
}

static int __init sobel_init(void)
{
	int ret = 0;

	//Initialize array

	ret = alloc_chrdev_region(&my_dev_id, 0, 1, DRIVER_NAME);
	if (ret){
		printk(KERN_ERR "failed to register char device\n");
		return ret;
	}
	printk(KERN_INFO "char device region allocated\n");

	my_class = class_create(THIS_MODULE, "sobel_class");
	if (my_class == NULL){
		printk(KERN_ERR "failed to create class\n");
		goto fail_0;
	}
	printk(KERN_INFO "class created\n");

	my_device = device_create(my_class, NULL, my_dev_id, NULL, DRIVER_NAME);
	if (my_device == NULL){
		printk(KERN_ERR "failed to create device\n");
		goto fail_1;
	}
	printk(KERN_INFO "device created\n");

	my_cdev = cdev_alloc();	
	my_cdev->ops = &my_fops;
	my_cdev->owner = THIS_MODULE;
	ret = cdev_add(my_cdev, my_dev_id, 1);
	if (ret)
	{
		printk(KERN_ERR "failed to add cdev\n");
		goto fail_2;
	}
	printk(KERN_INFO "cdev added\n");
	printk(KERN_INFO "Hello world\n");

	return platform_driver_register(&sobel_driver);

	fail_2:
		device_destroy(my_class, my_dev_id);
	fail_1:
		class_destroy(my_class);
	fail_0:
		unregister_chrdev_region(my_dev_id, 1);
	return -1;
}

static void __exit sobel_exit(void)
{
 	platform_driver_unregister(&sobel_driver);
	cdev_del(my_cdev);
	device_destroy(my_class, my_dev_id);
	class_destroy(my_class);
	unregister_chrdev_region(my_dev_id,1);
	printk(KERN_INFO "Goodbye, cruel world\n");
}


module_init(sobel_init);
module_exit(sobel_exit);

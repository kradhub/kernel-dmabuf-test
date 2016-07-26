#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/dma-buf.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include "dmabufexp.h"

#define MAJOR_NUMBER 60

#define DMADEV_INFO(dev, fmt, ...) \
	printk(KERN_INFO "dmabufexp-%d: " fmt "\n", dev->no, ##__VA_ARGS__)
#define DMADEV_ERR(dev, fmt, ...) \
	printk(KERN_ERR "dmabufexp-%d: " fmt "\n", dev->no, ##__VA_ARGS__)

struct dmabufexp_buffer
{
	size_t size;
	uint8_t *data;
};

struct dmabufexp_device
{
	int no;
	struct device *dev;
	struct cdev c_dev;
};

static struct class *dmabufexp_class;
static struct dmabufexp_device dmabufexp_dev[2];
static dev_t first;

static struct dmabufexp_buffer *dmabufexp_buffer_alloc(size_t size)
{
	struct dmabufexp_buffer *buf;

	buf = kmalloc(sizeof(*buf), GFP_KERNEL);
	if (buf == NULL)
		return ERR_PTR(-ENOMEM);

	buf->size = size;
	buf->data = kmalloc(size, GFP_KERNEL);
	if (buf->data == NULL) {
		kfree(buf);
		return ERR_PTR(-ENOMEM);
	}

	printk(KERN_INFO "buffer of size %zu successfully allocated\n", size);

	return buf;
}

static void dmabufexp_buffer_free(struct dmabufexp_buffer * buf)
{
	kfree(buf->data);
	kfree(buf);
}

/**
 * DMABuf Operations
 */

struct sg_table *dmabufexp_dmabuf_map(struct dma_buf_attachment * attach,
		enum dma_data_direction dir)
{
	return NULL;
}

void dmabufexp_dmabuf_unmap(struct dma_buf_attachment * attach,
		struct sg_table * sg, enum dma_data_direction dir)
{
}

void dmabufexp_dmabuf_release(struct dma_buf * dmabuf)
{
	struct dmabufexp_buffer *buf = dmabuf->priv;

	printk(KERN_INFO "releasing buffer of size %zu\n", buf->size);
	dmabufexp_buffer_free(buf);
}

void *dmabufexp_dmabuf_kmap_atomic(struct dma_buf * dmabuf, unsigned long arg)
{
	return NULL;
}

void *dmabufexp_dmabuf_kmap(struct dma_buf * dmabuf, unsigned long pgnum)
{
	struct dmabufexp_buffer *buf = dmabuf->priv;

	return buf->data + pgnum * PAGE_SIZE;
}

int dmabufexp_dmabuf_mmap(struct dma_buf * dmabuf, struct vm_area_struct * vma)
{
	struct dmabufexp_buffer *buf = dmabuf->priv;
	int ret;
	unsigned long size;

	printk(KERN_INFO "map dmabuf to userspace\n");

	if (buf == NULL) {
		printk(KERN_ERR "no memory to map\n");
		return -EINVAL;
	}

	size = vma->vm_end - vma->vm_start;
	if (size > DMABUFEXP_BUF_SIZE) {
		printk(KERN_ERR "size is too big\n");
		return -EINVAL;
	}

	ret = remap_pfn_range(vma, vma->vm_start,
			__pa(buf->data) >> PAGE_SHIFT, size, vma->vm_page_prot);

	return ret;
}

static struct dma_buf_ops dmabufexp_dmabufops = {
	.map_dma_buf = dmabufexp_dmabuf_map,
	.unmap_dma_buf = dmabufexp_dmabuf_unmap,
	.release = dmabufexp_dmabuf_release,
	.kmap_atomic = dmabufexp_dmabuf_kmap_atomic,
	.kmap = dmabufexp_dmabuf_kmap,
	.mmap = dmabufexp_dmabuf_mmap
};

static int dmabufexp_open(struct inode * inode, struct file * f)
{
	struct dmabufexp_device *dev;

	dev = container_of(inode->i_cdev, struct dmabufexp_device, c_dev);
	f->private_data = dev;

	return 0;
}

static int dmabufexp_release(struct inode * inode, struct file * f)
{
	return 0;
}

/**
 * Device file operations
 */

static int dmabufexp_ioctl_export(struct dmabufexp_device *dev)
{
	struct dmabufexp_buffer *buf;
	struct dma_buf *dmabuf;
	int fd;

	DMADEV_INFO(dev, "allocating and exporting a buffer");

	buf = dmabufexp_buffer_alloc(DMABUFEXP_BUF_SIZE);
	if (IS_ERR(buf))
		return -ENOMEM;

	snprintf(buf->data, buf->size, "Hi, I'm buffer");

	dmabuf = dma_buf_export_named(buf, &dmabufexp_dmabufops, buf->size,
			O_RDWR, "dmabufexp", NULL);
	if (IS_ERR(dmabuf)) {
		printk(KERN_ERR "failed to export dmabuf\n");
		return -1;
	}

	fd = dma_buf_fd(dmabuf, 0);
	if (fd < 0) {
		printk(KERN_ERR "failed to get an fd for dmabuf\n");
		return -1;
	}

	DMADEV_INFO(dev, "buffer successfully exported using fd %d", fd);

	return fd;
}

static int dmabufexp_ioctl_import(struct dmabufexp_device * dev, int fd)
{
	int ret = 0;
	struct dma_buf *dmabuf;
	/* struct dma_buf_attachment *attach; */
	uint8_t *data;

	DMADEV_INFO(dev, "importing buffer associated with fd %d", fd);

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf)) {
		DMADEV_ERR(dev, "failed to import fd");
		ret = -EBADF;
		goto done;
	}

	/* don't attach when doing a CPU access */
#if 0
	attach = dma_buf_attach(dmabuf, ctx->dev);
	if (IS_ERR(attach)) {
		printk(KERN_ERR "failed to attach to dmabuf\n");
		ret = -1;
		dma_buf_put(dmabuf);
		goto done;
	}
#endif

	DMADEV_INFO(dev, "buffer with fd %d successfully imported", fd);

	ret = dma_buf_begin_cpu_access(dmabuf, 0, DMABUFEXP_BUF_SIZE, DMA_FROM_DEVICE);
	if (ret < 0) {
		DMADEV_ERR(dev, "failed to access dmabuf with CPU");
		dma_buf_put(dmabuf);
		goto done;
	}

	data = dma_buf_kmap(dmabuf, 0);
	if (data == NULL) {
		DMADEV_ERR(dev, "failed to map dmabuf");
		dma_buf_put(dmabuf);
		dma_buf_end_cpu_access(dmabuf, 0, DMABUFEXP_BUF_SIZE, DMA_FROM_DEVICE);
		ret = -1;
		goto done;
	}

	DMADEV_INFO(dev, "buffer data is: %s\n", data);

	dma_buf_kunmap(dmabuf, 0, data);
	dma_buf_end_cpu_access(dmabuf, 0, DMABUFEXP_BUF_SIZE, DMA_FROM_DEVICE);

	dma_buf_put(dmabuf);

done:
	return ret;
}

static int dmabufexp_ioctl_export_copy_user(struct dmabufexp_device * dev,
		unsigned long * param)
{
	int fd;

	fd = dmabufexp_ioctl_export(dev);

	if (copy_to_user((void __user *)(*param), &fd, sizeof(int))) {
		DMADEV_ERR(dev, "failed to copy fd to user\n");
		return -EFAULT;
	}

	return 0;
}

static int dmabufexp_ioctl_import_copy_user(struct dmabufexp_device * dev,
		unsigned long * param)
{
	int fd;

	if (copy_from_user(&fd, (void __user *)(*param), sizeof(int))) {
		DMADEV_ERR(dev, "failed to copy fd from user\n");
		return -EINVAL;
	}

	return dmabufexp_ioctl_import(dev, fd);
}

static long dmabufexp_ioctl(struct file * f, unsigned int num,
		unsigned long params)
{
	int ret = 0;
	struct dmabufexp_device *dev = f->private_data;

	if (dev == NULL)
		return -EINVAL;

	switch (num) {
	case DMABUFEXP_IO_EXPORT:
		ret = dmabufexp_ioctl_export_copy_user(dev, &params);
		break;

	case DMABUFEXP_IO_IMPORT:
		ret = dmabufexp_ioctl_import_copy_user(dev, &params);
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static struct file_operations dmabufexp_fops = {
	.owner = THIS_MODULE,
	.open = dmabufexp_open,
	.release = dmabufexp_release,
	.unlocked_ioctl = dmabufexp_ioctl
};

static int dmabufexp_device_init(struct dmabufexp_device * dev, int index)
{
	dev_t devno = MKDEV(MAJOR(first), MINOR(first) + index);
	int ret;

	if (index > 1) {
		printk(KERN_ERR "device index out of range\n");
		return -EINVAL;
	}

	printk(KERN_INFO "initializing device %d\n", index);
	dev->no = index;

	dev->dev = device_create(dmabufexp_class, NULL, devno, dev,
			"dmabufexp-%u", index);
	if (dev->dev == NULL) {
		printk(KERN_ERR "failed to create device\n");
		return -1;
	}

	cdev_init(&dev->c_dev, &dmabufexp_fops);
	ret = cdev_add(&dev->c_dev, devno, 1);
	if (ret < 0) {
		printk(KERN_ERR "failed to add device\n");
		device_destroy(dmabufexp_class, devno);
		return -1;
	}

	return 0;
}

static int dmabufexp_device_deinit(struct dmabufexp_device * dev, int index)
{
	dev_t devno = MKDEV(MAJOR(first), MINOR(first) + index);

	if (index > 1) {
		printk(KERN_ERR "device index out of range\n");
		return -EINVAL;
	}

	cdev_del(&dev->c_dev);
	if (dev->dev)
		device_destroy(dmabufexp_class, devno);
	return 0;
}

static int dmabufexp_init(void)
{
	int ret;
	int i;

	printk(KERN_INFO "dmabufexp initialization\n");

	ret = alloc_chrdev_region(&first, 0, 2, "dmabufexp");
	if (ret < 0) {
		printk(KERN_ERR "failed to alloc chrdev region\n");
		goto cleanup;
	}

	dmabufexp_class = class_create(THIS_MODULE, "dmabufexp");
	if (dmabufexp_class == NULL) {
		printk(KERN_ERR "failed to create class\n");
		ret = -1;
		goto cleanup;
	}

	for (i = 0; i < 2; i++) {
		ret = dmabufexp_device_init(&dmabufexp_dev[i], i);
		if (ret < 0)
			return ret;
	}

	printk(KERN_INFO "dmabuf initialized\n");
	return 0;

cleanup:
	for (i = 0; i < 2; i++)
		dmabufexp_device_deinit(&dmabufexp_dev[i], i);

	if (dmabufexp_class)
		class_destroy(dmabufexp_class);

	unregister_chrdev_region(first, 2);
	return -1;
}

static void dmabufexp_exit(void)
{
	int i;

	printk(KERN_INFO "dmabufexp deinitialization\n");

	for (i = 0; i < 2; i++)
		dmabufexp_device_deinit(&dmabufexp_dev[i], i);

	if (dmabufexp_class)
		class_destroy(dmabufexp_class);

	unregister_chrdev_region(first, 2);
}

module_init(dmabufexp_init);
module_exit(dmabufexp_exit);

MODULE_LICENSE("GPL");

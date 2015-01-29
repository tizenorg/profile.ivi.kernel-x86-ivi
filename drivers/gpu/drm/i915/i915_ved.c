/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Yao Cheng <yao.cheng@intel.com>
 *
 */

#include "i915_drv.h"

/**
 * DOC: VED video core integration
 *
 * Motivation:
 * Some platforms (e.g. valleyview) integrates a VED inside GPU to extend the
 * video decoding capability.
 * The VED is driven by the standalone drm driver "ipvr" which covers PowerVR
 * VPUs. Since the PowerVR VPUs are also integrated by non-i915 platforms such
 * as GMA500, we'd like to keep ipvr driver and i915 driver separated and
 * independent to each other. To achieve this we do the minimum work in i915
 * to setup a bridge between ipvr and i915:
 * 1. Create a platform device to share MMIO/IRQ resources
 * 2. Make the platform device child of i915 device for runtime PM.
 * 3. Create IRQ chip to forward the VED irqs.
 * ipvr driver probes the VED device and creates a new dri card on install.
 *
 * Threats:
 * Due to the restriction in Linux platform device model, user need manually
 * uninstall ipvr driver before uninstalling i915 module, otherwise he might
 * run into use-after-free issues after i915 removes the platform device.
 *
 * Implementation:
 * The MMIO/REG platform resources are created according to the registers
 * specification.
 * When forwarding VED irqs, the flow control handler selection depends on the
 * platform, for example on valleyview handle_simple_irq is enough.
 *
 */

static struct platform_device* vlv_ved_platdev_create(struct drm_device *dev)
{
	int ret;
	struct resource rsc[2] = { {0}, {0} };
	struct platform_device *platdev;
	u64 *dma_mask = NULL;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->ved.irq < 0)
		return ERR_PTR(-EINVAL);

	platdev = platform_device_alloc("ipvr-ved-vlv", -1);
	if (!platdev) {
		ret = -ENOMEM;
		DRM_ERROR("Failed to allocate VED platform device\n");
		goto err;
	}

	/* to work-around check_addr in nommu_map_sg() */
	dma_mask = kmalloc(sizeof(*platdev->dev.dma_mask), GFP_KERNEL);
	if (!dma_mask) {
		ret = -ENOMEM;
		DRM_ERROR("Failed to allocate dma_mask\n");
		goto err_put_dev;
	}
	*dma_mask = DMA_BIT_MASK(31);
	platdev->dev.dma_mask = dma_mask;
	platdev->dev.coherent_dma_mask = *dma_mask;

	rsc[0].start    = rsc[0].end = dev_priv->ved.irq;
	rsc[0].flags    = IORESOURCE_IRQ;
	rsc[0].name     = "ipvr-ved-vlv-irq";

	rsc[1].start    = pci_resource_start(dev->pdev, 0) + VLV_VED_BASE;
	rsc[1].end      = pci_resource_start(dev->pdev, 0) + VLV_VED_BASE + VLV_VED_SIZE;
	rsc[1].flags    = IORESOURCE_MEM;
	rsc[1].name     = "ipvr-ved-vlv-mmio";

	ret = platform_device_add_resources(platdev, rsc, 2);
	if (ret) {
		DRM_ERROR("Failed to add resource for VED platform device: %d\n", ret);
		goto err_put_dev;
	}

	platdev->dev.parent = dev->dev; /* for VED driver's runtime-PM */
	ret = platform_device_add(platdev);
	if (ret) {
		DRM_ERROR("Failed to add VED platform device: %d\n", ret);
		goto err_put_dev;
	}

	return platdev;
err_put_dev:
	platform_device_put(platdev);
err:
	if (dma_mask)
		kfree(dma_mask);
	return ERR_PTR(ret);
}

static void vlv_ved_platdev_destroy(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	if (dev_priv->ved.platdev) {
		kfree(dev_priv->ved.platdev->dev.dma_mask);
		platform_device_unregister(dev_priv->ved.platdev);
	}
}

static void vlv_ved_irq_unmask(struct irq_data *d)
{
	struct drm_device *dev = d->chip_data;
	struct drm_i915_private *dev_priv = (struct drm_i915_private *) dev->dev_private;
	unsigned long irqflags;
	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);

	dev_priv->irq_mask &= ~VLV_VED_BLOCK_INTERRUPT;
	I915_WRITE(VLV_IIR, VLV_VED_BLOCK_INTERRUPT);
	I915_WRITE(VLV_IIR, VLV_VED_BLOCK_INTERRUPT);
	I915_WRITE(VLV_IMR, dev_priv->irq_mask);
	I915_WRITE(VLV_IER, ~dev_priv->irq_mask);
	POSTING_READ(VLV_IER);

	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

static void vlv_ved_irq_mask(struct irq_data *d)
{
	struct drm_device *dev = d->chip_data;
	struct drm_i915_private *dev_priv = (struct drm_i915_private *) dev->dev_private;
	unsigned long irqflags;
	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);

	dev_priv->irq_mask |= VLV_VED_BLOCK_INTERRUPT;
	I915_WRITE(VLV_IER, ~dev_priv->irq_mask);
	I915_WRITE(VLV_IMR, dev_priv->irq_mask);
	I915_WRITE(VLV_IIR, VLV_VED_BLOCK_INTERRUPT);
	I915_WRITE(VLV_IIR, VLV_VED_BLOCK_INTERRUPT);
	POSTING_READ(VLV_IIR);

	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

static struct irq_chip vlv_ved_irqchip = {
	.name = "ipvr_ved_irqchip",
	.irq_mask = vlv_ved_irq_mask,
	.irq_unmask = vlv_ved_irq_unmask,
};

static int vlv_ved_irq_init(struct drm_device *dev, int irq)
{
	struct drm_i915_private *dev_priv = (struct drm_i915_private *) dev->dev_private;
	//WARN_ON(!intel_irqs_enabled(dev_priv));
	irq_set_chip_and_handler_name(irq,
		&vlv_ved_irqchip,
		handle_simple_irq,
		"ipvr_ved_vlv_irq_handler");
	return irq_set_chip_data(irq, dev);
}

/**
 * vlv_ved_irq_handler() - forwards the VED irq
 * @dev: the i915 drm device
 *
 * the VED irq is forwarded to the irq handler registered by VED driver.
 */
void vlv_ved_irq_handler(struct drm_device *dev)
{
	int ret;
	struct drm_i915_private *dev_priv = dev->dev_private;
	if (dev_priv->ved.irq < 0 && printk_ratelimit()) {
		DRM_ERROR("invalid ved irq number: %d\n", dev_priv->ved.irq);
		return;
	}
	ret = generic_handle_irq(dev_priv->ved.irq);
	if (ret && printk_ratelimit()) {
		DRM_ERROR("error handling vlv ved irq: %d\n", ret);
	}
}

/**
 * vlv_setup_ved() - setup the bridge between VED driver and i915
 * @dev: the i915 drm device
 *
 * set up the minimum required resources for the bridge: irq chip, platform
 * resource and platform device. i915 device is set as parent of the new
 * platform device.
 *
 * Return: 0 if successful. non-zero if allocation/initialization fails
 */
int vlv_setup_ved(struct drm_device *dev)
{
	int ret;
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->ved.irq = irq_alloc_descs(-1, 0, 1, 0);
	if (dev_priv->ved.irq < 0) {
		DRM_ERROR("Failed to allocate IRQ desc: %d\n", dev_priv->ved.irq);
		ret = dev_priv->ved.irq;
		goto err;
	}

	ret = vlv_ved_irq_init(dev, dev_priv->ved.irq);
	if (ret) {
		DRM_ERROR("Failed to initialize irqchip for vlv-ved: %d\n", ret);
		goto err_free_irq;
	}

	dev_priv->ved.platdev = vlv_ved_platdev_create(dev);
	if (IS_ERR(dev_priv->ved.platdev)) {
		ret = PTR_ERR(dev_priv->ved.platdev);
		DRM_ERROR("Failed to create platform device for vlv-ved: %d\n", ret);
		goto err_free_irq;
	}

	return 0;
err_free_irq:
	irq_free_desc(dev_priv->ved.irq);
err:
	dev_priv->ved.irq = -1;
	dev_priv->ved.platdev = NULL;
	return ret;
}

/**
 * vlv_teardown_ved() - destroy the bridge between VED driver and i915
 * @dev: the i915 drm device
 *
 * release all the resources for VED <-> i915 bridge.
 */
void vlv_teardown_ved(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	vlv_ved_platdev_destroy(dev);
	if (dev_priv->ved.irq >= 0)
		irq_free_desc(dev_priv->ved.irq);
}

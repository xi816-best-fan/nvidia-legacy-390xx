/*
 * Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "nvidia-drm-conftest.h"

#if defined(NV_DRM_AVAILABLE)

#include "nvidia-drm-priv.h"
#include "nvidia-drm-ioctl.h"
#include "nvidia-drm-prime-fence.h"
#include "nvidia-drm-gem.h"
#include "nvidia-dma-resv-helper.h"
#include "nvidia-drm-gem-nvkms-memory.h"

#if defined(NV_DRM_DRM_PRIME_H_PRESENT)
#include <drm/drm_prime.h>
#endif

#include "linux/dma-buf.h"

void nv_drm_gem_free(struct drm_gem_object *gem)
{
    struct nv_drm_gem_object *nv_gem = to_nv_gem_object(gem);

    /* Cleanup core gem object */
    drm_gem_object_release(&nv_gem->base);

#if defined(NV_DRM_FENCE_AVAILABLE) && !defined(NV_DRM_GEM_OBJECT_HAS_RESV)
    nv_dma_resv_fini(&nv_gem->resv);
#endif

    nv_gem->ops->free(nv_gem);
}

#if !defined(NV_DRM_DRIVER_HAS_GEM_PRIME_CALLBACKS) && \
    defined(NV_DRM_GEM_OBJECT_VMAP_HAS_MAP_ARG)

/*
 * The 'dma_buf_map' structure is renamed to 'iosys_map' by the commit
 * 7938f4218168 ("dma-buf-map: Rename to iosys-map").
 */
#if defined(NV_LINUX_IOSYS_MAP_H_PRESENT)
typedef struct iosys_map nv_sysio_map_t;
#else
typedef struct dma_buf_map nv_sysio_map_t;
#endif

static int nv_drm_gem_vmap(struct drm_gem_object *gem,
                           nv_sysio_map_t *map)
{
    map->vaddr = nv_drm_gem_prime_vmap(gem);
    if (map->vaddr == NULL) {
        return -ENOMEM;
    }
    map->is_iomem = true;
    return 0;
}

static void nv_drm_gem_vunmap(struct drm_gem_object *gem,
                              nv_sysio_map_t *map)
{
    nv_drm_gem_prime_vunmap(gem, map->vaddr);
    map->vaddr = NULL;
}
#endif

#if !defined(NV_DRM_DRIVER_HAS_GEM_FREE_OBJECT) || \
    !defined(NV_DRM_DRIVER_HAS_GEM_PRIME_CALLBACKS)
static struct drm_gem_object_funcs nv_drm_gem_funcs = {
    .free = nv_drm_gem_free,
    .get_sg_table = nv_drm_gem_prime_get_sg_table,

#if !defined(NV_DRM_DRIVER_HAS_GEM_PRIME_CALLBACKS)
    .export  = nv_drm_gem_prime_export,
#if defined(NV_DRM_GEM_OBJECT_VMAP_HAS_MAP_ARG)
    .vmap    = nv_drm_gem_vmap,
    .vunmap  = nv_drm_gem_vunmap,
#else
    .vmap    = nv_drm_gem_prime_vmap,
    .vunmap  = nv_drm_gem_prime_vunmap,
#endif

#if defined(NV_DRM_ATOMIC_MODESET_AVAILABLE)
    .vm_ops  = &nv_drm_gem_vma_ops,
#endif

#endif
};
#endif

void nv_drm_gem_object_init(struct nv_drm_device *nv_dev,
                            struct nv_drm_gem_object *nv_gem,
                            const struct nv_drm_gem_object_funcs * const ops,
                            size_t size,
                            bool prime)
{
    struct drm_device *dev = nv_dev->dev;

    nv_gem->nv_dev = nv_dev;
    nv_gem->prime = prime;
    nv_gem->ops = ops;

    /* Initialize the gem object */

#if defined(NV_DRM_FENCE_AVAILABLE)
    nv_dma_resv_init(&nv_gem->resv);

#if defined(NV_DRM_GEM_OBJECT_HAS_RESV)
    nv_gem->base.resv = &nv_gem->resv;
#endif

#endif

#if !defined(NV_DRM_DRIVER_HAS_GEM_FREE_OBJECT)
    nv_gem->base.funcs = &nv_drm_gem_funcs;
#endif

    drm_gem_private_object_init(dev, &nv_gem->base, size);
}

struct dma_buf *nv_drm_gem_prime_export(
#if defined(NV_DRM_GEM_PRIME_EXPORT_HAS_DEV_ARG)
    struct drm_device *dev,
#endif
    struct drm_gem_object *gem, int flags)
{
    struct nv_drm_device *nv_dev = to_nv_device(gem->dev);

    struct nv_drm_gem_object *nv_gem = to_nv_gem_object(gem);

    if (!nv_gem->prime) {
        NV_DRM_DEV_LOG_ERR(
            nv_dev,
            "Gem object 0x%p is not suitable to export", gem);
        return ERR_PTR(-EINVAL);
    }

    return drm_gem_prime_export(
#if defined(NV_DRM_GEM_PRIME_EXPORT_HAS_DEV_ARG)
                dev,
#endif
                gem, flags);
}

struct sg_table *nv_drm_gem_prime_get_sg_table(struct drm_gem_object *gem)
{
    struct nv_drm_gem_object *nv_gem = to_nv_gem_object(gem);

    if (nv_gem->ops->prime_get_sg_table != NULL) {
        return nv_gem->ops->prime_get_sg_table(nv_gem);
    }

    return ERR_PTR(-ENOTSUPP);
}

void *nv_drm_gem_prime_vmap(struct drm_gem_object *gem)
{
    struct nv_drm_gem_object *nv_gem = to_nv_gem_object(gem);

    if (nv_gem->ops->prime_vmap != NULL) {
        return nv_gem->ops->prime_vmap(nv_gem);
    }

    return ERR_PTR(-ENOTSUPP);
}

void nv_drm_gem_prime_vunmap(struct drm_gem_object *gem, void *address)
{
    struct nv_drm_gem_object *nv_gem = to_nv_gem_object(gem);

    if (nv_gem->ops->prime_vunmap != NULL) {
        nv_gem->ops->prime_vunmap(nv_gem, address);
    }
}

#if defined(NV_DRM_DRIVER_HAS_GEM_PRIME_RES_OBJ)
nv_dma_resv_t* nv_drm_gem_prime_res_obj(struct drm_gem_object *obj)
{
    struct nv_drm_gem_object *nv_gem = to_nv_gem_object(obj);

    return &nv_gem->resv;
}
#endif

#endif /* NV_DRM_AVAILABLE */

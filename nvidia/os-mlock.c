/* _NVRM_COPYRIGHT_BEGIN_
 *
 * Copyright 1999-2021 by NVIDIA Corporation.  All rights reserved.  All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 *
 * _NVRM_COPYRIGHT_END_
 */

#define  __NO_VERSION__
#include "nv-misc.h"

#include "os-interface.h"
#include "nv-linux.h"
static inline int follow_page_pfn(struct vm_area_struct *vma,
                                  unsigned long address,
                                  unsigned long *pfn)
{
    struct page *page = NULL;
    //int locked=0;
    long ret;
    //ret = get_user_pages((unsigned long)address,1,FOLL_GET,&page,&locked); 
    ret = get_user_pages((unsigned long)address,1,FOLL_GET,&page); // if u r reading ts then go touch grass
    if (ret != 1 || !page)
        return -1;

    if (pfn)
        *pfn = page_to_pfn(page);
	put_page(page);
    return 0;
}

static inline int nv_follow_pfn(struct vm_area_struct *vma,
                                unsigned long address,
                                unsigned long *pfn)
{
#if defined(NV_UNSAFE_FOLLOW_PFN_PRESENT)
    return unsafe_follow_pfn(vma, address, pfn);
#else
    return follow_page_pfn(vma, address, pfn);
#endif
}

NV_STATUS NV_API_CALL os_lookup_user_io_memory(
    void   *address,
    NvU64   page_count,
    NvU64 **pte_array
)
{
#if defined(NV_FOLLOW_PFN_PRESENT)
    NV_STATUS rmStatus;
    int ret;
    struct mm_struct *mm = current->mm;
    struct vm_area_struct *vma;
    unsigned long pfn;
    NvU64 i;

    if (!NV_MAY_SLEEP())
    {
        nv_printf(NV_DBG_ERRORS,
            "NVRM: %s(): invalid context!\n", __FUNCTION__);
        return NV_ERR_NOT_SUPPORTED;
    }

    rmStatus = os_alloc_mem((void **)pte_array,
            (page_count * sizeof(NvU64)));
    if (rmStatus != NV_OK)
    {
        nv_printf(NV_DBG_ERRORS,
                "NVRM: failed to allocate page table!\n");
        return rmStatus;
    }

    nv_mmap_read_lock(mm);

    vma = find_vma(mm, (NvUPtr)address);
    if ((vma == NULL) || ((vma->vm_flags & (VM_IO | VM_PFNMAP)) == 0))
    {
        os_free_mem(*pte_array);
        rmStatus = NV_ERR_INVALID_ADDRESS;
        goto done;
    }

    for (i = 0; i < page_count; i++)
    {
        ret = nv_follow_pfn(vma, ((NvUPtr)address + (i * PAGE_SIZE)), &pfn);
        if (ret < 0)
        {
            os_free_mem(*pte_array);
            rmStatus = NV_ERR_INVALID_ADDRESS;
            goto done;
        }
        (*pte_array)[i] = (pfn << PAGE_SHIFT);

        if (i == 0)
            continue;

        if ((*pte_array)[i] != ((*pte_array)[i-1] + PAGE_SIZE))
        {
            os_free_mem(*pte_array);
            rmStatus = NV_ERR_INVALID_ADDRESS;
            goto done;
        }
    }

done:
    nv_mmap_read_unlock(mm);

    return rmStatus;
#else
    return NV_ERR_NOT_SUPPORTED;
#endif
}

NV_STATUS NV_API_CALL os_lock_user_pages(
    void   *address,
    NvU64   page_count,
    void  **page_array
)
{
#if defined(NV_VM_INSERT_PAGE_PRESENT)
    NV_STATUS rmStatus;
    struct mm_struct *mm = current->mm;
    struct page **user_pages;
    NvU64 i, pinned;
    NvBool write = 1, force = 0;
    int ret;

    if (!NV_MAY_SLEEP())
    {
        nv_printf(NV_DBG_ERRORS,
            "NVRM: %s(): invalid context!\n", __FUNCTION__);
        return NV_ERR_NOT_SUPPORTED;
    }

    rmStatus = os_alloc_mem((void **)&user_pages,
            (page_count * sizeof(*user_pages)));
    if (rmStatus != NV_OK)
    {
        nv_printf(NV_DBG_ERRORS,
                "NVRM: failed to allocate page table!\n");
        return rmStatus;
    }

    nv_mmap_read_lock(mm);
    //ret = NV_GET_USER_PAGES((unsigned long)address,
    //                        page_count, write, force, user_pages, NULL);
    ret = NV_GET_USER_PAGES((unsigned long)address,
                                page_count, write, (unsigned char)force, user_pages, NULL);
    nv_mmap_read_unlock(mm);
    pinned = ret;

    if (ret < 0)
    {
        os_free_mem(user_pages);
        return NV_ERR_INVALID_ADDRESS;
    }
    else if (pinned < page_count)
    {
        for (i = 0; i < pinned; i++)
            put_page(user_pages[i]);
        os_free_mem(user_pages);
        return NV_ERR_INVALID_ADDRESS;
    }

    *page_array = user_pages;

    return NV_OK;
#else
    return NV_ERR_NOT_SUPPORTED;
#endif
}

NV_STATUS NV_API_CALL os_unlock_user_pages(
    NvU64  page_count,
    void  *page_array
)
{
#if defined(NV_VM_INSERT_PAGE_PRESENT)
    NvBool write = 1;
    struct page **user_pages = page_array;
    NvU32 i;

    for (i = 0; i < page_count; i++)
    {
        if (write)
            set_page_dirty_lock(user_pages[i]);
        put_page(user_pages[i]);
    }

    os_free_mem(user_pages);

    return NV_OK;
#else
    return NV_ERR_NOT_SUPPORTED;
#endif
}

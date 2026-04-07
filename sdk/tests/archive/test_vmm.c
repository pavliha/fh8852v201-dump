#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <errno.h>

#define VMM_ALLOC 0xC0506D0A
#define VMM_MMAP  0xC0506D14

int main(void)
{
    printf("=== VMM Alloc Test ===\n\n");

    int vmm = open("/dev/vmm_userdev", O_RDWR);
    printf("vmm fd=%d\n", vmm);
    if (vmm < 0) return 1;

    /* Exact struct layout from Ghidra decompilation:
     * [0x00] phys_addr_lo  (uint32, output)
     * [0x04] phys_addr_hi  (uint32, output, should be 0 for 32-bit)
     * [0x08] alignment     (uint32, input: param_6)
     * [0x0C] size          (uint32, input: param_5)
     * [0x10] ???
     * [0x14] virt_addr     (uint32, output from mmap)
     * [0x18] flags         (uint32, set for mmap: 0x103)
     * [0x1C] name          (char[16])
     * [0x2C] zone          (char[40])
     * Total: 0x50 (80) bytes
     */
    uint8_t p[80];

    /* Try allocating 88064 bytes (the VPU system size) */
    uint32_t sizes[] = { 88064, 4096, 1024 };
    const char *names[] = { "test-sys", "test-4k", "test-1k" };

    for (int i = 0; i < 3; i++) {
        memset(p, 0, 80);
        *(uint32_t *)(p + 0x08) = 8;          /* alignment */
        *(uint32_t *)(p + 0x0C) = sizes[i];   /* size */
        strncpy((char *)(p + 0x1C), names[i], 15);
        strncpy((char *)(p + 0x2C), "anonymous", 39);

        int ret = ioctl(vmm, VMM_ALLOC, p);
        uint32_t phys = *(uint32_t *)p;
        printf("  alloc %s (%u bytes): ret=%d phys=0x%08x errno=%d\n",
               names[i], sizes[i], ret, phys, ret < 0 ? errno : 0);

        if (ret >= 0 && phys != 0) {
            printf("    *** VMM ALLOC SUCCESS! ***\n");

            /* Try mmap */
            *(uint32_t *)(p + 0x18) = 0x103;  /* flags: PROT_READ|PROT_WRITE + MAP_SHARED */
            ret = ioctl(vmm, VMM_MMAP, p);
            uint32_t virt = *(uint32_t *)(p + 0x14);
            printf("    mmap: ret=%d virt=0x%08x errno=%d\n",
                   ret, virt, ret < 0 ? errno : 0);
        }
    }

    /* Also try with different alignment and zero padding */
    printf("\n  Trying alternative struct layouts...\n");

    /* Layout B: size at offset 0x08, align at 0x0C */
    memset(p, 0, 80);
    *(uint32_t *)(p + 0x08) = 4096;    /* size */
    *(uint32_t *)(p + 0x0C) = 8;       /* alignment */
    strncpy((char *)(p + 0x1C), "test-b", 15);
    strncpy((char *)(p + 0x2C), "anonymous", 39);
    int ret = ioctl(vmm, VMM_ALLOC, p);
    printf("  Layout B: ret=%d phys=0x%08x errno=%d\n",
           ret, *(uint32_t *)p, ret < 0 ? errno : 0);

    /* Layout C: name at offset 0x10, zone at 0x20 */
    memset(p, 0, 80);
    *(uint32_t *)(p + 0x08) = 8;
    *(uint32_t *)(p + 0x0C) = 4096;
    strncpy((char *)(p + 0x10), "test-c", 15);
    strncpy((char *)(p + 0x20), "anonymous", 39);
    ret = ioctl(vmm, VMM_ALLOC, p);
    printf("  Layout C: ret=%d phys=0x%08x errno=%d\n",
           ret, *(uint32_t *)p, ret < 0 ? errno : 0);

    /* Dump raw ioctl result */
    memset(p, 0xAA, 80);
    *(uint32_t *)(p + 0x08) = 8;
    *(uint32_t *)(p + 0x0C) = 4096;
    strncpy((char *)(p + 0x1C), "rawtest", 15);
    strncpy((char *)(p + 0x2C), "anonymous", 39);
    ret = ioctl(vmm, VMM_ALLOC, p);
    printf("\n  Raw dump after ioctl (ret=%d errno=%d):\n", ret, ret<0?errno:0);
    for (int i = 0; i < 80; i += 4) {
        if (i % 16 == 0) printf("    [0x%02x]", i);
        printf(" %08x", *(uint32_t *)(p + i));
        if (i % 16 == 12) printf("\n");
    }

    close(vmm);
    printf("\n=== Done ===\n");
    return 0;
}

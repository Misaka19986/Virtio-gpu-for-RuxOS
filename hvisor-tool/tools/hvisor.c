#include "hvisor.h"
#include "cJSON.h"
#include "event_monitor.h"
#include "log.h"
#include "virtio.h"
#include "zone_config.h"
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void __attribute__((noreturn)) help(int exit_status) {
    printf("Invalid Parameters!\n");
    exit(exit_status);
}

void *read_file(char *filename, u_int64_t *filesize) {
    int fd;
    struct stat st;
    void *buf;
    ssize_t len;

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("read_file: open file failed");
        exit(1);
    }

    if (fstat(fd, &st) < 0) {
        perror("read_file: fstat failed");
        exit(1);
    }

    long page_size = sysconf(_SC_PAGESIZE);

    // Calculate buffer size, ensuring alignment to page boundary
    ssize_t buf_size = (st.st_size + page_size - 1) & ~(page_size - 1);

    buf = malloc(buf_size);
    memset(buf, 0, buf_size);

    len = read(fd, buf, st.st_size);
    if (len < 0) {
        perror("read_file: read failed");
        exit(1);
    }

    if (filesize)
        *filesize = len;

    close(fd);

    return buf;
}

int open_dev() {
    int fd = open("/dev/hvisor", O_RDWR);
    if (fd < 0) {
        perror("open hvisor failed");
        exit(1);
    }
    return fd;
}

// static void get_info(char *optarg, char **path, u64 *address) {
// 	char *now;
// 	*path = strtok(optarg, ",");
// 	now = strtok(NULL, "=");
// 	if (strcmp(now, "addr") == 0) {
// 		now = strtok(NULL, "=");
// 		*address = strtoull(now, NULL, 16);
// 	} else {
// 		help(1);
// 	}
// }

static __u64 load_image_to_memory(const char *path, __u64 load_paddr) {
    __u64 size, page_size,
        map_size; // Define variables: image size, page size, and map size
    int fd;       // File descriptor
    void *image_content,
        *virt_addr; // Pointers to image content and virtual address

    fd = open_dev();
    // Load image content into memory
    image_content = read_file(path, &size);

    page_size = sysconf(_SC_PAGESIZE);
    map_size = (size + page_size - 1) & ~(page_size - 1);

    // Map the physical memory to virtual memory
    virt_addr = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                     load_paddr);

    if (virt_addr == MAP_FAILED) {
        perror("Error mapping memory");
        exit(1);
    }

    memmove(virt_addr, image_content, map_size);

    free(image_content);
    munmap(virt_addr, map_size);

    close(fd);
    return map_size;
}

#define CHECK_JSON_NULL(json_ptr, json_name)                                   \
    if (json_ptr == NULL) {                                                    \
        fprintf(stderr, "\'%s\' is missing in json file.\n", json_name);       \
        return -1;                                                             \
    }

#define CHECK_JSON_NULL_ERR_OUT(json_ptr, json_name)                           \
    if (json_ptr == NULL) {                                                    \
        fprintf(stderr, "\'%s\' is missing in json file.\n", json_name);       \
        goto err_out;                                                          \
    }

static int parse_arch_config(cJSON *root, zone_config_t *config) {
    cJSON *arch_config_json = cJSON_GetObjectItem(root, "arch_config");
    if (arch_config_json == NULL) {
        fprintf(stderr, "No arch_config field found.\n");
        return -1;
    }

#ifdef ARM64
    cJSON *gicd_base_json = cJSON_GetObjectItem(arch_config_json, "gicd_base");
    cJSON *gicr_base_json = cJSON_GetObjectItem(arch_config_json, "gicr_base");
    cJSON *gits_base_json = cJSON_GetObjectItem(arch_config_json, "gits_base");
    cJSON *gicd_size_json = cJSON_GetObjectItem(arch_config_json, "gicd_size");
    cJSON *gicr_size_json = cJSON_GetObjectItem(arch_config_json, "gicr_size");
    cJSON *gits_size_json = cJSON_GetObjectItem(arch_config_json, "gits_size");

    CHECK_JSON_NULL(gicd_base_json, "gicd_base")
    CHECK_JSON_NULL(gicr_base_json, "gicr_base")
    CHECK_JSON_NULL(gicd_size_json, "gicd_size")
    CHECK_JSON_NULL(gicr_size_json, "gicr_size")

    if (gits_base_json == NULL || gits_size_json == NULL) {
        printf("No gits fields in arch_config.\n");
    } else {
        config->arch_config.gits_base =
            strtoull(gits_base_json->valuestring, NULL, 16);
        config->arch_config.gits_size =
            strtoull(gits_size_json->valuestring, NULL, 16);
    }

    config->arch_config.gicd_base =
        strtoull(gicd_base_json->valuestring, NULL, 16);
    config->arch_config.gicr_base =
        strtoull(gicr_base_json->valuestring, NULL, 16);
    config->arch_config.gicd_size =
        strtoull(gicd_size_json->valuestring, NULL, 16);
    config->arch_config.gicr_size =
        strtoull(gicr_size_json->valuestring, NULL, 16);
#endif

#ifdef RISCV64
    cJSON *plic_base_json = cJSON_GetObjectItem(arch_config_json, "plic_base");
    cJSON *plic_size_json = cJSON_GetObjectItem(arch_config_json, "plic_size");

    if (plic_base_json == NULL || plic_size_json == NULL) {
        fprintf(stderr, "Missing fields in arch_config.\n");
        return -1;
    }

    config->arch_config.plic_base =
        strtoull(plic_base_json->valuestring, NULL, 16);
    config->arch_config.plic_size =
        strtoull(plic_size_json->valuestring, NULL, 16);
#endif

    return 0;
}

static int parse_pci_config(cJSON *root, zone_config_t *config) {
    cJSON *pci_config_json = cJSON_GetObjectItem(root, "pci_config");
    if (pci_config_json == NULL) {
        fprintf(stderr, "No pci_config field found.\n");
        return -1;
    }

#ifdef ARM64
    cJSON *ecam_base_json = cJSON_GetObjectItem(pci_config_json, "ecam_base");
    cJSON *io_base_json = cJSON_GetObjectItem(pci_config_json, "io_base");
    cJSON *pci_io_base_json =
        cJSON_GetObjectItem(pci_config_json, "pci_io_base");
    cJSON *mem32_base_json = cJSON_GetObjectItem(pci_config_json, "mem32_base");
    cJSON *pci_mem32_base_json =
        cJSON_GetObjectItem(pci_config_json, "pci_mem32_base");
    cJSON *mem64_base_json = cJSON_GetObjectItem(pci_config_json, "mem64_base");
    cJSON *pci_mem64_base_json =
        cJSON_GetObjectItem(pci_config_json, "pci_mem64_base");
    cJSON *ecam_size_json = cJSON_GetObjectItem(pci_config_json, "ecam_size");
    cJSON *io_size_json = cJSON_GetObjectItem(pci_config_json, "io_size");
    cJSON *mem32_size_json = cJSON_GetObjectItem(pci_config_json, "mem32_size");
    cJSON *mem64_size_json = cJSON_GetObjectItem(pci_config_json, "mem64_size");

    CHECK_JSON_NULL(ecam_base_json, "ecam_base")
    CHECK_JSON_NULL(io_base_json, "io_base")
    CHECK_JSON_NULL(mem32_base_json, "mem32_base")
    CHECK_JSON_NULL(mem64_base_json, "mem64_base")
    CHECK_JSON_NULL(ecam_size_json, "ecam_size")
    CHECK_JSON_NULL(io_size_json, "io_size")
    CHECK_JSON_NULL(mem32_size_json, "mem32_size")
    CHECK_JSON_NULL(mem64_size_json, "mem64_size")
    CHECK_JSON_NULL(pci_io_base_json, "pci_io_base")
    CHECK_JSON_NULL(pci_mem32_base_json, "pci_mem32_base")
    CHECK_JSON_NULL(pci_mem64_base_json, "pci_mem64_base")

    config->pci_config.ecam_base =
        strtoull(ecam_base_json->valuestring, NULL, 16);
    config->pci_config.io_base = strtoull(io_base_json->valuestring, NULL, 16);
    config->pci_config.mem32_base =
        strtoull(mem32_base_json->valuestring, NULL, 16);
    config->pci_config.mem64_base =
        strtoull(mem64_base_json->valuestring, NULL, 16);
    config->pci_config.pci_io_base =
        strtoull(pci_io_base_json->valuestring, NULL, 16);
    config->pci_config.pci_mem32_base =
        strtoull(pci_mem32_base_json->valuestring, NULL, 16);
    config->pci_config.pci_mem64_base =
        strtoull(pci_mem64_base_json->valuestring, NULL, 16);
    config->pci_config.ecam_size =
        strtoull(ecam_size_json->valuestring, NULL, 16);
    config->pci_config.io_size = strtoull(io_size_json->valuestring, NULL, 16);
    config->pci_config.mem32_size =
        strtoull(mem32_size_json->valuestring, NULL, 16);
    config->pci_config.mem64_size =
        strtoull(mem64_size_json->valuestring, NULL, 16);
    cJSON *alloc_pci_devs_json = cJSON_GetObjectItem(root, "alloc_pci_devs");
    int num_pci_devs = cJSON_GetArraySize(alloc_pci_devs_json);
    config->num_pci_devs = num_pci_devs;
    for (int i = 0; i < num_pci_devs; i++) {
        config->alloc_pci_devs[i] =
            cJSON_GetArrayItem(alloc_pci_devs_json, i)->valueint;
    }
#endif

    return 0;
}

static int zone_start_from_json(const char *json_config_path,
                                zone_config_t *config) {
    cJSON *root = NULL;

    FILE *file = fopen(json_config_path, "r");
    if (file == NULL) {
        fprintf(stderr, "Error opening json file: %s\n", json_config_path);
        exit(1);
    }
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *buffer = malloc(file_size + 1);
    if (fread(buffer, 1, file_size, file) == 0) {
        fprintf(stderr, "Error reading json file: %s\n", json_config_path);
        goto err_out;
    }
    fclose(file);
    buffer[file_size] = '\0';

    // parse JSON
    root = cJSON_Parse(buffer);
    cJSON *zone_id_json = cJSON_GetObjectItem(root, "zone_id");
    cJSON *cpus_json = cJSON_GetObjectItem(root, "cpus");
    cJSON *name_json = cJSON_GetObjectItem(root, "name");
    cJSON *memory_regions_json = cJSON_GetObjectItem(root, "memory_regions");
    cJSON *kernel_filepath_json = cJSON_GetObjectItem(root, "kernel_filepath");
    cJSON *dtb_filepath_json = cJSON_GetObjectItem(root, "dtb_filepath");
    cJSON *kernel_load_paddr_json =
        cJSON_GetObjectItem(root, "kernel_load_paddr");
    cJSON *dtb_load_paddr_json = cJSON_GetObjectItem(root, "dtb_load_paddr");
    cJSON *entry_point_json = cJSON_GetObjectItem(root, "entry_point");
    cJSON *kernel_args_json = cJSON_GetObjectItem(root, "kernel_args");
    cJSON *interrupts_json = cJSON_GetObjectItem(root, "interrupts");
    cJSON *ivc_configs_json = cJSON_GetObjectItem(root, "ivc_configs");

    CHECK_JSON_NULL_ERR_OUT(zone_id_json, "zone_id")
    CHECK_JSON_NULL_ERR_OUT(cpus_json, "cpus")
    CHECK_JSON_NULL_ERR_OUT(name_json, "name")
    CHECK_JSON_NULL_ERR_OUT(memory_regions_json, "memory_regions")
    CHECK_JSON_NULL_ERR_OUT(kernel_filepath_json, "kernel_filepath")
    CHECK_JSON_NULL_ERR_OUT(dtb_filepath_json, "dtb_filepath")
    CHECK_JSON_NULL_ERR_OUT(kernel_load_paddr_json, "kernel_load_paddr")
    CHECK_JSON_NULL_ERR_OUT(dtb_load_paddr_json, "dtb_load_paddr")
    CHECK_JSON_NULL_ERR_OUT(entry_point_json, "entry_point")
    CHECK_JSON_NULL_ERR_OUT(kernel_args_json, "kernel_args")
    CHECK_JSON_NULL_ERR_OUT(interrupts_json, "interrupts")
    CHECK_JSON_NULL_ERR_OUT(ivc_configs_json, "ivc_configs")

    config->zone_id = zone_id_json->valueint;

    int num_cpus = cJSON_GetArraySize(cpus_json);

    for (int i = 0; i < num_cpus; i++) {
        config->cpus |= (1 << cJSON_GetArrayItem(cpus_json, i)->valueint);
    }

    int num_memory_regions = cJSON_GetArraySize(memory_regions_json);
    int num_interrupts = cJSON_GetArraySize(interrupts_json);

    if (num_memory_regions > CONFIG_MAX_MEMORY_REGIONS ||
        num_interrupts > CONFIG_MAX_INTERRUPTS) {
        fprintf(stderr, "Exceeded maximum allowed regions/interrupts.\n");
        goto err_out;
    }

    // Iterate through each memory region of the zone
    // Including memory and MMIO regions of the zone
    config->num_memory_regions = num_memory_regions;
    for (int i = 0; i < num_memory_regions; i++) {
        cJSON *region = cJSON_GetArrayItem(memory_regions_json, i);
        memory_region_t *mem_region = &config->memory_regions[i];

        const char *type_str = cJSON_GetObjectItem(region, "type")->valuestring;
        if (strcmp(type_str, "ram") == 0) {
            mem_region->type = MEM_TYPE_RAM;
        } else if (strcmp(type_str, "io") == 0) {
            // io device
            mem_region->type = MEM_TYPE_IO;
        } else if (strcmp(type_str, "virtio") == 0) {
            // virtio device
            mem_region->type = MEM_TYPE_VIRTIO;
        } else {
            printf("Unknown memory region type: %s\n", type_str);
            mem_region->type = -1; // invalid type
        }

        mem_region->physical_start =
            strtoull(cJSON_GetObjectItem(region, "physical_start")->valuestring,
                     NULL, 16);
        mem_region->virtual_start =
            strtoull(cJSON_GetObjectItem(region, "virtual_start")->valuestring,
                     NULL, 16);
        mem_region->size = strtoull(
            cJSON_GetObjectItem(region, "size")->valuestring, NULL, 16);

        log_debug("memory_region %d: type %d, physical_start %llx, "
                  "virtual_start %llx, size %llx\n",
                  i, mem_region->type, mem_region->physical_start,
                  mem_region->virtual_start, mem_region->size);
    }

    config->num_interrupts = num_interrupts;
    for (int i = 0; i < num_interrupts; i++) {
        config->interrupts[i] =
            cJSON_GetArrayItem(interrupts_json, i)->valueint;
    }

    // ivc
    int num_ivc_configs = cJSON_GetArraySize(ivc_configs_json);
    config->num_ivc_configs = num_ivc_configs;
    for (int i = 0; i < num_ivc_configs; i++) {
        cJSON *ivc_config_json = cJSON_GetArrayItem(ivc_configs_json, i);
        ivc_config_t *ivc_config = &config->ivc_configs[i];
        ivc_config->ivc_id =
            cJSON_GetObjectItem(ivc_config_json, "ivc_id")->valueint;
        ivc_config->peer_id =
            cJSON_GetObjectItem(ivc_config_json, "peer_id")->valueint;
        ivc_config->shared_mem_ipa = strtoull(
            cJSON_GetObjectItem(ivc_config_json, "shared_mem_ipa")->valuestring,
            NULL, 16);
        ivc_config->control_table_ipa =
            strtoull(cJSON_GetObjectItem(ivc_config_json, "control_table_ipa")
                         ->valuestring,
                     NULL, 16);
        ivc_config->rw_sec_size = strtoull(
            cJSON_GetObjectItem(ivc_config_json, "rw_sec_size")->valuestring,
            NULL, 16);
        ivc_config->out_sec_size = strtoull(
            cJSON_GetObjectItem(ivc_config_json, "out_sec_size")->valuestring,
            NULL, 16);
        ivc_config->interrupt_num =
            cJSON_GetObjectItem(ivc_config_json, "interrupt_num")->valueint;
        ivc_config->max_peers =
            cJSON_GetObjectItem(ivc_config_json, "max_peers")->valueint;
        printf("ivc_config %d: ivc_id %d, peer_id %d, shared_mem_ipa %llx, "
               "interrupt_num %d, max_peers %d\n",
               i, ivc_config->ivc_id, ivc_config->peer_id,
               ivc_config->shared_mem_ipa, ivc_config->interrupt_num,
               ivc_config->max_peers);
    }
    config->entry_point = strtoull(entry_point_json->valuestring, NULL, 16);

    config->kernel_load_paddr =
        strtoull(kernel_load_paddr_json->valuestring, NULL, 16);

    config->dtb_load_paddr =
        strtoull(dtb_load_paddr_json->valuestring, NULL, 16);

    // Load kernel image to memory
    config->kernel_size = load_image_to_memory(
        kernel_filepath_json->valuestring,
        strtoull(kernel_load_paddr_json->valuestring, NULL, 16));

    // Load dtb to memory
    config->dtb_size = load_image_to_memory(
        dtb_filepath_json->valuestring,
        strtoull(dtb_load_paddr_json->valuestring, NULL, 16));

    // strncpy(config->kernel_args, kernel_args_json->valuestring,
    // CONFIG_KERNEL_ARGS_MAXLEN);

    // check name length
    if (strlen(name_json->valuestring) > CONFIG_NAME_MAXLEN) {
        fprintf(stderr, "Zone name too long: %s\n", name_json->valuestring);
        goto err_out;
    }
    strncpy(config->name, name_json->valuestring, CONFIG_NAME_MAXLEN);

    // Parse architecture-specific configurations (interrupts for each platform)
    parse_arch_config(root, config);

    parse_pci_config(root, config);

    if (root)
        cJSON_Delete(root);
    if (buffer)
        free(buffer);

    int fd = open_dev();
    int err = ioctl(fd, HVISOR_ZONE_START, config);

    if (err)
        perror("zone_start: ioctl failed");

    close(fd);

    return 0;
err_out:
    if (root)
        cJSON_Delete(root);
    if (buffer)
        free(buffer);
    return -1;
}

// ./hvisor zone start <path_to_config_file>
static int zone_start(int argc, char *argv[]) {
    int zone_id;
    char *json_config_path = NULL;
    zone_config_t config;

    if (argc != 4) {
        help(1);
    }
    json_config_path = argv[3];

    memset(&config, 0, sizeof(zone_config_t));
    return zone_start_from_json(json_config_path, &config);
}

// ./hvisor zone shutdown -id 1
static int zone_shutdown(int argc, char *argv[]) {
    if (argc != 2 || strcmp(argv[0], "-id") != 0) {
        help(1);
    }
    __u64 zone_id;
    sscanf(argv[1], "%llu", &zone_id);
    int fd = open_dev();
    int err = ioctl(fd, HVISOR_ZONE_SHUTDOWN, zone_id);
    if (err)
        perror("zone_shutdown: ioctl failed");
    close(fd);
    return err;
}

static void print_cpu_list(__u64 cpu_mask, char *outbuf, size_t bufsize) {
    int found_cpu = 0;
    char *buf = outbuf;

    for (int i = 0; i < MAX_CPUS && buf - outbuf < bufsize; i++) {
        if ((cpu_mask & (1ULL << i)) != 0) {
            if (found_cpu) {
                *buf++ = ',';
                *buf++ = ' ';
            }
            snprintf(buf, bufsize - (buf - outbuf), "%d", i);
            buf += strlen(buf);
            found_cpu = 1;
        }
    }
    if (!found_cpu) {
        memcpy(outbuf, "none", 5);
    }
}

// ./hvisor zone list
static int zone_list(int argc, char *argv[]) {
    if (argc != 0) {
        help(1);
    }
    __u64 cnt = CONFIG_MAX_ZONES;
    zone_info_t *zones = malloc(sizeof(zone_info_t) * cnt);
    zone_list_args_t args = {cnt, zones};
    // printf("zone_list: cnt %llu, zones %p\n", cnt, zones);
    int fd = open_dev();
    int ret = ioctl(fd, HVISOR_ZONE_LIST, &args);
    if (ret < 0)
        perror("zone_list: ioctl failed");

    printf("| %11s     | %10s        | %9s       |\n", "zone_id", "cpus",
           "name");

    for (int i = 0; i < ret; i++) {
        char cpu_list_str[256]; // Assuming this buffer size is enough
        memset(cpu_list_str, 0, sizeof(cpu_list_str));
        print_cpu_list(zones[i].cpus, cpu_list_str, sizeof(cpu_list_str));
        printf("| %15u | %17s | %15s |\n", zones[i].zone_id, cpu_list_str,
               zones[i].name);
    }
    free(zones);
    close(fd);
    return ret;
}

int main(int argc, char *argv[]) {
    int err = 0;

    if (argc < 2)
        help(1);

    if (strcmp(argv[1], "zone") == 0) {
        if (strcmp(argv[2], "start") == 0) {
            err = zone_start(argc, argv);
        } else if (strcmp(argv[2], "shutdown") == 0) {
            err = zone_shutdown(argc - 3, &argv[3]);
        } else if (strcmp(argv[2], "list") == 0) {
            err = zone_list(argc - 3, &argv[3]);
        } else {
            help(1);
        }
    } else if (strcmp(argv[1], "virtio") == 0) {
        if (strcmp(argv[2], "start") == 0) {
            err = virtio_start(argc, argv);
        } else {
            help(1);
        }
    } else {
        help(1);
    }

    return err ? 1 : 0;
}

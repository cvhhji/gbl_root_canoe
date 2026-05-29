#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <locale.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define mkdir_p(d) _mkdir(d)
#define PATH_SEP '\\'
#else
#include <unistd.h>
#include <sys/stat.h>
#define mkdir_p(d) mkdir(d, 0755)
#define PATH_SEP '/'
#endif

#define AVB_MAGIC "AVB0"
#define AVB_VBMETA_IMAGE_HEADER_SIZE 256
#define AVB_FOOTER_MAGIC "AVBf"
#define AVB_FOOTER_SIZE 64

#define MAX_PATH_LEN 512
#define MAX_CMD_LEN 2048
#define MAX_INPUT_LEN 256

static const char *fastboot_path = "fastboot";
static int is_chinese = 0;

/* ---- big-endian helpers ---- */
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static uint64_t be64(const uint8_t *p) {
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (v >> 24) & 0xff;
    p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8)  & 0xff;
    p[3] = v & 0xff;
}

static void put_be64(uint8_t *p, uint64_t v) {
    put_be32(p, (uint32_t)(v >> 32));
    put_be32(p + 4, (uint32_t)v);
}

/* ---- file I/O ---- */
static uint8_t *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0) { fclose(f); return NULL; }

    uint8_t *buf = malloc(sz);
    if (!buf) { fclose(f); return NULL; }

    if (fread(buf, 1, sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = sz;
    return buf;
}

static int write_file(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    if (fwrite(data, 1, size, f) != size) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

/* ---- exe directory ---- */
static int get_exe_dir(char *buf, size_t buf_size) {
#ifdef _WIN32
    extern unsigned long __stdcall GetModuleFileNameA(void*, char*, unsigned long);
    unsigned long len = GetModuleFileNameA(NULL, buf, (unsigned long)buf_size);
    if (len == 0 || len >= buf_size) return -1;
#else
    ssize_t len = readlink("/proc/self/exe", buf, buf_size - 1);
    if (len <= 0) return -1;
    buf[len] = '\0';
#endif
    char *last_sep = strrchr(buf, PATH_SEP);
#ifdef _WIN32
    if (!last_sep) last_sep = strrchr(buf, '/');
#endif
    if (last_sep)
        *last_sep = '\0';
    else
        strcpy(buf, ".");
    return 0;
}

/* ---- AVB footer / transplant ---- */
static int read_avb_footer(const uint8_t *data, size_t len,
                           uint64_t *original_size, uint64_t *vbmeta_offset,
                           uint64_t *vbmeta_size) {
    if (len < AVB_FOOTER_SIZE)
        return 0;
    const uint8_t *footer = data + len - AVB_FOOTER_SIZE;
    if (memcmp(footer, AVB_FOOTER_MAGIC, 4) != 0)
        return 0;
    *original_size = be64(footer + 12);
    *vbmeta_offset = be64(footer + 20);
    *vbmeta_size   = be64(footer + 28);
    return 1;
}

static void create_avb_footer(uint8_t *footer,
                               uint64_t original_size, uint64_t vbmeta_offset,
                               uint64_t vbmeta_size) {
    memset(footer, 0, AVB_FOOTER_SIZE);
    memcpy(footer, AVB_FOOTER_MAGIC, 4);
    put_be32(footer + 4, 1);
    put_be32(footer + 8, 0);
    put_be64(footer + 12, original_size);
    put_be64(footer + 20, vbmeta_offset);
    put_be64(footer + 28, vbmeta_size);
}

static int transplant_vbmeta(const char *vbmeta_path, const char *source_image,
                              const char *output_path) {
    size_t vbmeta_size;
    uint8_t *vbmeta_data = read_file(vbmeta_path, &vbmeta_size);
    if (!vbmeta_data) {
        if(is_chinese) fprintf(stderr, "读取 vbmeta 失败: %s\n", vbmeta_path);
        else fprintf(stderr, "Failed to read vbmeta: %s\n", vbmeta_path);
        return -1;
    }

    size_t target_size;
    uint8_t *target_data = read_file(source_image, &target_size);
    if (!target_data) {
        if(is_chinese) fprintf(stderr, "读取镜像失败: %s\n", source_image);
        else fprintf(stderr, "Failed to read source image: %s\n", source_image);
        free(vbmeta_data);
        return -1;
    }

    uint64_t original_size;
    uint64_t existing_offset, existing_size;
    if (read_avb_footer(target_data, target_size, &original_size, &existing_offset, &existing_size)) {
        if(is_chinese) printf("  目标已存在 VBMeta，原始数据大小: %llu\n", (unsigned long long)original_size);
        else printf("  Target has existing VBMeta, original data size: %llu\n", (unsigned long long)original_size);
    } else {
        original_size = target_size - vbmeta_size - AVB_FOOTER_SIZE;
        if(is_chinese) printf("  目标无 VBMeta，计算原始大小: %llu\n", (unsigned long long)original_size);
        else printf("  Target has no VBMeta, calculated original size: %llu\n", (unsigned long long)original_size);
    }

    uint64_t vbmeta_offset = original_size;
    uint64_t footer_offset = target_size - AVB_FOOTER_SIZE;
    uint64_t required = original_size + vbmeta_size + AVB_FOOTER_SIZE;

    if (required > target_size) {
        if(is_chinese) fprintf(stderr, "空间不足: 需要 %llu，现有 %llu\n", (unsigned long long)required, (unsigned long long)target_size);
        else fprintf(stderr, "Insufficient space: need %llu, have %llu\n", (unsigned long long)required, (unsigned long long)target_size);
        free(vbmeta_data);
        free(target_data);
        return -1;
    }

    uint8_t *output = calloc(1, target_size);
    if (!output) {
        free(vbmeta_data);
        free(target_data);
        return -1;
    }

    memcpy(output, target_data, (size_t)original_size);
    memcpy(output + vbmeta_offset, vbmeta_data, vbmeta_size);
    create_avb_footer(output + footer_offset, original_size, vbmeta_offset, vbmeta_size);

    free(target_data);
    free(vbmeta_data);

    if (write_file(output_path, output, target_size) != 0) {
        if(is_chinese) fprintf(stderr, "写入修补镜像失败: %s\n", output_path);
        else fprintf(stderr, "Failed to write transplanted image: %s\n", output_path);
        free(output);
        return -1;
    }

    uint64_t v_orig, v_off, v_sz;
    if (read_avb_footer(output, target_size, &v_orig, &v_off, &v_sz)) {
        if (v_off + v_sz <= target_size && memcmp(output + v_off, AVB_MAGIC, 4) == 0) {
            if(is_chinese) printf("  VBMeta 修补验证成功\n");
            else printf("  VBMeta transplant verified OK\n");
        } else {
            if(is_chinese) fprintf(stderr, "  VBMeta 修补验证失败\n");
            else fprintf(stderr, "  VBMeta transplant verification failed\n");
            free(output);
            return -1;
        }
    }

    free(output);
    return 0;
}

/* ---- partition ---- */
static void strip_slot_suffix(const char *partition, char *base, size_t base_size) {
    size_t len = strlen(partition);
    if (len > 3 && strcmp(partition + len - 3, "_ab") == 0) {
        snprintf(base, base_size, "%.*s", (int)(len - 3), partition);
    } else if (len > 2 && (strcmp(partition + len - 2, "_a") == 0 || strcmp(partition + len - 2, "_b") == 0)) {
        snprintf(base, base_size, "%.*s", (int)(len - 2), partition);
    } else {
        snprintf(base, base_size, "%s", partition);
    }
}

/* ---- backup ---- */
static int run_backup(const char *exe_dir) {
    char backup_bin[MAX_PATH_LEN];
    char vbmetas_dir[MAX_PATH_LEN];
    char cmd[MAX_CMD_LEN];

#ifdef _WIN32
    snprintf(backup_bin, sizeof(backup_bin), "%s\\bin\\vbmetabackup.exe", exe_dir);
#else
    snprintf(backup_bin, sizeof(backup_bin), "%s/bin/vbmetabackup", exe_dir);
#endif
    snprintf(vbmetas_dir, sizeof(vbmetas_dir), "%s%cvbmetas", exe_dir, PATH_SEP);

    if (!file_exists(backup_bin)) {
        if(is_chinese) fprintf(stderr, "备份工具不存在: %s\n", backup_bin);
        else fprintf(stderr, "Backup tool not found: %s\n", backup_bin);
        return -1;
    }

    if(is_chinese){
        printf("==========================================================\n");
        printf("未检测到备份！必须先备份 VBMeta 才能继续\n");
        printf("==========================================================\n\n");
        printf("请操作：\n");
        printf("  1. 重启手机进入安卓系统\n");
        printf("  2. 连接 USB\n");
        printf("  3. 给 ADB 授权 root 权限\n\n");
        printf("按回车开始备份...");
    }else{
        printf("==========================================================\n");
        printf("No backup found. VBMeta backup is required before flashing.\n");
        printf("==========================================================\n\n");
        printf("Please:\n");
        printf("  1. Reboot device to Android\n");
        printf("  2. Connect via USB\n");
        printf("  3. Grant root access to ADB shell\n\n");
        printf("Press Enter to start backup...");
    }

    fflush(stdout);
    getchar();

    snprintf(cmd, sizeof(cmd), "\"%s\" -o \"%s\"", backup_bin, vbmetas_dir);
    printf("\n");
    int ret = system(cmd);

    if (ret != 0) {
        if(is_chinese) fprintf(stderr, "备份失败 (代码=%d)\n", ret);
        else fprintf(stderr, "Backup failed (ret=%d)\n", ret);
        return -1;
    }
    return 0;
}

static int check_and_run_backup(const char *exe_dir) {
    char marker[MAX_PATH_LEN];
    snprintf(marker, sizeof(marker), "%s%cfinish_backup", exe_dir, PATH_SEP);
    if (file_exists(marker)) return 0;

    if (run_backup(exe_dir) != 0) return -1;

    FILE *f = fopen(marker, "w");
    if (f) { fprintf(f, "done\n"); fclose(f); }

    if(is_chinese) printf("\n✅ 备份完成！\n\n");
    else printf("\n✅ Backup complete.\n\n");

    Sleep(1000);
    return 0;
}

/* ---- flash ---- */
static int flash_partition(const char *partition, const char *image_path) {
    char cmd[MAX_CMD_LEN];
    snprintf(cmd, sizeof(cmd), "%s flash %s \"%s\"", fastboot_path, partition, image_path);
    printf("$ %s\n", cmd);
    int ret = system(cmd);
    if (ret != 0) {
        if(is_chinese) fprintf(stderr, "❌ 刷写失败 (代码=%d)\n", ret);
        else fprintf(stderr, "❌ Flash failed (ret=%d)\n", ret);
    }
    return ret;
}

/* ---- reboot menu ---- */
static void show_reboot_menu() {
    if(is_chinese){
        printf("\n==================== 重启选项 ====================\n");
        printf("1. 重启到系统\n");
        printf("2. 重启到 Recovery\n");
        printf("3. 重启到 FastbootD\n");
        printf("4. 重启到 Bootloader\n");
        printf("5. 不重启，继续修补镜像\n");
        printf("==================================================\n");
        printf("请选择 [1-5]: ");
    }else{
        printf("\n==================== Reboot Menu ====================\n");
        printf("1. Reboot to System\n");
        printf("2. Reboot to Recovery\n");
        printf("3. Reboot to FastbootD\n");
        printf("4. Reboot to Bootloader\n");
        printf("5. Do not reboot, continue patching\n");
        printf("=====================================================\n");
        printf("Select [1-5]: ");
    }
}

static void run_reboot(int choice) {
    char cmd[MAX_CMD_LEN];
    switch(choice){
        case 1: snprintf(cmd, sizeof(cmd), "%s reboot", fastboot_path); break;
        case 2: snprintf(cmd, sizeof(cmd), "%s reboot recovery", fastboot_path); break;
        case 3: snprintf(cmd, sizeof(cmd), "%s reboot fastboot", fastboot_path); break;
        case 4: snprintf(cmd, sizeof(cmd), "%s reboot bootloader", fastboot_path); break;
        default: return;
    }
    if(is_chinese) printf("\n执行重启命令...\n");
    else printf("\nRebooting...\n");
    system(cmd);
    Sleep(1500);
}

static int ask_continue_or_exit() {
    int c;
    if(is_chinese){
        printf("\n==================== 操作完成 ====================\n");
        printf("1. 继续修补其他镜像\n");
        printf("2. 退出程序\n");
        printf("请选择 [1-2]: ");
    }else{
        printf("\n==================== Completed ====================\n");
        printf("1. Continue patching another image\n");
        printf("2. Exit program\n");
        printf("Select [1-2]: ");
    }
    fflush(stdout);
    scanf("%d", &c); getchar();
    return (c == 1) ? 1 : 0;
}

/* ---- input ---- */
static void read_line(const char *prompt, char *buf, size_t size) {
    printf("%s", prompt);
    fflush(stdout);
    if (fgets(buf, (int)size, stdin))
        buf[strcspn(buf, "\r\n")] = '\0';
}

/* ---- language detect ---- */
static void detect_language() {
#ifdef _WIN32
    LANGID lang = GetUserDefaultLangID();
    if((lang & 0x3FF) == 0x04) is_chinese = 1;
#else
    char *lang = getenv("LANG");
    if(lang && strstr(lang, "zh_CN")) is_chinese = 1;
#endif
}

/* ---- main patch flow ---- */
static int patch_and_flash(const char *exe_dir) {
    char partition_buf[MAX_INPUT_LEN];
    char image_buf[MAX_PATH_LEN];
    const char *partition_arg, *image_arg;

    if(is_chinese) read_line("请输入分区名 (如 boot_a): ", partition_buf, sizeof(partition_buf));
    else read_line("Enter partition (e.g. boot_a): ", partition_buf, sizeof(partition_buf));
    partition_arg = partition_buf;

    if(is_chinese) read_line("请输入要修补的镜像路径: ", image_buf, sizeof(image_buf));
    else read_line("Enter image path to patch: ", image_buf, sizeof(image_buf));
    image_arg = image_buf;

    if (!file_exists(image_arg)) {
        if(is_chinese) fprintf(stderr, "❌ 镜像不存在\n");
        else fprintf(stderr, "❌ Image not found\n");
        return -1;
    }

    char base_name[MAX_INPUT_LEN];
    strip_slot_suffix(partition_arg, base_name, sizeof(base_name));
    if(is_chinese) printf("分区: %s (基准: %s)\n", partition_arg, base_name);
    else printf("Partition: %s (base: %s)\n", partition_arg, base_name);

    char vbmeta_path[MAX_PATH_LEN];
    snprintf(vbmeta_path, sizeof(vbmeta_path), "%s%cvbmetas%c%s.vbmeta", exe_dir, PATH_SEP, PATH_SEP, base_name);

    const char *flash_image = image_arg;
    char temp_image[MAX_PATH_LEN] = {0};

    if (file_exists(vbmeta_path)) {
        if(is_chinese) printf("✅ 找到 VBMeta 备份: %s\n", vbmeta_path);
        else printf("✅ Found vbmeta backup: %s\n", vbmeta_path);

        char temp_dir[MAX_PATH_LEN];
        snprintf(temp_dir, sizeof(temp_dir), "%s%ctemp", exe_dir, PATH_SEP);
        mkdir_p(temp_dir);
        snprintf(temp_image, sizeof(temp_image), "%s%c%s.img", temp_dir, PATH_SEP, partition_arg);

        if(is_chinese) printf("正在修补 VBMeta...\n");
        else printf("Patching VBMeta...\n");

        if (transplant_vbmeta(vbmeta_path, image_arg, temp_image) != 0) {
            if(is_chinese) fprintf(stderr, "❌ 修补失败\n");
            else fprintf(stderr, "❌ Patch failed\n");
            remove(temp_image);
            return -1;
        }
        if(is_chinese) printf("✅ 镜像修补完成！\n", temp_image);
        else printf("✅ Image patched successfully!\n", temp_image);
        flash_image = temp_image;
    }

    if(is_chinese) printf("\n按回车进入 Fastboot 并开始刷写...\n");
    else printf("\nPress Enter to enter Fastboot and flash...\n");
    getchar();

    int ret = flash_partition(partition_arg, flash_image);
    if (temp_image[0]) remove(temp_image);

    if (ret == 0) {
        if(is_chinese) printf("\n✅ 刷写成功！\n");
        else printf("\n✅ Flash success!\n");
    }
    return ret;
}

/* ---- main ---- */
int main() {
    setlocale(LC_ALL, "");
    detect_language();

    char exe_dir[MAX_PATH_LEN];
    if (get_exe_dir(exe_dir, sizeof(exe_dir)) != 0) {
        if(is_chinese) fprintf(stderr, "获取程序目录失败\n");
        else fprintf(stderr, "Failed to get exe dir\n");
        return 1;
    }

    if(is_chinese) printf("工作目录: %s\n", exe_dir);
    else printf("Working dir: %s\n", exe_dir);

    if (check_and_run_backup(exe_dir) != 0) {
        if(is_chinese) printf("按回车退出...\n");
        else printf("Press Enter to exit...\n");
        getchar();
        return 1;
    }

    while(1){
        int ok = patch_and_flash(exe_dir);
        if(ok != 0){
            if(is_chinese) printf("\n流程异常，按回车继续...\n");
            else printf("\nError, press Enter to continue...\n");
            getchar();
            continue;
        }

        show_reboot_menu();
        int ch; scanf("%d", &ch); getchar();

        if(ch >=1 && ch <=4){
            run_reboot(ch);
            int cont = ask_continue_or_exit();
            if(!cont) break;
        }else{
            if(is_chinese) printf("\n不重启，继续修补...\n");
            else printf("\nContinue patching...\n");
        }
    }

    if(is_chinese) printf("\n程序退出\n");
    else printf("\nExit\n");
    return 0;
}

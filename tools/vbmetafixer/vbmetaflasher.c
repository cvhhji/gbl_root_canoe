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
#include <langinfo.h>
#define mkdir_p(d) mkdir(d, 0755)
#define PATH_SEP '/'
#endif

#define AVB_MAGIC "AVB0"
#define AVB_VBMETA_IMAGE_HEADER_SIZE 256
#define AVB_FOOTER_MAGIC "AVBf"
#define AVB_FOOTER_SIZE 64

#define MAX_PATH_LEN 512
#define MAX_CMD_LEN 1024
#define MAX_INPUT_LEN 256

static const char *fastboot_path = "fastboot";
static const char *adb_path = "adb";
static int is_chinese = 0;

static const char *txt_press_enter_fastboot_cn = "\n按回车键，设备将重启进入 Fastboot...";
static const char *txt_press_enter_fastboot_en = "\nPress Enter to reboot device to Fastboot...";
static const char *txt_flash_success_cn = "\n✅ 分区刷写成功！\n";
static const char *txt_flash_success_en = "\n✅ Partition flash completed!\n";
static const char *txt_flash_fail_cn = "\n❌ 分区刷写失败！请检查连接与分区名称\n";
static const char *txt_flash_fail_en = "\n❌ Flash failed! Please check connection and partition name\n";
static const char *txt_select_reboot_opt_cn = "\n请选择操作：\n1. 重启到 Recovery\n2. 重启到 Fastbootd\n3. 重启到 Bootloader\n4. 重启到系统\n5. 不重启，继续修补\n请输入选项(1-5)：";
static const char *txt_select_reboot_opt_en = "\nSelect operation:\n1. Reboot to Recovery\n2. Reboot to Fastbootd\n3. Reboot to Bootloader\n4. Reboot to System\n5. Continue without reboot\nEnter option(1-5): ";
static const char *txt_invalid_opt_cn = "选项无效，请重新输入！\n";
static const char *txt_invalid_opt_en = "Invalid option, please try again!\n";
static const char *txt_reboot_exec_cn = "正在执行重启命令...\n";
static const char *txt_reboot_exec_en = "Executing reboot command...\n";
static const char *txt_after_reboot_select_cn = "\n重启完成，请选择：\n1. 退出程序\n2. 继续修补镜像\n请输入选项(1-2)：";
static const char *txt_after_reboot_select_en = "\nReboot finished, select:\n1. Exit program\n2. Continue patching image\nEnter option(1-2): ";
static const char *txt_exit_prog_cn = "已选择退出，程序结束\n";
static const char *txt_exit_prog_en = "Exit selected, program terminated\n";
static const char *txt_new_task_cn = "==================== 新任务 ====================\n";
static const char *txt_new_task_en = "==================== New Task ====================\n";
static const char *txt_input_part_cn = "请输入Fastboot分区名(如 boot_a / vbmeta_b): ";
static const char *txt_input_part_en = "Enter Fastboot partition name(e.g. boot_a / vbmeta_b): ";
static const char *txt_part_empty_cn = "分区名为空，退出程序\n";
static const char *txt_part_empty_en = "Partition name is empty, exit program\n";
static const char *txt_input_img_cn = "请输入待修补镜像完整路径: ";
static const char *txt_input_img_en = "Enter full path of image to patch: ";
static const char *txt_img_empty_cn = "镜像路径为空，退出程序\n";
static const char *txt_img_empty_en = "Image path is empty, exit program\n";
static const char *txt_img_not_exist_cn = "错误: 镜像文件不存在！\n";
static const char *txt_img_not_exist_en = "Error: Image file not found!\n";
static const char *txt_start_patch_cn = "\n开始修补镜像...\n";
static const char *txt_start_patch_en = "\nStart patching image...\n";
static const char *txt_patch_ok_cn = "✅ 镜像修补完成: %s\n";
static const char *txt_patch_ok_en = "✅ Image patched: %s\n";
static const char *txt_patch_fail_cn = "❌ 镜像修补失败！\n";
static const char *txt_patch_fail_en = "❌ Patch failed!\n";
static const char *txt_prog_dir_cn = "程序运行目录: %s\n\n";
static const char *txt_prog_dir_en = "Program running dir: %s\n\n";
static const char *txt_backup_tip1_cn = "==========================================================\n未检测到VBMeta备份，必须先完成备份才能继续！\n==========================================================\n\n请确保：\n  1. 设备正常进入安卓系统\n  2. USB已连接、开启USB调试并拥有ROOT权限\n\n按回车开始备份...";
static const char *txt_backup_tip1_en = "==========================================================\nVBMeta backup not found, backup is required first!\n==========================================================\n\nPlease ensure:\n  1. Device is running Android system\n  2. USB connected, USB Debugging & ROOT enabled\n\nPress Enter to start backup...";
static const char *txt_backup_exec_cn = "\n执行: %s\n";
static const char *txt_backup_exec_en = "\nExecute: %s\n";
static const char *txt_backup_fail_cn = "\nERROR: 备份执行失败！\n";
static const char *txt_backup_fail_en = "\nERROR: Backup failed!\n";
static const char *txt_backup_ok_cn = "\n✅ 备份完成！\n\n";
static const char *txt_backup_ok_en = "\n✅ Backup completed!\n\n";
static const char *txt_backup_bin_not_exist_cn = "\nERROR: 备份工具不存在: %s\n";
static const char *txt_backup_bin_not_exist_en = "\nERROR: Backup tool not found: %s\n";
static const char *txt_get_dir_fail_cn = "获取程序目录失败\n";
static const char *txt_get_dir_fail_en = "Failed to get program directory\n";
static const char *txt_press_enter_close_cn = "\n按回车键关闭窗口...";
static const char *txt_press_enter_close_en = "\nPress Enter to close window...";
static const char *txt_reboot_fb_cn = "\n>>> 正在重启设备进入 Fastboot 模式...\n";
static const char *txt_reboot_fb_en = "\n>>> Rebooting device to Fastboot...\n";
static const char *txt_exec_cmd_cn = "执行命令: %s\n";
static const char *txt_exec_cmd_en = "Execute command: %s\n";
static const char *txt_fb_manual_cn = "⚠️  自动进入Fastboot失败，请手动进入！\n";
static const char *txt_fb_manual_en = "⚠️  Auto enter Fastboot failed, please enter manually!\n";

static void detect_language(void)
{
#ifdef _WIN32
    WCHAR lang_buf[64];
    GetUserDefaultLocaleName(lang_buf, 64);
    if (wcsstr(lang_buf, L"zh") != NULL)
        is_chinese = 1;
#else
    setlocale(LC_ALL, "");
    const char *lang = nl_langinfo(LANG);
    if (strstr(lang, "zh") != NULL)
        is_chinese = 1;
#endif
}

static const char* get_txt(const char *cn, const char *en)
{
    return is_chinese ? cn : en;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static uint64_t be64(const uint8_t *p)
{
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (v >> 24) & 0xff;
    p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8)  & 0xff;
    p[3] = v & 0xff;
}

static void put_be64(uint8_t *p, uint64_t v)
{
    put_be32(p, (uint32_t)(v >> 32));
    put_be32(p + 4, (uint32_t)v);
}

static uint8_t *read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *buf = malloc(sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, sz, f) != (size_t)sz)
    {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = sz;
    return buf;
}

static int write_file(const char *path, const uint8_t *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (fwrite(data, 1, size, f) != size)
    {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

static int get_exe_dir(char *buf, size_t buf_size)
{
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

static int read_avb_footer(const uint8_t *data, size_t len,
                           uint64_t *original_size, uint64_t *vbmeta_offset,
                           uint64_t *vbmeta_size)
{
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
                               uint64_t vbmeta_size)
{
    memset(footer, 0, AVB_FOOTER_SIZE);
    memcpy(footer, AVB_FOOTER_MAGIC, 4);
    put_be32(footer + 4, 1);
    put_be32(footer + 8, 0);
    put_be64(footer + 12, original_size);
    put_be64(footer + 20, vbmeta_offset);
    put_be64(footer + 28, vbmeta_size);
}

static int transplant_vbmeta(const char *vbmeta_path, const char *source_image,
                              const char *output_path)
{
    size_t vbmeta_size;
    uint8_t *vbmeta_data = read_file(vbmeta_path, &vbmeta_size);
    if (!vbmeta_data)
    {
        fprintf(stderr, "%s %s\n", get_txt("读取VBMeta失败:", "Read VBMeta failed:"), vbmeta_path);
        return -1;
    }
    size_t target_size;
    uint8_t *target_data = read_file(source_image, &target_size);
    if (!target_data)
    {
        fprintf(stderr, "%s %s\n", get_txt("读取镜像失败:", "Read image failed:"), source_image);
        free(vbmeta_data);
        return -1;
    }
    uint64_t original_size;
    uint64_t existing_offset, existing_size;
    if (read_avb_footer(target_data, target_size,
                        &original_size, &existing_offset, &existing_size))
    {
        printf("  %s %llu\n", get_txt("检测到已有AVB Footer，原始数据大小:", "AVB Footer found, original size:"),
               (unsigned long long)original_size);
    }
    else
    {
        original_size = target_size - vbmeta_size - AVB_FOOTER_SIZE;
        printf("  %s %llu\n", get_txt("无AVB Footer，计算原始数据大小:", "No AVB Footer, calculated original size:"),
               (unsigned long long)original_size);
    }
    uint64_t vbmeta_offset = original_size;
    uint64_t footer_offset = target_size - AVB_FOOTER_SIZE;
    uint64_t required = original_size + vbmeta_size + AVB_FOOTER_SIZE;
    if (required > target_size)
    {
        fprintf(stderr, "%s %llu %s %llu\n", get_txt("空间不足: 需要", "No enough space: need"),
                (unsigned long long)required, get_txt("字节，当前", "bytes, current"), (unsigned long long)target_size);
        free(vbmeta_data);
        free(target_data);
        return -1;
    }
    uint8_t *output = calloc(1, target_size);
    if (!output)
    {
        free(vbmeta_data);
        free(target_data);
        return -1;
    }
    memcpy(output, target_data, (size_t)original_size);
    memcpy(output + vbmeta_offset, vbmeta_data, vbmeta_size);
    create_avb_footer(output + footer_offset, original_size, vbmeta_offset, vbmeta_size);
    free(target_data);
    free(vbmeta_data);
    if (write_file(output_path, output, target_size) != 0)
    {
        fprintf(stderr, "%s\n", get_txt("写入修补镜像失败:", "Write patched image failed:"));
        free(output);
        return -1;
    }
    uint64_t v_orig, v_off, v_sz;
    if (read_avb_footer(output, target_size, &v_orig, &v_off, &v_sz))
    {
        if (v_off + v_sz <= target_size && memcmp(output + v_off, AVB_MAGIC, 4) == 0)
        {
            free(output);
            return 0;
        }
    }
    free(output);
    fprintf(stderr, "%s\n", get_txt("VBMeta校验失败", "VBMeta verify failed"));
    return -1;
}

static void strip_slot_suffix(const char *partition, char *base, size_t base_size)
{
    size_t len = strlen(partition);
    if (len > 3 && strcmp(partition + len - 3, "_ab") == 0)
    {
        snprintf(base, base_size, "%.*s", (int)(len - 3), partition);
    }
    else if (len > 2 && (strcmp(partition + len - 2, "_a") == 0 ||
                         strcmp(partition + len - 2, "_b") == 0))
    {
        snprintf(base, base_size, "%.*s", (int)(len - 2), partition);
    }
    else
    {
        snprintf(base, base_size, "%s", partition);
    }
}

static int reboot_fastboot(void)
{
    char cmd[MAX_CMD_LEN];
    printf("%s", get_txt(txt_reboot_fb_cn, txt_reboot_fb_en));
    snprintf(cmd, sizeof(cmd), "%s reboot bootloader", adb_path);
    printf("%s %s\n", get_txt(txt_exec_cmd_cn, txt_exec_cmd_en), cmd);
    int ret = system(cmd);
    if (ret != 0)
    {
        fprintf(stderr, "%s\n", get_txt(txt_fb_manual_cn, txt_fb_manual_en));
    }
    return ret;
}

static int flash_partition(const char *partition, const char *image_path)
{
    char cmd[MAX_CMD_LEN];
    printf("\n>>> %s %s ...\n", get_txt("开始刷写分区", "Start flash partition"), partition);
    snprintf(cmd, sizeof(cmd), "%s flash %s \"%s\"", fastboot_path, partition, image_path);
    printf("%s %s\n", get_txt(txt_exec_cmd_cn, txt_exec_cmd_en), cmd);
    return system(cmd);
}

static void read_line(const char *prompt, char *buf, size_t size)
{
    printf("%s", prompt);
    fflush(stdout);
    if (fgets(buf, (int)size, stdin))
        buf[strcspn(buf, "\r\n")] = '\0';
}

static int run_backup(const char *exe_dir)
{
    char backup_bin[MAX_PATH_LEN];
    char vbmetas_dir[MAX_PATH_LEN];
    char cmd[MAX_CMD_LEN];

#ifdef _WIN32
    snprintf(backup_bin, sizeof(backup_bin), "%s\\bin\\vbmetabackup.exe", exe_dir);
#else
    snprintf(backup_bin, sizeof(backup_bin), "%s/bin/vbmetabackup", exe_dir);
#endif
    snprintf(vbmetas_dir, sizeof(vbmetas_dir), "%s%cvbmetas", exe_dir, PATH_SEP);

    if (!file_exists(backup_bin))
    {
        fprintf(stderr, "%s %s\n", get_txt(txt_backup_bin_not_exist_cn, txt_backup_bin_not_exist_en), backup_bin);
        printf("%s", get_txt(txt_press_enter_close_cn, txt_press_enter_close_en));
        getchar();
        return -1;
    }

    printf("%s", get_txt(txt_backup_tip1_cn, txt_backup_tip1_en));
    fflush(stdout);
    getchar();

    snprintf(cmd, sizeof(cmd), "%s -o %s", backup_bin, vbmetas_dir);
    printf("%s %s\n", get_txt(txt_backup_exec_cn, txt_backup_exec_en), cmd);

    int ret = system(cmd);
    if (ret != 0)
    {
        fprintf(stderr, "%s\n", get_txt(txt_backup_fail_cn, txt_backup_fail_en));
        printf("%s", get_txt(txt_press_enter_close_cn, txt_press_enter_close_en));
        getchar();
        return -1;
    }
    return 0;
}

static int check_and_run_backup(const char *exe_dir)
{
    char marker[MAX_PATH_LEN];
    snprintf(marker, sizeof(marker), "%s%cfinish_backup", exe_dir, PATH_SEP);
    if (file_exists(marker))
        return 0;

    if (run_backup(exe_dir) != 0)
        return -1;

    FILE *f = fopen(marker, "w");
    if (f)
    {
        fprintf(f, "done\n");
        fclose(f);
    }
    printf("%s", get_txt(txt_backup_ok_cn, txt_backup_ok_en));
    return 0;
}

static int exec_reboot(int opt)
{
    char cmd[MAX_CMD_LEN];
    printf("%s", get_txt(txt_reboot_exec_cn, txt_reboot_exec_en));
    switch (opt)
    {
        case 1:
            snprintf(cmd, sizeof(cmd), "%s reboot recovery", adb_path);
            break;
        case 2:
            snprintf(cmd, sizeof(cmd), "%s reboot fastbootd", adb_path);
            break;
        case 3:
            snprintf(cmd, sizeof(cmd), "%s reboot bootloader", adb_path);
            break;
        case 4:
            snprintf(cmd, sizeof(cmd), "%s reboot", adb_path);
            break;
        default:
            return 0;
    }
    system(cmd);
    return 1;
}

int main(int argc, char **argv)
{
    // ========== 修复Windows中文乱码：设置控制台为UTF-8代码页 ==========
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    detect_language();
    char exe_dir[MAX_PATH_LEN];
    if (get_exe_dir(exe_dir, sizeof(exe_dir)) != 0)
    {
        fprintf(stderr, "%s\n", get_txt(txt_get_dir_fail_cn, txt_get_dir_fail_en));
        printf("%s", get_txt(txt_press_enter_close_cn, txt_press_enter_close_en));
        getchar();
        return 1;
    }
    printf("%s\n", get_txt(txt_prog_dir_cn, txt_prog_dir_en), exe_dir);

    if (check_and_run_backup(exe_dir) != 0)
        return 1;

    char partition_buf[MAX_INPUT_LEN];
    char image_buf[MAX_PATH_LEN];
    char temp_image[MAX_PATH_LEN];
    char vbmeta_path[MAX_PATH_LEN];
    char base_name[MAX_INPUT_LEN];
    char temp_dir[MAX_PATH_LEN];
    char choice[32];
    int keep_run = 1;

    snprintf(temp_dir, sizeof(temp_dir), "%s%ctemp", exe_dir, PATH_SEP);
    mkdir_p(temp_dir);

    while (keep_run)
    {
        printf("%s", get_txt(txt_new_task_cn, txt_new_task_en));
        read_line(get_txt(txt_input_part_cn, txt_input_part_en), partition_buf, sizeof(partition_buf));
        if (partition_buf[0] == '\0')
        {
            printf("%s\n", get_txt(txt_part_empty_cn, txt_part_empty_en));
            break;
        }

        read_line(get_txt(txt_input_img_cn, txt_input_img_en), image_buf, sizeof(image_buf));
        if (image_buf[0] == '\0')
        {
            printf("%s\n", get_txt(txt_img_empty_cn, txt_img_empty_en));
            break;
        }
        if (!file_exists(image_buf))
        {
            fprintf(stderr, "%s\n", get_txt(txt_img_not_exist_cn, txt_img_not_exist_en));
            continue;
        }

        strip_slot_suffix(partition_buf, base_name, sizeof(base_name));
        snprintf(vbmeta_path, sizeof(vbmeta_path), "%s%cvbmetas%c%s.vbmeta",
                 exe_dir, PATH_SEP, PATH_SEP, base_name);

        snprintf(temp_image, sizeof(temp_image), "%s%c%s.img",
                 temp_dir, PATH_SEP, partition_buf);

        printf("%s", get_txt(txt_start_patch_cn, txt_start_patch_en));
        if (transplant_vbmeta(vbmeta_path, image_buf, temp_image) != 0)
        {
            fprintf(stderr, "%s\n", get_txt(txt_patch_fail_cn, txt_patch_fail_en));
            continue;
        }
        printf(get_txt(txt_patch_ok_cn, txt_patch_ok_en), temp_image);

        printf("%s", get_txt(txt_press_enter_fastboot_cn, txt_press_enter_fastboot_en));
        fflush(stdout);
        getchar();
        reboot_fastboot();

        int flash_ret = flash_partition(partition_buf, temp_image);
        if (flash_ret == 0)
            printf("%s", get_txt(txt_flash_success_cn, txt_flash_success_en));
        else
            fprintf(stderr, "%s", get_txt(txt_flash_fail_cn, txt_flash_fail_en));

        remove(temp_image);

        int reboot_opt = 0;
        while (1)
        {
            read_line(get_txt(txt_select_reboot_opt_cn, txt_select_reboot_opt_en), choice, sizeof(choice));
            reboot_opt = atoi(choice);
            if (reboot_opt >= 1 && reboot_opt <= 5)
                break;
            printf("%s", get_txt(txt_invalid_opt_cn, txt_invalid_opt_en));
        }

        if (reboot_opt != 5)
        {
            exec_reboot(reboot_opt);
            int after_opt = 0;
            while (1)
            {
                read_line(get_txt(txt_after_reboot_select_cn, txt_after_reboot_select_en), choice, sizeof(choice));
                after_opt = atoi(choice);
                if (after_opt == 1 || after_opt == 2)
                    break;
                printf("%s", get_txt(txt_invalid_opt_cn, txt_invalid_opt_en));
            }
            if (after_opt == 1)
            {
                printf("%s\n", get_txt(txt_exit_prog_cn, txt_exit_prog_en));
                keep_run = 0;
            }
        }
    }

    printf("%s", get_txt(txt_press_enter_close_cn, txt_press_enter_close_en));
    getchar();
    return 0;
}

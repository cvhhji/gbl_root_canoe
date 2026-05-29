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

#define AVB_MAGIC               "AVB0"
#define AVB_VBMETA_IMAGE_HEADER_SIZE 256
#define AVB_FOOTER_MAGIC        "AVBf"
#define AVB_FOOTER_SIZE         64

#define MAX_PATH_LEN            512
#define MAX_CMD_LEN             2048
#define MAX_INPUT_LEN           256
#define MAX_PARTITION_NAME      128

static const char *fastboot_path = "fastboot";
static const char *adb_path = "adb";
static int is_chinese = 0;

static const char *txt_press_enter_fastboot_cn = "\n按回车键，设备将重启进入 Fastboot...";
static const char *txt_press_enter_fastboot_en = "\nPress Enter to reboot device to Fastboot...";
static const char *txt_flash_success_cn = "\n✅ 分区刷写成功！\n";
static const char *txt_flash_success_en = "\n✅ Partition flash completed!\n";
static const char *txt_flash_fail_cn = "\n❌ 分区刷写失败！请检查连接与镜像文件\n";
static const char *txt_flash_fail_en = "\n❌ Flash failed! Please check connection and image file\n";
static const char *txt_select_reboot_opt_cn = "\n请选择操作：\n1. 重启到 Recovery\n2. 重启到 Fastbootd\n3. 重启到 Bootloader\n4. 正常重启设备\n5. 不重启，继续操作\n请输入选项(1-5)：";
static const char *txt_select_reboot_opt_en = "\nSelect operation:\n1. Reboot to Recovery\n2. Reboot to Fastbootd\n3. Reboot to Bootloader\n4. Normal Reboot\n5. Continue without reboot\nEnter option(1-5): ";
static const char *txt_invalid_opt_cn = "选项无效，请重新输入！\n";
static const char *txt_invalid_opt_en = "Invalid option, please try again!\n";
static const char *txt_reboot_exec_cn = "正在执行重启命令...\n";
static const char *txt_reboot_exec_en = "Executing reboot command...\n";
static const char *txt_after_reboot_select_cn = "\n重启完成，请选择：\n1. 退出程序\n2. 继续修补镜像\n请输入选项(1-2)：";
static const char *txt_after_reboot_select_en = "\nReboot finished, select:\n1. Exit program\n2. Continue patching\nEnter option(1-2): ";
static const char *txt_exit_prog_cn = "已选择退出，程序结束\n";
static const char *txt_exit_prog_en = "Exit selected, program terminated\n";
static const char *txt_new_task_cn = "==================== 新任务 ====================\n";
static const char *txt_new_task_en = "==================== New Task ====================\n";
static const char *txt_input_part_cn = "请输入Fastboot分区名(如 boot / vbmeta): ";
static const char *txt_input_part_en = "Enter partition name(e.g. boot / vbmeta): ";
static const char *txt_part_empty_cn = "分区名为空，退出任务\n";
static const char *txt_part_empty_en = "Partition name empty, exit task\n";
static const char *txt_input_img_cn = "请输入镜像文件完整路径: ";
static const char *txt_input_img_en = "Enter full image path: ";
static const char *txt_img_empty_cn = "镜像路径为空，退出任务\n";
static const char *txt_img_empty_en = "Image path empty, exit task\n";
static const char *txt_img_not_exist_cn = "错误: 镜像文件不存在！\n";
static const char *txt_img_not_exist_en = "Error: Image file not found!\n";
static const char *txt_start_flash_cn = "\n开始刷写镜像...\n";
static const char *txt_start_flash_en = "\nStart flashing image...\n";
static const char *txt_flash_ok_cn = "✅ 镜像刷写完成: %s\n";
static const char *txt_flash_ok_en = "✅ Image flashed: %s\n";
static const char *txt_flash_err_cn = "❌ 镜像刷写失败！\n";
static const char *txt_flash_err_en = "❌ Flash failed!\n";
static const char *txt_prog_dir_cn = "程序运行目录: %s\n\n";
static const char *txt_prog_dir_en = "Program running dir: %s\n\n";
static const char *txt_backup_tip1_cn = "==========================================================\n未检测到VBMeta备份，必须先完成备份才能继续！\n==========================================================\n\n请确保：\n  1. 设备正常进入系统\n  2. USB调试、Root权限已开启\n\n按回车开始备份...";
static const char *txt_backup_tip1_en = "==========================================================\nVBMeta backup not found, backup required first!\n==========================================================\n\nPlease ensure:\n  1. Device booted normally\n  2. USB Debug & Root enabled\n\nPress Enter to start backup...";
static const char *txt_backup_exec_cn = "\n执行备份命令: %s\n";
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

static const char* get_txt(const char *cn, const char *en)
{
    return is_chinese ? cn : en;
}

static uint32_t be32(uint32_t val)
{
    uint8_t b[4];
    b[0] = (val >> 24) & 0xFF;
    b[1] = (val >> 16) & 0xFF;
    b[2] = (val >> 8)  & 0xFF;
    b[3] = val & 0xFF;
    return *(uint32_t*)b;
}

static uint64_t be64(uint64_t val)
{
    uint8_t b[8];
    b[0] = (val >> 56) & 0xFF;
    b[1] = (val >> 48) & 0xFF;
    b[2] = (val >> 40) & 0xFF;
    b[3] = (val >> 32) & 0xFF;
    b[4] = (val >> 24) & 0xFF;
    b[5] = (val >> 16) & 0xFF;
    b[6] = (val >> 8)  & 0xFF;
    b[7] = val & 0xFF;
    return *(uint64_t*)b;
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if(f)
    {
        fclose(f);
        return 1;
    }
    return 0;
}

static int get_exe_dir(char *out, size_t out_len)
{
#ifdef _WIN32
    char buf[MAX_PATH_LEN] = {0};
    if(!GetModuleFileNameA(NULL, buf, MAX_PATH_LEN))
        return -1;
    char *p = strrchr(buf, '\\');
    if(!p) return -1;
    *p = '\0';
    strncpy(out, buf, out_len - 1);
    out[out_len - 1] = '\0';
#else
    char buf[MAX_PATH_LEN] = {0};
    ssize_t len = readlink("/proc/self/exe", buf, MAX_PATH_LEN - 1);
    if(len <= 0) return -1;
    char *p = strrchr(buf, '/');
    if(!p) return -1;
    *p = '\0';
    strncpy(out, buf, out_len - 1);
    out[out_len - 1] = '\0';
#endif
    return 0;
}

static void detect_language(void)
{
#ifdef _WIN32
    WCHAR lang_buf[64];
    GetUserDefaultLocaleName(lang_buf, 64);
    if (wcsstr(lang_buf, L"zh") != NULL)
        is_chinese = 1;
#else
    setlocale(LC_ALL, "");
    const char *lang = nl_langinfo(NL_LANGINFO_LANG);
    if (strstr(lang, "zh") != NULL)
        is_chinese = 1;
#endif
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
    (void)system(cmd);
    return 1;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
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

    printf(get_txt(txt_prog_dir_cn, txt_prog_dir_en), exe_dir);

    char backup_bin[MAX_PATH_LEN];
    snprintf(backup_bin, sizeof(backup_bin), "%c%s%ctoolkit%cbin%cvbmetabackup",
        PATH_SEP, exe_dir, PATH_SEP, PATH_SEP, PATH_SEP);

    if (!file_exists(backup_bin))
    {
        fprintf(stderr, "%s\n", get_txt(txt_backup_bin_not_exist_cn, txt_backup_bin_not_exist_en), backup_bin);
        printf("%s", get_txt(txt_press_enter_close_cn, txt_press_enter_close_en));
        getchar();
        return 1;
    }

    char backup_cmd[MAX_CMD_LEN];
    snprintf(backup_cmd, sizeof(backup_cmd), "\"%s\"", backup_bin);
    if (!file_exists("vbmeta_backup.bin"))
    {
        printf("%s", get_txt(txt_backup_tip1_cn, txt_backup_tip1_en));
        getchar();
        printf("%s\n", get_txt(txt_backup_exec_cn, txt_backup_exec_en), backup_cmd);
        int ret = system(backup_cmd);
        if (ret != 0 || !file_exists("vbmeta_backup.bin"))
        {
            fprintf(stderr, "%s\n", get_txt(txt_backup_fail_cn, txt_backup_fail_en));
            printf("%s", get_txt(txt_press_enter_close_cn, txt_press_enter_close_en));
            getchar();
            return 1;
        }
        printf("%s", get_txt(txt_backup_ok_cn, txt_backup_ok_en));
    }

    int run_loop = 1;
    while (run_loop)
    {
        printf("%s", get_txt(txt_new_task_cn, txt_new_task_en));

        char part_name[MAX_INPUT_LEN] = {0};
        printf("%s", get_txt(txt_input_part_cn, txt_input_part_en));
        fgets(part_name, sizeof(part_name), stdin);
        size_t plen = strlen(part_name);
        if (plen > 0 && part_name[plen-1] == '\n')
            part_name[plen-1] = '\0';
        if (strlen(part_name) == 0)
        {
            printf("%s\n", get_txt(txt_part_empty_cn, txt_part_empty_en));
            continue;
        }

        char img_path[MAX_PATH_LEN] = {0};
        printf("%s", get_txt(txt_input_img_cn, txt_input_img_en));
        fgets(img_path, sizeof(img_path), stdin);
        size_t ilen = strlen(img_path);
        if (ilen > 0 && img_path[ilen-1] == '\n')
            img_path[ilen-1] = '\0';
        if (strlen(img_path) == 0)
        {
            printf("%s\n", get_txt(txt_img_empty_cn, txt_img_empty_en));
            continue;
        }
        if (!file_exists(img_path))
        {
            printf("%s\n", get_txt(txt_img_not_exist_cn, txt_img_not_exist_en));
            continue;
        }

        printf("%s", get_txt(txt_start_flash_cn, txt_start_flash_en));
        char flash_cmd[MAX_CMD_LEN];
        snprintf(flash_cmd, sizeof(flash_cmd), "%s flash %s \"%s\"", fastboot_path, part_name, img_path);
        int flash_ret = system(flash_cmd);
        if (flash_ret == 0)
            printf("%s\n", get_txt(txt_flash_ok_cn, txt_flash_ok_en), part_name);
        else
            printf("%s\n", get_txt(txt_flash_err_cn, txt_flash_err_en));

        int opt = 0;
        while (1)
        {
            printf("%s", get_txt(txt_select_reboot_opt_cn, txt_select_reboot_opt_en));
            char buf[16] = {0};
            fgets(buf, sizeof(buf), stdin);
            opt = atoi(buf);
            if (opt >= 1 && opt <=5)
                break;
            printf("%s\n", get_txt(txt_invalid_opt_cn, txt_invalid_opt_en));
        }

        if (opt != 5)
        {
            exec_reboot(opt);
            int next_opt = 0;
            while (1)
            {
                printf("%s", get_txt(txt_after_reboot_select_cn, txt_after_reboot_select_en));
                char buf[16] = {0};
                fgets(buf, sizeof(buf), stdin);
                next_opt = atoi(buf);
                if (next_opt ==1 || next_opt ==2)
                    break;
                printf("%s\n", get_txt(txt_invalid_opt_cn, txt_invalid_opt_en));
            }
            if (next_opt ==1)
                run_loop = 0;
        }
        else
        {
            int next_opt = 0;
            while (1)
            {
                printf("%s", get_txt(txt_after_reboot_select_cn, txt_after_reboot_select_en));
                char buf[16] = {0};
                fgets(buf, sizeof(buf), stdin);
                next_opt = atoi(buf);
                if (next_opt ==1 || next_opt ==2)
                    break;
                printf("%s\n", get_txt(txt_invalid_opt_cn, txt_invalid_opt_en));
            }
            if (next_opt ==1)
                run_loop = 0;
        }
    }

    printf("%s", get_txt(txt_press_enter_close_cn, txt_press_enter_close_en));
    getchar();
    return 0;
}

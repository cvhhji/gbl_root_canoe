#!/system/bin/sh
if [ -z "$MODDIR" ]; then
  MODDIR=$(CDPATH= cd -- "$(dirname "$0")/.." 2>/dev/null && pwd)
fi
if [ -z "$MODDIR" ]; then
  echo 'ERROR=MODDIR detection failed' >&2
  exit 1
fi

export MODPATH=$MODDIR
export BINDIR=$MODDIR/bin
export KSU_MODULE=${KSU_MODULE:-fake_bl_efisp}

USER_LANG=$(ksud module config get user_lang 2>/dev/null)
case "$USER_LANG" in
  zh|en) LANG=$USER_LANG ;;
  *)
    SYSTEM_LOCALE=$(getprop persist.sys.locale 2>/dev/null)
    [ -n "$SYSTEM_LOCALE" ] || SYSTEM_LOCALE=$(getprop ro.product.locale 2>/dev/null)
    case "$SYSTEM_LOCALE" in
      zh*) LANG=zh ;;
      *) LANG=en ;;
    esac
    ;;
esac

if [ "$LANG" = "zh" ]; then
  TEXT_IDLE="等待操作"
  TEXT_NO_SLOT="无法识别当前槽位"
  TEXT_NO_TARGET_SLOT="无法计算目标槽位"
  TEXT_CURRENT_SLOT_LABEL="当前槽位"
  TEXT_TARGET_SLOT_LABEL="目标槽位"
  TEXT_FLASHING="刷写任务运行中，目标槽位"
  TEXT_PATCH_ONLY="分区修补任务运行中"
  TEXT_DEBUG_MODE="调试模式：仅处理不刷写，efisp 目录使用模块 tmp/efisp"
  TEXT_DEBUG_DONE="调试任务已完成，文件保存在"
  TEXT_DEBUG_FAILED="调试过程中出错"
  TEXT_EXTRACT_FAILED="ABL 提取失败"
  TEXT_PATCH_FAILED="补丁应用失败"
  TEXT_PERSIST_NOT_MOUNTED="persist 分区未挂载到 /mnt/vendor/persist"
  TEXT_EFISP_MKDIR_FAILED="创建 efisp 启动目录失败"
  TEXT_EFISP_WRITE_FAILED="写入 efisp 启动文件失败"
  TEXT_BACKUP_BOOT="已备份旧的 boot.efi"
  TEXT_EFISP_FILES_OK="efisp 启动项已更新"
  TEXT_EFISP_SET_RW_FAILED="efisp 分区设置可写失败"
  TEXT_EFISP_FLASH_FAILED="efisp 刷写失败"
  TEXT_EFISP_FLASH_OK="efisp 刷写完成"
  TEXT_UPDATING_BDS_TOOLS="BDS 与 Tools 更新任务运行中"
  TEXT_BDS_TOOLS_OK="BDS 与 Tools 更新任务已完成"
  TEXT_BDS_TOOLS_FAIL="BDS 与 Tools 更新失败"
  TEXT_BDS_OLD_VER="你的假回锁是旧版，请OTA最新完整包并在重启前选择刷写到下一槽"
  TEXT_BDS_NOT_INSTALLED="你还没有安装假回锁，请重新安装模块选择全新安装"
  TEXT_GBL_VULN="检测到GBL漏洞，跳过BL刷写"
  TEXT_GBL_VULN_SKIP="已跳过BL刷写"
  TEXT_GBL_DETECT_FAILED="漏洞检测失败，继续流程"
  TEXT_NO_GBL_VULN="未检测到GBL漏洞"
  TEXT_EFISP_WARN="efisp 刷写失败，继续刷入BL"
  TEXT_SET_RW_FAILED="分区设置可写失败"
  TEXT_FLASH_PART="刷写"
  TEXT_FLASH_OK="完成"
  TEXT_ALL_OK="刷写任务已完成（含 efisp）"
  TEXT_ALL_OK_NO_EFISP="分区修补任务已完成（未刷写 ABL）"
  TEXT_BUSY="任务正在运行"
  TEXT_LOG_CLEARED="日志已清空"
  TEXT_PATCH_START="分区修补任务运行中"
  TEXT_PATCH_VENDORBOOT_START="修补 vendor_boot"
  TEXT_PATCH_VENDORBOOT_DONE="vendor_boot 修补完成"
  TEXT_PATCH_SUPER_START="修补 super 分区"
  TEXT_PATCH_SUPER_DONE="super 修补完成"
  TEXT_PATCH_DEBUG_SAVE="调试模式：跳过实际刷写"
  TEXT_PATCH_NO_SELECTED="未勾选任何需要修补的分区"
  TEXT_PATCH_BOTH_ERR="不能同时修补 vendor_boot 和 super"
  TEXT_PATCH_ERR="分区修补出错"
  TEXT_PATCH_DONE="分区修补任务已完成"
  TEXT_BIN_NOT_FOUND="修补文件未找到"
  TEXT_PATCH_ARGS="修补参数"
  TEXT_BIN_RUN_INFO="执行中"
  TEXT_PATCH_SLOT="目标槽位"
else
  TEXT_IDLE="Waiting"
  TEXT_NO_SLOT="Cannot detect current slot"
  TEXT_NO_TARGET_SLOT="Cannot detect target slot"
  TEXT_CURRENT_SLOT_LABEL="Current slot"
  TEXT_TARGET_SLOT_LABEL="Target slot"
  TEXT_FLASHING="Flash task running, target slot"
  TEXT_PATCH_ONLY="Partition patch task running"
  TEXT_DEBUG_MODE="Debug Mode: process only, no flash; efisp dir uses module tmp/efisp"
  TEXT_DEBUG_DONE="Debug task completed"
  TEXT_DEBUG_FAILED="Debug error"
  TEXT_EXTRACT_FAILED="ABL extract failed"
  TEXT_PATCH_FAILED="Patch failed"
  TEXT_PERSIST_NOT_MOUNTED="persist is not mounted at /mnt/vendor/persist"
  TEXT_EFISP_MKDIR_FAILED="efisp boot dir create failed"
  TEXT_EFISP_WRITE_FAILED="efisp boot file write failed"
  TEXT_BACKUP_BOOT="Backed up old boot.efi"
  TEXT_EFISP_FILES_OK="efisp boot entries updated"
  TEXT_EFISP_SET_RW_FAILED="efisp setrw failed"
  TEXT_EFISP_FLASH_FAILED="efisp flash failed"
  TEXT_EFISP_FLASH_OK="efisp flash ok"
  TEXT_UPDATING_BDS_TOOLS="BDS and Tools update task running"
  TEXT_BDS_TOOLS_OK="BDS and Tools update task completed"
  TEXT_BDS_TOOLS_FAIL="BDS and Tools update failed"
  TEXT_BDS_OLD_VER="Your fake-lock is an old version. Please OTA the latest full package and select 'Flash to other slot' before rebooting."
  TEXT_BDS_NOT_INSTALLED="Fake-lock is not installed yet. Please reinstall the module and choose a fresh install."
  TEXT_GBL_VULN="GBL vuln detected, skip BL flash"
  TEXT_GBL_VULN_SKIP="Skipped BL flash"
  TEXT_GBL_DETECT_FAILED="Vuln check failed"
  TEXT_NO_GBL_VULN="No GBL vuln found"
  TEXT_EFISP_WARN="efisp failed, continue BL"
  TEXT_SET_RW_FAILED="setrw failed"
  TEXT_FLASH_PART="Flashing"
  TEXT_FLASH_OK="done"
  TEXT_ALL_OK="Flash task completed (with efisp)"
  TEXT_ALL_OK_NO_EFISP="Partition patch task completed (ABL not flashed)"
  TEXT_BUSY="Task running"
  TEXT_LOG_CLEARED="Log cleared"
  TEXT_PATCH_START="Partition patch task running"
  TEXT_PATCH_VENDORBOOT_START="Patch vendor_boot"
  TEXT_PATCH_VENDORBOOT_DONE="vendor_boot patched"
  TEXT_PATCH_SUPER_START="Patch super partition"
  TEXT_PATCH_SUPER_DONE="super patched"
  TEXT_PATCH_DEBUG_SAVE="Debug mode: skip actual flash"
  TEXT_PATCH_NO_SELECTED="No partition selected for patching"
  TEXT_PATCH_BOTH_ERR="Cannot patch vendor_boot and super at the same time"
  TEXT_PATCH_ERR="Partition patch error"
  TEXT_PATCH_DONE="Partition patch task completed"
  TEXT_BIN_NOT_FOUND="Binary not found"
  TEXT_PATCH_ARGS="Patch args"
  TEXT_BIN_RUN_INFO="Run binary"
  TEXT_PATCH_SLOT="Target slot"
fi

RUNTIME_DIR="$MODDIR/tmp"
BY_NAME_DIR="/dev/block/by-name"
PERSIST_MNT="/mnt/vendor/persist"
EFISP_DIR="$PERSIST_MNT/efisp"
BDS_EFI="$MODDIR/BDS.efi"
IMAGE_NAMES="abl"
LOG_FILE="$RUNTIME_DIR/flash.log"
COMMAND_LOG="$RUNTIME_DIR/command.log"
STATE_FILE="$RUNTIME_DIR/state"
MESSAGE_FILE="$RUNTIME_DIR/message"
UPDATED_FILE="$RUNTIME_DIR/updated"
TASK_FILE="$RUNTIME_DIR/task_id"
PID_FILE="$RUNTIME_DIR/flash.pid"
LOCK_DIR="$RUNTIME_DIR/flash.lock"
export PATH=/data/adb/ksu/bin:/system/bin:/system/xbin:$PATH
RUNTIME_READY=0

timestamp() { date '+%Y-%m-%d %H:%M:%S'; }
read_line() { [ -f "$1" ] && IFS= read -r _line < "$1" && printf "%s\n" "$_line"; }
emit() { printf "%s\n" "$1"; }

ensure_runtime() {
  [ "$RUNTIME_READY" = "1" ] && return
  mkdir -p "$RUNTIME_DIR"
  [ -f "$LOG_FILE" ] || : > "$LOG_FILE"
  [ -f "$STATE_FILE" ] || echo idle > "$STATE_FILE"
  [ -f "$MESSAGE_FILE" ] || echo "$TEXT_IDLE" > "$MESSAGE_FILE"
  [ -f "$UPDATED_FILE" ] || timestamp > "$UPDATED_FILE"
  [ -f "$TASK_FILE" ] || echo 0 > "$TASK_FILE"
  RUNTIME_READY=1
}

clean_workdir() {
  for _f in "$RUNTIME_DIR"/*; do
    [ -e "$_f" ] || continue
    case "${_f##*/}" in
      flash.pid|state|message|updated|task_id|flash.log|flash.lock) ;;
      *) rm -rf "$_f" ;;
    esac
  done
}

write_state() {
  ensure_runtime
  echo "$1" > "$STATE_FILE"
  echo "$2" > "$MESSAGE_FILE"
  timestamp > "$UPDATED_FILE"
}

write_log() {
  ensure_runtime
  echo "[$(timestamp)] $*" >> "$LOG_FILE"
}

log_failure_reason() {
  _failure_label="$1"
  _failure_file="$2"
  _failure_reason=""
  if [ -s "$_failure_file" ]; then
    _failure_reason=$(sed '/^[[:space:]]*$/d' "$_failure_file" | tail -n 1 | tr '\r\n' ' ' | cut -c 1-240)
  fi
  if [ -n "$_failure_reason" ]; then
    write_log "$_failure_label: $_failure_reason"
  else
    write_log "$_failure_label"
  fi
}

run_quiet() (
  _quiet_failure="$1"
  shift
  : > "$COMMAND_LOG"
  "$@" > "$COMMAND_LOG" 2>&1
  _quiet_result=$?
  if [ "$_quiet_result" -ne 0 ]; then
    log_failure_reason "$_quiet_failure" "$COMMAND_LOG"
  fi
  rm -f "$COMMAND_LOG"
  return "$_quiet_result"
)

detect_current_slot() {
  case "$(getprop ro.boot.slot_suffix 2>/dev/null)" in
    _a) echo _a ;;
    _b) echo _b ;;
    *) return 1 ;;
  esac
}

other_slot() {
  case "$1" in
    _a) echo _b ;;
    _b) echo _a ;;
    *) return 1 ;;
  esac
}

slot_suffix_to_letter() {
  echo "${1#_}"
}

partition_path() { echo "$BY_NAME_DIR/$1$2"; }

set_partition_writable() {
  _partition="$1"
  if [ ! -b "$_partition" ]; then
    write_log "block device not found: $_partition"
    return 1
  fi

  _attempt=1
  while [ "$_attempt" -le 3 ]; do
    : > "$COMMAND_LOG"
    if blockdev --setrw "$_partition" > "$COMMAND_LOG" 2>&1; then
      rm -f "$COMMAND_LOG"
      return 0
    fi
    sync
    sleep 1
    _attempt=$((_attempt + 1))
  done
  log_failure_reason "$TEXT_SET_RW_FAILED: $_partition" "$COMMAND_LOG"
  rm -f "$COMMAND_LOG"
  return 1
}

flash_bds_image() {
  _efisp_partition="$BY_NAME_DIR/efisp"
  if ! set_partition_writable "$_efisp_partition"; then
    return 1
  fi
  if ! run_quiet "$TEXT_EFISP_FLASH_FAILED" dd if="$BDS_EFI" of="$_efisp_partition" bs=4M conv=fsync; then
    return 1
  fi
  sync
  write_log "$TEXT_EFISP_FLASH_OK"
}

current_pid() {
  [ -f "$PID_FILE" ] || return 1
  pid=$(tr -d '[:space:]' < "$PID_FILE")
  kill -0 "$pid" 2>/dev/null && echo "$pid" && return 0
  rm -f "$PID_FILE"
  return 1
}

persist_mounted() { grep -q " $PERSIST_MNT " /proc/mounts; }

place_efisp_tree_to() {
  run_quiet "$TEXT_EFISP_WRITE_FAILED" cp -r "$MODDIR/efisp/." "$1/"
}

build_patched_efi() {
  abl="$1"
  rm -f "$RUNTIME_DIR/LinuxLoader.efi" "$RUNTIME_DIR/patched.efi" "$RUNTIME_DIR/patch.log"
  if ! "$MODDIR/bin/extractfv" -o "$RUNTIME_DIR" -v "$abl" >> "$LOG_FILE" 2>&1; then
    write_log "$TEXT_EXTRACT_FAILED"
    return 1
  fi
  if ! "$MODDIR/bin/patch_abl" "$RUNTIME_DIR/LinuxLoader.efi" "$RUNTIME_DIR/patched.efi" > "$RUNTIME_DIR/patch.log" 2>&1; then
    cat "$RUNTIME_DIR/patch.log" >> "$LOG_FILE"
    write_log "$TEXT_PATCH_FAILED"
    return 1
  fi
  cat "$RUNTIME_DIR/patch.log" >> "$LOG_FILE"
  [ -s "$RUNTIME_DIR/patched.efi" ] || { write_log "$TEXT_PATCH_FAILED"; return 1; }
}

update_efisp() {
  abl=$1
  is_debug=$2
  clean_workdir
  build_patched_efi "$abl" || return 1

  if grep -q "Warning: Failed to patch ABL GBL" "$RUNTIME_DIR/patch.log"; then
    gbl_vuln=0
  else
    gbl_vuln=1
  fi

  if [ "$is_debug" = "yes" ]; then
    write_log "$TEXT_DEBUG_MODE"
    efisp_target=$RUNTIME_DIR/efisp
  else
    efisp_target=$EFISP_DIR
    if ! persist_mounted; then
      write_log "$TEXT_PERSIST_NOT_MOUNTED"
      return 1
    fi
  fi

  run_quiet "$TEXT_EFISP_MKDIR_FAILED" mkdir -p "$efisp_target" || return 1

  if [ "$is_debug" != "yes" ] && [ -f "$efisp_target/boot.efi" ]; then
    run_quiet "$TEXT_EFISP_WRITE_FAILED" mv "$efisp_target/boot.efi" "$efisp_target/boot_backup.efi" || return 1
    write_log "$TEXT_BACKUP_BOOT"
  fi

  if ! run_quiet "$TEXT_EFISP_WRITE_FAILED" cp "$RUNTIME_DIR/patched.efi" "$efisp_target/boot.efi"; then
    return 1
  fi
  place_efisp_tree_to "$efisp_target" || return 1
  sync
  write_log "$TEXT_EFISP_FILES_OK"

  if [ "$is_debug" = "yes" ]; then
    return 0
  fi

  flash_bds_image || return 1

  if [ "$gbl_vuln" = "1" ]; then
    write_log "$TEXT_GBL_VULN"
    return 2
  fi
  return 0
}

detect_gbl_vulnerability() {
  clean_workdir
  build_patched_efi "$1" || return 1
  if ! grep -q "Warning: Failed to patch ABL GBL" $RUNTIME_DIR/patch.log; then
    write_log "$TEXT_GBL_VULN"
    return 0
  fi
  write_log "$TEXT_NO_GBL_VULN"
  return 2
}

efisp_has_mz() {
  [ -b "$BY_NAME_DIR/efisp" ] || return 1
  [ "$(dd if="$BY_NAME_DIR/efisp" bs=2 count=1 2>/dev/null)" = "MZ" ]
}

gbl_exploit_present() {
  current_slot=$(detect_current_slot) || return 1
  abl=$(partition_path abl "$current_slot")
  detect_gbl_vulnerability "$abl"
  [ $? -eq 0 ]
}

update_bds_tools() {
  if ! persist_mounted; then
    write_log "$TEXT_PERSIST_NOT_MOUNTED"
    return 1
  fi

  if [ ! -f "$EFISP_DIR/boot.efi" ]; then
    if efisp_has_mz && gbl_exploit_present; then
      write_state error "$TEXT_BDS_OLD_VER"
    else
      write_state error "$TEXT_BDS_NOT_INSTALLED"
    fi
    return 2
  fi

  run_quiet "$TEXT_EFISP_MKDIR_FAILED" mkdir -p "$EFISP_DIR" || return 1

  flash_bds_image || return 1

  place_efisp_tree_to "$EFISP_DIR" || return 1
  sync
  write_log "$TEXT_EFISP_FILES_OK"
  return 0
}

cleanup_lock() { rm -rf "$LOCK_DIR" "$PID_FILE" "$COMMAND_LOG" "$RUNTIME_DIR/patch.log"; }

print_status() {
  ensure_runtime
  current_slot=$(detect_current_slot)
  target_slot=$(other_slot "$current_slot")
  _state=$(read_line "$STATE_FILE")
  running=0
  pid=""
  case "$_state" in
    success|warning|error) ;;
    *) pid=$(current_pid); [ -n "$pid" ] && running=1 ;;
  esac
  _msg=$(read_line "$MESSAGE_FILE")
  _upd=$(read_line "$UPDATED_FILE")
  _task=$(read_line "$TASK_FILE")

  out="CURRENT_SLOT=$current_slot|TARGET_SLOT=$target_slot|RUNNING=$running|PID=$pid|STATE=$_state|MESSAGE=$_msg|UPDATED_AT=$_upd|TASK_ID=$_task|USER_LANG=$LANG"
  emit "$out"
}

exec_patch_by_args() {
  arg_str="$1"
  slot_override="$2"

  arg_super=0
  arg_vendor_boot=0
  arg_debug=0
  case ",$arg_str," in *,super=1,*) arg_super=1 ;; esac
  case ",$arg_str," in *,vendor_boot=1,*) arg_vendor_boot=1 ;; esac
  case ",$arg_str," in *,debug=1,*) arg_debug=1 ;; esac

  if [ "$arg_super" = "1" ] && [ "$arg_vendor_boot" = "1" ]; then
    write_log "$TEXT_PATCH_BOTH_ERR"
    return 1
  fi

  if [ "$arg_super" != "1" ] && [ "$arg_vendor_boot" != "1" ]; then
    write_log "$TEXT_PATCH_NO_SELECTED"
    return 2
  fi

  if [ -n "$slot_override" ]; then
    target_slot_suffix="$slot_override"
  else
    target_slot_suffix=$(detect_current_slot)
    [ -z "$target_slot_suffix" ] && { write_log "$TEXT_NO_SLOT"; return 1; }
  fi
  slot_letter=$(slot_suffix_to_letter "$target_slot_suffix")

  if [ "$arg_debug" != "1" ]; then
    patch_partition=$(partition_path vendor_boot "$target_slot_suffix")
    set_partition_writable "$patch_partition" || return 1
  fi

  _old_pwd="$PWD"
  cd "$BINDIR" || { write_log "$TEXT_BIN_NOT_FOUND: $BINDIR"; return 1; }

  if [ "$arg_vendor_boot" = "1" ]; then
    write_log "$TEXT_PATCH_VENDORBOOT_START ($TEXT_PATCH_SLOT: $target_slot_suffix)"
    if [ "$arg_debug" = "1" ]; then
      write_log "$TEXT_PATCH_DEBUG_SAVE"
    else
      if [ -x "$BINDIR/patch_tools" ]; then
        if ! run_quiet "$TEXT_PATCH_ERR" "$BINDIR/patch_tools" patch_vendor "$slot_letter"; then
          cd "$_old_pwd"
          return 1
        fi
      else
        write_log "$TEXT_BIN_NOT_FOUND: patch_tools"
        cd "$_old_pwd"
        return 1
      fi
    fi
    write_log "$TEXT_PATCH_VENDORBOOT_DONE ($TEXT_PATCH_SLOT: $target_slot_suffix)"
  fi

  if [ "$arg_super" = "1" ]; then
    write_log "$TEXT_PATCH_SUPER_START ($TEXT_PATCH_SLOT: $target_slot_suffix)"
    if [ "$arg_debug" = "1" ]; then
      write_log "$TEXT_PATCH_DEBUG_SAVE"
    else
      if [ -x "$BINDIR/patch_tools" ]; then
        if ! run_quiet "$TEXT_PATCH_ERR" "$BINDIR/patch_tools" patch_vendor "$slot_letter" super; then
          cd "$_old_pwd"
          return 1
        fi
      else
        write_log "$TEXT_BIN_NOT_FOUND: patch_tools"
        cd "$_old_pwd"
        return 1
      fi
    fi
    write_log "$TEXT_PATCH_SUPER_DONE ($TEXT_PATCH_SLOT: $target_slot_suffix)"
  fi

  cd "$_old_pwd"
  return 0
}

run_flash() {
  mode_full="$1"
  debug=no

  case "$mode_full" in
    *,*)
      base_mode="${mode_full%%,*}"
      patch_args="${mode_full#*,}"
      ;;
    *)
      base_mode="$mode_full"
      patch_args=""
      ;;
  esac

  if [ "$base_mode" = "debug" ]; then
    debug=yes
    base_mode=update-efisp
  fi

  ensure_runtime
  mkdir "$LOCK_DIR" 2>/dev/null || { write_log "$TEXT_BUSY"; exit 1; }
  echo $$ > "$PID_FILE"
  trap cleanup_lock EXIT INT TERM HUP
  : > "$LOG_FILE"

  if [ "$base_mode" = "update-bds-tools" ]; then
    write_state running "$TEXT_UPDATING_BDS_TOOLS"
    update_bds_tools
    res=$?
    if [ $res -eq 0 ]; then
      write_state success "$TEXT_BDS_TOOLS_OK"
    elif [ $res -eq 2 ]; then
      :
    else
      write_state error "$TEXT_BDS_TOOLS_FAIL"
    fi
    exit 0
  fi

  current_slot=$(detect_current_slot)
  target_slot=$(other_slot "$current_slot")
  [ -z "$current_slot" ] && { write_state error "$TEXT_NO_SLOT"; exit 1; }
  [ -z "$target_slot" ] && { write_state error "$TEXT_NO_TARGET_SLOT"; exit 1; }

  if [ "$base_mode" = "skip-efisp" ]; then
    write_state running "$TEXT_PATCH_ONLY ($TEXT_TARGET_SLOT_LABEL: $target_slot)"

    if [ -z "$patch_args" ]; then
      write_log "$TEXT_PATCH_NO_SELECTED"
      write_state error "$TEXT_PATCH_NO_SELECTED"
      exit 0
    fi

    exec_patch_by_args "$patch_args" "$target_slot"
    res=$?
    if [ $res -eq 0 ]; then
      write_log "$TEXT_PATCH_DONE ($TEXT_TARGET_SLOT_LABEL: $target_slot)"
      write_state success "$TEXT_ALL_OK_NO_EFISP ($TEXT_TARGET_SLOT_LABEL: $target_slot)"
    elif [ $res -eq 2 ]; then
      write_state error "$TEXT_PATCH_NO_SELECTED"
    else
      write_state error "$TEXT_PATCH_ERR"
    fi
    exit 0
  fi

  write_state running "$TEXT_FLASHING $target_slot"
  abl=$(partition_path abl "$target_slot")

  if [ "$debug" = "yes" ]; then
    update_efisp "$abl" yes
    efisp_res=$?
    
    patch_res=0
    if [ -n "$patch_args" ]; then
      exec_patch_by_args "$patch_args" "$target_slot"
      patch_res=$?
    fi

    if [ $efisp_res -eq 0 ] && [ $patch_res -ne 1 ]; then
      write_state success "$TEXT_DEBUG_DONE $RUNTIME_DIR"
    else
      write_state error "$TEXT_DEBUG_FAILED"
    fi
    exit 0
  fi

  efisp_fail=0
  skip_abl_flash=0
  update_efisp "$abl" no
  res=$?
  if [ $res -eq 1 ]; then
    efisp_fail=1
    write_state running "$TEXT_EFISP_WARN"
  elif [ $res -eq 2 ]; then
    skip_abl_flash=1
    write_log "$TEXT_GBL_VULN_SKIP"
  fi

  if [ "$skip_abl_flash" != "1" ]; then
    for part in $IMAGE_NAMES; do
      dst=$(partition_path "$part" "$target_slot")
      src=$(partition_path "$part" "$current_slot")
      if ! blockdev --setrw "$dst" >> "$LOG_FILE" 2>&1; then
        write_log "$TEXT_SET_RW_FAILED: $dst"
        write_state error "$TEXT_SET_RW_FAILED"
        exit 1
      fi
      if ! dd if="$src" of="$dst" bs=4M conv=fsync >> "$LOG_FILE" 2>&1; then
        write_log "$TEXT_FLASH_PART $part failed"
        write_state error "$TEXT_FLASH_PART failed"
        exit 1
      fi
      sync
      write_log "$TEXT_FLASH_PART $part -> $dst $TEXT_FLASH_OK"
    done
  fi

  patch_fail=0
  if [ -n "$patch_args" ]; then
    exec_patch_by_args "$patch_args" "$target_slot"
    if [ $? -ne 0 ]; then
      patch_fail=1
    fi
  fi

  if [ $efisp_fail -eq 1 ] || [ $patch_fail -eq 1 ]; then
    write_state warning "BL done, partial failed ($TEXT_TARGET_SLOT_LABEL: $target_slot)"
  elif [ "$skip_abl_flash" = "1" ] && [ -n "$patch_args" ]; then
    write_log "$TEXT_PATCH_DONE ($TEXT_TARGET_SLOT_LABEL: $target_slot)"
    write_state success "$TEXT_PATCH_DONE ($TEXT_TARGET_SLOT_LABEL: $target_slot)"
  elif [ "$skip_abl_flash" = "1" ]; then
    write_log "$TEXT_GBL_VULN_SKIP ($TEXT_TARGET_SLOT_LABEL: $target_slot)"
    write_state success "$TEXT_GBL_VULN_SKIP ($TEXT_TARGET_SLOT_LABEL: $target_slot)"
  else
    write_log "$TEXT_ALL_OK ($TEXT_TARGET_SLOT_LABEL: $target_slot)"
    write_state success "$TEXT_ALL_OK ($TEXT_TARGET_SLOT_LABEL: $target_slot)"
  fi
}

run_patch() {
  arg_str="$1"

  ensure_runtime
  mkdir "$LOCK_DIR" 2>/dev/null || { write_log "$TEXT_BUSY"; exit 1; }
  echo $$ > "$PID_FILE"
  trap cleanup_lock EXIT INT TERM HUP
  : > "$LOG_FILE"

  current_slot=$(detect_current_slot)
  [ -z "$current_slot" ] && { write_log "$TEXT_NO_SLOT"; write_state error "$TEXT_NO_SLOT"; exit 1; }
  write_state running "$TEXT_PATCH_START ($TEXT_CURRENT_SLOT_LABEL: $current_slot)"

  exec_patch_by_args "$arg_str"
  res=$?

  if [ $res -eq 1 ]; then
    write_state error "$TEXT_PATCH_ERR"
    exit 1
  elif [ $res -eq 2 ]; then
    write_state error "$TEXT_PATCH_NO_SELECTED"
    exit 0
  fi

  write_log "$TEXT_PATCH_DONE ($TEXT_CURRENT_SLOT_LABEL: $current_slot)"
  write_state success "$TEXT_PATCH_DONE ($TEXT_CURRENT_SLOT_LABEL: $current_slot)"
  exit 0
}

start_patch() {
  ensure_runtime
  [ -n "$(current_pid)" ] && { emit "ALREADY_RUNNING=1"; return; }
  task_id="$(date +%s)-$$"
  echo "$task_id" > "$TASK_FILE"
  write_state running "$TEXT_PATCH_START"
  setsid sh "$0" patch "$1" >/dev/null 2>&1 </dev/null &
  sleep 1
  if [ -n "$(current_pid)" ]; then
    emit "STARTED=1|TASK_ID=$task_id"
  else
    st=$(read_line "$STATE_FILE")
    [ -n "$st" ] && emit "FINISHED=$st|TASK_ID=$task_id" || emit "STARTED=0"
  fi
}

start_flash() {
  ensure_runtime
  [ -n "$(current_pid)" ] && { emit "ALREADY_RUNNING=1"; return; }
  task_id="$(date +%s)-$$"
  echo "$task_id" > "$TASK_FILE"
  case "$1" in
    update-bds-tools*) _start_msg="$TEXT_UPDATING_BDS_TOOLS" ;;
    skip-efisp*) _start_msg="$TEXT_PATCH_ONLY" ;;
    debug*) _start_msg="$TEXT_DEBUG_MODE" ;;
    *) _start_msg="$TEXT_FLASHING" ;;
  esac
  write_state running "$_start_msg"
  setsid sh "$0" flash "$1" >/dev/null 2>&1 </dev/null &
  sleep 1
  if [ -n "$(current_pid)" ]; then
    emit "STARTED=1|TASK_ID=$task_id"
  else
    st=$(read_line "$STATE_FILE")
    [ -n "$st" ] && emit "FINISHED=$st|TASK_ID=$task_id" || emit "STARTED=0"
  fi
}

print_log() { cat "$LOG_FILE"; }
tail_log() { tail -n200 "$LOG_FILE" | awk '{printf "%s@NL@", $0}'; }

clear_log() {
  ensure_runtime
  [ -n "$(current_pid)" ] && { emit "BUSY=1"; return; }
  : > "$LOG_FILE"
  write_state idle "$TEXT_LOG_CLEARED"
  emit "CLEARED=1"
}

set_language() {
  case "$1" in
    zh)
      _language_name="假回锁"
      _language_description="自动刷新bl相关分区到非活动槽位"
      ;;
    en)
      _language_name="Fake BL EFISP"
      _language_description="Automatically flash BL-related partitions to inactive slot"
      ;;
    *) return 1 ;;
  esac
  ksud module config set user_lang "$1" >/dev/null 2>&1 || return 1
  sed -i "s|^name=.*|name=$_language_name|" "$MODDIR/module.prop" || return 1
  sed -i "s|^description=.*|description=$_language_description|" "$MODDIR/module.prop" || return 1
  emit "LANG=$1"
}

case "$1" in
  status) print_status ;;
  flash) run_flash "$2" ;;
  start) start_flash "$2" ;;
  patch) run_patch "$2" ;;
  start-patch) start_patch "$2" ;;
  log) print_log ;;
  tail) tail_log ;;
  clear-log) clear_log ;;
  set-language) set_language "$2" ;;
  *) exit 1 ;;
esac

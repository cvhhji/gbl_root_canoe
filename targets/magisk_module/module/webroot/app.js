import { exec as ksuExec, moduleInfo as ksuModuleInfo, toast } from "./kernelsu.js";

const IMAGE_NAMES = ["abl"];

const state = {
  confirmStep: 0,
  pendingAction: null,
  moduleDir: "",
  scriptPath: "",
  status: null,
  pollTimer: null,
  pollInFlight: false,
  refreshRequested: false,
  prevStatusRaw: null,
  prevLogRaw: null,
  taskStarted: false,
  activeTaskId: "",
  activeTaskScope: "",
  activeTaskSlot: "",
  completionNotifiedTaskId: "",
  lang: "zh"
};

const i18n = {
  zh: {
    pageTitle: "假回锁 - BL Flasher",
    ksuWebUI: "KernelSU Module WebUI",
    heroDesc: "自动识别当前活动槽位，若新版本存在GBL漏洞则跳过BL刷写；将BL镜像刷写到另一槽位，并将破解ABL放入persist的efisp目录、BDS刷入efisp分区",
    slotStatus: "槽位状态",
    refresh: "刷新",
    languageSwitch: "English",
    languageChanged: "已切换为中文",
    languageChangeFailed: "语言切换失败",
    currentSlot: "当前槽位",
    targetSlot: "目标槽位",
    imageCount: "镜像数量",
    taskStatus: "任务状态",
    flash: "刷写到另一槽位",
    bdsTools: "仅更新BDS和Tools",
    patchPart: "修补当前槽位分区",
    confirmBdsTools: "确认更新 BDS/Tools",
    confirmPatchPart: "确认修补分区",
    modalBdsStep1: "将把 BDS.efi 刷入 efisp 分区，并用模块自带的 efisp 文件夹（BOOTENTRIES 与 tools）替换 persist 上的启动根目录。不会改动 ABL 与 boot.efi。",
    modalBdsStep2: "第二次确认: 这是高风险写入操作，错误的 BDS 或 efisp 写入可能导致无法进入启动菜单。确认后将立即开始。",
    modalPatchStep1: (slot) => `将对当前活动槽位 ${slot} 执行勾选的修补操作。调试模式下不会执行实际修补。`,
    modalPatchStep2: "第二次确认：修补分区属于高风险操作，错误会导致系统无法启动，确认后立即执行。",
    toastStartBdsTools: "BDS/Tools 更新任务已启动",
    toastBdsToolsDone: "BDS/Tools 更新任务运行成功",
    clearLog: "清空日志",
    updateEfisp: "更新 efisp（默认开启）",
    debugMode: "调试模式（仅处理不刷写，efisp 目录使用模块 tmp/efisp）",
    lblPatchVendorBoot: "修补 vendor_boot 分区",
    lblPatchSuper: "修补 super 分区",
    patchMutualTip: "vendor_boot 和 super 仅可选择一项",
    warning: "刷写对象是 bootloader 相关分区，风险较高。开始前请确认镜像与机型严格匹配。",
    imageMap: "镜像映射",
    partition: "分区名",
    source: "源分区 (当前槽位)",
    target: "目标分区",
    action: "操作",
    waiting: "等待读取模块状态",
    log: "实时日志",
    autoPoll: "自动轮询最近 200 行",
    risk: "高风险操作",
    confirmFlash: "确认操作",
    cancel: "取消",
    continue: "继续",
    waitingStatus: "等待检测",
    slotUnknown: "槽位未知",
    logWaiting: "等待日志输出...",
    toastRunning: "已有任务在运行",
    toastStartDebug: "调试任务已启动",
    toastStartFlash: "刷写任务已启动",
    toastStartPatch: "分区修补任务已启动",
    toastDebugDone: "调试任务运行成功",
    toastFlashDone: "刷写任务运行成功",
    toastPatchDone: "分区修补任务运行成功",
    toastBlDone: "BL 刷写完成，但 efisp 未更新",
    toastFailed: "任务执行失败",
    toastStartError: "任务启动失败",
    statusIdle: "状态: idle",
    statusRunning: "状态: running",
    statusSuccess: "状态: success",
    statusWarning: "状态: warning",
    statusError: "状态: error",
    toastLogBusy: "任务运行中，暂时不能清空日志",
    toastLogCleared: "日志已清空",
    modalStep1Debug: "调试模式：将执行所有处理流程但不刷写分区，生成的文件保存在 tmp 目录。",
    modalStep1Normal: (slot) => `第一次确认: 将把当前槽位的 BL 分区拷贝到槽位 ${slot}`,
    modalStep1PatchOnly: (slot, name) => `第一次确认: 仅对目标槽位 ${slot} 执行 ${name} 修补，不刷写 ABL、不更新 efisp。`,
    modalStep2: "第二次确认: 这是高风险写入操作，错误操作可能导致目标槽位无法启动。确认后将立即开始。",
    withEfisp: "，并更新 efisp。",
    noEfisp: "，不更新 efisp。",
    withPatch: (name) => `，同步对目标槽位执行 ${name} 修补`,
    confirmSlot: "请确认槽位无误。",
    taskRunning:"任务运行中",
    waitOperate:"等待操作",
    copyPart:"分区拷贝",
    statusReadFail:"状态读取失败",
    startFail:"启动失败"
  },
  en: {
    pageTitle: "Fake Lock - BL Flasher",
    ksuWebUI: "KernelSU Module WebUI",
    heroDesc: "Auto-detect active slot. Skip BL flash if new build has GBL exploit. Flash BL images to inactive slot, place the cracked ABL in persist's efisp dir and flash the BDS to the efisp partition.",
    slotStatus: "Slot Status",
    refresh: "Refresh",
    languageSwitch: "中文",
    languageChanged: "Switched to English",
    languageChangeFailed: "Language switch failed",
    currentSlot: "Current Slot",
    targetSlot: "Target Slot",
    imageCount: "Image Count",
    taskStatus: "Task Status",
    flash: "Flash To Other Slot",
    bdsTools: "Update BDS & Tools Only",
    patchPart: "Patch Current Slot",
    confirmBdsTools: "Confirm BDS/Tools Update",
    confirmPatchPart: "Confirm Partition Patch",
    modalBdsStep1: "Will flash BDS.efi to the efisp partition and replace the persist boot root with the bundled efisp folder (BOOTENTRIES and tools). The ABL and boot.efi are not touched.",
    modalBdsStep2: "2nd Confirm: This is a high-risk write. A wrong BDS or efisp write may prevent the boot menu from loading. It starts immediately after confirm.",
    modalPatchStep1: (slot) => `Will patch the current active slot ${slot} with the selected option. Debug mode does not write partitions.`,
    modalPatchStep2: "2nd Confirm: Partition patching is high-risk, bad patch may cause boot failure. Start immediately after confirm.",
    toastStartBdsTools: "BDS/Tools update started",
    toastBdsToolsDone: "BDS/Tools update task succeeded",
    clearLog: "Clear Log",
    updateEfisp: "Update efisp (on by default)",
    debugMode: "Debug Mode (process only, no flash; efisp dir uses module tmp/efisp)",
    lblPatchVendorBoot: "Patch vendor_boot partition",
    lblPatchSuper: "Patch super partition",
    patchMutualTip: "Only one of vendor_boot / super can be selected",
    warning: "Flashing bootloader partitions is high risk. Verify images match your device before starting.",
    imageMap: "Image Mapping",
    partition: "Partition",
    source: "Source (Current)",
    target: "Target",
    action: "Action",
    waiting: "Waiting for module status",
    log: "Live Log",
    autoPoll: "Auto poll last 200 lines",
    risk: "HIGH RISK",
    confirmFlash: "Confirm Action",
    cancel: "Cancel",
    continue: "Continue",
    waitingStatus: "Waiting",
    slotUnknown: "Slot Unknown",
    logWaiting: "Waiting for log...",
    toastRunning: "Task is already running",
    toastStartDebug: "Debug task started",
    toastStartFlash: "Flash task started",
    toastStartPatch: "Partition patch task started",
    toastDebugDone: "Debug task succeeded",
    toastFlashDone: "Flash task succeeded",
    toastPatchDone: "Partition patch task succeeded",
    toastBlDone: "BL flashed, but efisp not updated",
    toastFailed: "Task finished (failed)",
    statusIdle: "Status: idle",
    statusRunning: "Status: running",
    statusSuccess: "Status: success",
    statusWarning: "Status: warning",
    statusError: "Status: error",
    toastStartError: "Failed to start task",
    toastLogBusy: "Cannot clear log while task is running",
    toastLogCleared: "Log cleared",
    modalStep1Debug: "Debug Mode: All processes run without flashing partitions. Files saved to tmp directory.",
    modalStep1Normal: (slot) => `1st Confirm: Copy BL partition from current slot to ${slot}`,
    modalStep1PatchOnly: (slot, name) => `1st Confirm: Patch ${name} on target slot ${slot} only. No ABL flash, no efisp update.`,
    modalStep2: "2nd Confirm: This is a high-risk write operation. Wrong action may brick the target slot. Flash will start immediately after confirm.",
    withEfisp: ", and update efisp.",
    noEfisp: ", efisp not updated.",
    withPatch: (name) => `, apply ${name} patch to target slot`,
    confirmSlot: "Please confirm slot is correct.",
    taskRunning:"Task Running",
    waitOperate:"Waiting",
    copyPart:"Copy",
    statusReadFail:"Status Read Failed",
    startFail:"Start Failed"
  }
};

const elements = {
  stateChip: document.getElementById("stateChip"),
  slotChip: document.getElementById("slotChip"),
  currentSlot: document.getElementById("currentSlot"),
  targetSlot: document.getElementById("targetSlot"),
  imageCount: document.getElementById("imageCount"),
  taskMessage: document.getElementById("taskMessage"),
  updatedAt: document.getElementById("updatedAt"),
  imageTableBody: document.getElementById("imageTableBody"),
  logOutput: document.getElementById("logOutput"),
  flashButton: document.getElementById("flashButton"),
  bdsToolsButton: document.getElementById("bdsToolsButton"),
  patchPartButton: document.getElementById("patchPartButton"),
  clearLogButton: document.getElementById("clearLogButton"),
  refreshButton: document.getElementById("refreshButton"),
  languageButton: document.getElementById("languageButton"),
  confirmModal: document.getElementById("confirmModal"),
  confirmText: document.getElementById("confirmText"),
  nextConfirmButton: document.getElementById("nextConfirmButton"),
  cancelConfirmButton: document.getElementById("cancelConfirmButton"),
  updateEfispCheckbox: document.getElementById("updateEfispCheckbox"),
  debugModeCheckbox: document.getElementById("debugModeCheckbox"),
  patchVendorBootCheckbox: document.getElementById("patchVendorBootCheckbox"),
  patchSuperCheckbox: document.getElementById("patchSuperCheckbox"),
  patchMutualTip: document.getElementById("patchMutualTip"),
  pageTitle: document.getElementById("pageTitle")
};

function setText(selector, value) {
  const node = document.querySelector(selector);
  if (node) node.textContent = value;
}

function applyLanguage(lang) {
  if (!i18n[lang]) return;
  state.lang = lang;
  const t = i18n[lang];
  document.documentElement.lang = lang === "zh" ? "zh-CN" : "en";
  elements.pageTitle.textContent = t.pageTitle;
  elements.languageButton.textContent = t.languageSwitch;
  elements.languageButton.setAttribute("aria-label", t.languageSwitch);
  setText("#lblKsuWebUI", t.ksuWebUI);
  setText(".hero-copy", t.heroDesc);
  setText("#lblSlotStatus", t.slotStatus);
  setText("#lblCurrentSlot", t.currentSlot);
  setText("#lblTargetSlot", t.targetSlot);
  setText("#lblImageCount", t.imageCount);
  setText("#lblTaskStatus", t.taskStatus);
  setText("#lblUpdateEfisp", t.updateEfisp);
  setText("#lblDebugMode", t.debugMode);
  setText("#lblPatchVendorBoot", t.lblPatchVendorBoot);
  setText("#lblPatchSuper", t.lblPatchSuper);
  elements.patchMutualTip.textContent = t.patchMutualTip;
  setText("#lblWarning", t.warning);
  setText("#lblImageMap", t.imageMap);
  setText("#tblPartition", t.partition);
  setText("#tblSource", t.source);
  setText("#tblTarget", t.target);
  setText("#tblAction", t.action);
  setText("#tblWaiting", t.waiting);
  setText("#lblLog", t.log);
  setText("#lblAutoPoll", t.autoPoll);
  setText("#modalRisk", t.risk);
  setText("#modalTitle", t.confirmFlash);
  if (elements.logOutput.textContent === "等待日志输出..." || elements.logOutput.textContent === "Waiting for log...") {
    elements.logOutput.textContent = t.logWaiting;
  }
  if (elements.stateChip.textContent === "等待检测" || elements.stateChip.textContent === "Waiting") {
    elements.stateChip.textContent = t.waitingStatus;
  }
  if (elements.slotChip.textContent === "槽位未知" || elements.slotChip.textContent === "Slot Unknown") {
    elements.slotChip.textContent = t.slotUnknown;
  }
  document.querySelectorAll("[data-i18n]").forEach(el => {
    const key = el.dataset.i18n;
    if (t[key]) el.textContent = t[key];
  });
  if (state.status) renderStatus(state.status);
}

function shellQuote(value) {
  return `'${String(value).replace(/'/g, `'\\''`)}'`;
}

function moduleInfo() {
  const raw = ksuModuleInfo();
  return typeof raw === "string" ? JSON.parse(raw) : raw;
}

async function runScript(action, arg) {
  const parts = [`MODDIR=${shellQuote(state.moduleDir)}`, "sh", shellQuote(state.scriptPath), action];
  if (arg) parts.push(shellQuote(arg));
  const command = parts.join(" ");
  const timeout = new Promise((_, reject) => {
    setTimeout(() => reject(new Error("KernelSU exec timeout")), 8000);
  });
  const { errno, stdout, stderr } = await Promise.race([ksuExec(command), timeout]);
  if (errno !== 0) throw new Error(stderr || `Command failed: ${errno}`);
  return stdout || "";
}

async function toggleLanguage() {
  const nextLanguage = state.lang === "zh" ? "en" : "zh";
  try {
    await runScript("set-language", nextLanguage);
    if (!elements.confirmModal.classList.contains("hidden")) closeConfirmModal();
    applyLanguage(nextLanguage);
    state.prevStatusRaw = null;
    await refreshStatus();
    toast(i18n[nextLanguage].languageChanged);
  } catch (e) {
    toast(`${i18n[state.lang].languageChangeFailed}: ${e.message}`);
  }
}

function parseKeyValueOutput(output) {
  const info = {};
  for (const line of output.split(/[\r\n|]+/)) {
    if (!line) continue;
    const eq = line.indexOf("=");
    if (eq > 0) info[line.slice(0, eq)] = line.slice(eq + 1);
  }
  return info;
}

function escapeHtml(str) {
  return str.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

function renderTable(currentSlot, targetSlot) {
  const t = i18n[state.lang];
  if (currentSlot === "-" || targetSlot === "-") {
    elements.imageTableBody.innerHTML = `<tr><td colspan="4" class="empty-row">${t.waiting}</td></tr>`;
    return;
  }
  elements.imageTableBody.innerHTML = IMAGE_NAMES.map(name => {
    const src = `/dev/block/by-name/${name}${currentSlot}`;
    const dst = `/dev/block/by-name/${name}${targetSlot}`;
    return `<tr><td>${escapeHtml(name)}</td><td class="caption">${escapeHtml(src)}</td><td>${escapeHtml(dst)}</td><td><span class="status-pill ok">${t.copyPart}</span></td></tr>`;
  }).join("");
}

function localizedTaskMessage(status) {
  const t = i18n[state.lang];
  const message = status.MESSAGE || "";
  const normalized = message.toLowerCase();
  const slotMatch = message.match(/(当前槽位|Current slot|目标槽位|Target slot)\s*:\s*(_[ab])/i);
  let suffix = "";
  if (slotMatch) {
    const current = /当前槽位|current slot/i.test(slotMatch[1]);
    suffix = ` (${current ? t.currentSlot : t.targetSlot}: ${slotMatch[2]})`;
  }
  if (status.STATE === "idle") {
    return /日志已清空|log cleared/i.test(message) ? t.toastLogCleared : t.waitOperate;
  }
  if (status.RUNNING === "1") {
    if (/分区修补|partition patch/i.test(message)) return t.toastStartPatch + suffix;
    if (/调试|debug/i.test(message)) return t.toastStartDebug + suffix;
    if (/bds/i.test(message)) return t.toastStartBdsTools;
    return t.toastStartFlash + suffix;
  }
  if (status.STATE === "success") {
    if (/修补|patch/i.test(message)) return t.toastPatchDone + suffix;
    if (/调试|debug/i.test(message)) return t.toastDebugDone + suffix;
    if (/bds/i.test(message)) return t.toastBdsToolsDone;
    return t.toastFlashDone + suffix;
  }
  if (status.STATE === "warning") return t.toastBlDone + suffix;
  return message || t.waitOperate;
}

function renderStatus(status) {
  state.status = status;
  const t = i18n[state.lang];
  const cur = status.CURRENT_SLOT || "-";
  const tar = status.TARGET_SLOT || "-";
  const run = status.RUNNING === "1";
  const st = status.STATE || "idle";
  const visibleState = run ? "running" : st;
  const msg = localizedTaskMessage(status);
  const statusLabels = {
    idle: t.statusIdle,
    running: t.statusRunning,
    success: t.statusSuccess,
    warning: t.statusWarning,
    error: t.statusError
  };
  elements.currentSlot.textContent = cur;
  elements.targetSlot.textContent = tar;
  elements.imageCount.textContent = IMAGE_NAMES.length;
  elements.taskMessage.textContent = msg;
  elements.updatedAt.textContent = status.UPDATED_AT || "-";
  elements.stateChip.textContent = statusLabels[visibleState] || `${state.lang === "zh" ? "状态" : "Status"}: ${visibleState}`;
  elements.stateChip.className = "chip";
  if (st === "success") elements.stateChip.classList.add("chip-success");
  else if (st === "error") elements.stateChip.classList.add("chip-danger");
  else if (st === "warning" || run) elements.stateChip.classList.add("chip-warn");
  elements.slotChip.textContent = (cur !== "-" && tar !== "-") ? `${state.lang === "zh" ? "当前" : "Current"} ${cur} → ${state.lang === "zh" ? "目标" : "Target"} ${tar}` : t.slotUnknown;
  elements.flashButton.disabled = run || cur === "-" || tar === "-";
  elements.bdsToolsButton.disabled = run;
  elements.patchPartButton.disabled = run;
  elements.clearLogButton.disabled = run;
  renderTable(cur, tar);
}
function taskSlotSuffix() {
  if (!state.activeTaskSlot || !state.activeTaskScope) return "";
  const t = i18n[state.lang];
  const label = state.activeTaskScope === "current" ? t.currentSlot : t.targetSlot;
  return ` (${label}: ${state.activeTaskSlot})`;
}

function notifyTaskFinished(stateStr) {
  const t = i18n[state.lang];
  const msg = state.status?.MESSAGE || "";
  const normalized = msg.toLowerCase();
  let notification = "";
  if (stateStr === "success") {
    if (msg.includes("修补") || normalized.includes("patch")) notification = t.toastPatchDone;
    else if (msg.includes("调试") || normalized.includes("debug")) notification = t.toastDebugDone;
    else if (normalized.includes("bds")) notification = t.toastBdsToolsDone;
    else notification = t.toastFlashDone;
  } else if (stateStr === "warning") notification = t.toastBlDone;
  else if (stateStr === "error") notification = t.toastFailed;
  if (notification) toast(notification + taskSlotSuffix());
}
function rememberPendingTask(taskId, scope = "", slot = "") {
  if (!taskId) return;
  state.taskStarted = true;
  state.activeTaskId = taskId;
  state.activeTaskScope = scope;
  state.activeTaskSlot = slot;
  try {
    localStorage.setItem("blFlasherPendingTask", JSON.stringify({ taskId, scope, slot }));
    localStorage.removeItem("blFlasherPendingTaskId");
  } catch {}
}

function clearPendingTask() {
  state.taskStarted = false;
  state.activeTaskId = "";
  state.activeTaskScope = "";
  state.activeTaskSlot = "";
  try {
    localStorage.removeItem("blFlasherPendingTask");
    localStorage.removeItem("blFlasherPendingTaskId");
  } catch {}
}

function applyStatus(s, notify = true) {
  if (!s?.STATE) return s;
  const taskId = s.TASK_ID || "";
  renderStatus(s);
  const isRunning = s.RUNNING === "1";
  const terminalState = ["success", "warning", "error"].includes(s.STATE);
  if (!isRunning && terminalState && state.activeTaskId && taskId === state.activeTaskId) {
    if (notify && taskId !== state.completionNotifiedTaskId) {
      state.completionNotifiedTaskId = taskId;
      notifyTaskFinished(s.STATE);
    }
    clearPendingTask();
  }
  return s;
}

async function refreshStatus() {
  try {
    const raw = await runScript("status");
    if (!raw) return state.status;
    if (raw === state.prevStatusRaw) return state.status;
    state.prevStatusRaw = raw;
    const s = parseKeyValueOutput(raw);
    if (s.USER_LANG === "en") applyLanguage("en");
    else if (s.USER_LANG === "zh") applyLanguage("zh");
    return applyStatus(s);
  } catch (e) {
    console.error("refreshStatus failed:", e);
    return state.status;
  }

}

async function refreshLog() {
  try {
    const raw = (await runScript("tail", "200")).trim();
    if (raw === state.prevLogRaw) return;
    state.prevLogRaw = raw;
    const log = raw.replace(/@NL@/g, String.fromCharCode(10));
    elements.logOutput.textContent = log || i18n[state.lang].logWaiting;
    elements.logOutput.scrollTop = elements.logOutput.scrollHeight;
  } catch (e) {
    elements.logOutput.textContent = `${state.lang === "zh" ? "日志读取失败" : "Log Read Failed"}: ${e.message}`;
  }
}

function closeConfirmModal() {
  state.confirmStep = 0;
  state.pendingAction = null;
  elements.confirmModal.classList.add("hidden");
  elements.confirmModal.setAttribute("aria-hidden", "true");
  elements.nextConfirmButton.textContent = i18n[state.lang].continue;
}

function getPatchArgString() {
  const parts = [];
  if (elements.patchVendorBootCheckbox.checked) parts.push("vendor_boot=1");
  if (elements.patchSuperCheckbox.checked) parts.push("super=1");
  if (elements.debugModeCheckbox.checked) parts.push("debug=1");
  return parts.join(",");
}

function getSelectedPatchName() {
  const t = i18n[state.lang];
  if (elements.patchVendorBootCheckbox.checked) return "vendor_boot";
  if (elements.patchSuperCheckbox.checked) return "super";
  return "";
}

function openConfirmModal(action) {
  const t = i18n[state.lang];
  state.pendingAction = action;
  state.confirmStep = 1;
  if (action === "bds-tools") {
    document.querySelector("#modalTitle").textContent = t.confirmBdsTools;
    elements.confirmText.textContent = t.modalBdsStep1;
    elements.nextConfirmButton.textContent = t.continue;
  } else if (action === "patch-part") {
    document.querySelector("#modalTitle").textContent = t.confirmPatchPart;
    elements.confirmText.textContent = t.modalPatchStep1(state.status?.CURRENT_SLOT || "?");
    elements.nextConfirmButton.textContent = t.continue;
  } else {
    document.querySelector("#modalTitle").textContent = t.confirmFlash;
    const tar = state.status?.TARGET_SLOT || "?";
    const efisp = elements.updateEfispCheckbox?.checked;
    const dbg = elements.debugModeCheckbox?.checked;
    const patchName = getSelectedPatchName();
    let msg = "";

    if (!efisp && patchName) {
      msg = t.modalStep1PatchOnly(tar, patchName);
    } else if (dbg) {
      msg = t.modalStep1Debug;
    } else {
      msg = t.modalStep1Normal(tar);
      msg += efisp ? t.withEfisp : t.noEfisp;
      if (patchName) msg += t.withPatch(patchName);
      msg += t.confirmSlot;
    }

    elements.confirmText.textContent = msg;
    elements.nextConfirmButton.textContent = dbg ? (state.lang === "zh" ? "开始调试" : "Start Debug") : t.continue;
  }
  elements.confirmModal.classList.remove("hidden");
  elements.confirmModal.setAttribute("aria-hidden", "false");
}

function handleConfirmProgress() {
  const t = i18n[state.lang];
  if (state.confirmStep === 1) {
    if (state.pendingAction === "bds-tools") {
      state.confirmStep = 2;
      elements.confirmText.textContent = t.modalBdsStep2;
      elements.nextConfirmButton.textContent = state.lang === "zh" ? "开始更新" : "Start Update";
      return;
    } else if(state.pendingAction === "patch-part"){
      state.confirmStep = 2;
      elements.confirmText.textContent = t.modalPatchStep2;
      elements.nextConfirmButton.textContent = state.lang === "zh" ? "确认修补" : "Confirm Patch";
      return;
    }
    const dbg = elements.debugModeCheckbox?.checked;
    if (!dbg) {
      state.confirmStep = 2;
      elements.confirmText.textContent = t.modalStep2;
      elements.nextConfirmButton.textContent = state.lang === "zh" ? "确认执行" : "Confirm";
      return;
    }
    closeConfirmModal();
    startFlash();
    return;
  }
  const action = state.pendingAction;
  closeConfirmModal();
  if (action === "bds-tools") startBdsTools();
  else if(action === "patch-part") startPatchPart();
  else startFlash();
}

function showTaskStarting(message, taskId) {
  renderStatus({
    ...(state.status || {}),
    RUNNING: "1",
    STATE: "running",
    MESSAGE: message,
    TASK_ID: taskId || state.activeTaskId,
    UPDATED_AT: new Date().toLocaleString()
  });
}

function handleStartResult(out, startedMessage, scope = "", slot = "") {
  const t = i18n[state.lang];
  if (out.ALREADY_RUNNING) {
    toast(t.toastRunning);
    return;
  }
  if (out.STARTED === "1" || out.FINISHED) {
    rememberPendingTask(out.TASK_ID || "", scope, slot);
    if (out.STARTED === "1") {
      showTaskStarting(startedMessage, out.TASK_ID || "");
      toast(startedMessage);
    }
    return;
  }
  toast(t.toastStartError);
}

async function startFlash() {
  const t = i18n[state.lang];
  const efisp = elements.updateEfispCheckbox?.checked;
  const dbg = elements.debugModeCheckbox?.checked;
  const baseMode = dbg ? "debug" : (efisp ? "update-efisp" : "skip-efisp");
  const patchArgs = getPatchArgString();
  const fullMode = patchArgs ? `${baseMode},${patchArgs}` : baseMode;
  const targetSlot = state.status?.TARGET_SLOT || "?";
  const startMessage = `${dbg ? t.toastStartDebug : t.toastStartFlash} (${t.targetSlot}: ${targetSlot})`;

  try {
    const out = parseKeyValueOutput(await runScript("start", fullMode));
    handleStartResult(out, startMessage, "target", targetSlot);
  } catch (e) { toast(`${t.startFail}: ${e.message}`); }
  await manualRefresh();
}

async function startPatchPart() {
  const t = i18n[state.lang];
  const currentSlot = state.status?.CURRENT_SLOT || "?";
  const startMessage = `${t.toastStartPatch} (${t.currentSlot}: ${currentSlot})`;
  try {
    const out = parseKeyValueOutput(await runScript("start-patch", getPatchArgString()));
    handleStartResult(out, startMessage, "current", currentSlot);
  } catch (e) { toast(`${t.startFail}: ${e.message}`); }
  await manualRefresh();
}

async function startBdsTools() {
  const t = i18n[state.lang];
  try {
    const out = parseKeyValueOutput(await runScript("start", "update-bds-tools"));
    handleStartResult(out, t.toastStartBdsTools);
  } catch (e) { toast(`${t.startFail}: ${e.message}`); }
  await manualRefresh();
}

async function clearLog() {
  const t = i18n[state.lang];
  try {
    const out = parseKeyValueOutput(await runScript("clear-log"));
    if (out.BUSY === "1") { toast(t.toastLogBusy); return; }
    elements.logOutput.textContent = t.logWaiting;
    state.prevLogRaw = null;
    state.prevStatusRaw = null;
    toast(t.toastLogCleared);
  } catch (e) { toast(`${state.lang === "zh" ? "清空失败" : "Clear Failed"}: ${e.message}`); }
  await manualRefresh();
}

function schedulePoll(delay) {
  if (state.pollTimer !== null) clearTimeout(state.pollTimer);
  state.pollTimer = null;
  if (delay !== null && !document.hidden) state.pollTimer = setTimeout(poll, delay);
}

async function poll() {
  if (state.pollInFlight) return;
  state.pollInFlight = true;
  try {
    await refreshStatus();
    await refreshLog();
  } catch (e) {
    console.error("poll failed:", e);
  } finally {
    state.pollInFlight = false;
    const running = state.taskStarted || state.status?.RUNNING === "1";
    const refreshNow = state.refreshRequested;
    state.refreshRequested = false;
    schedulePoll(refreshNow ? 0 : (running ? 1000 : 8000));
  }
}

function startPolling() {
  schedulePoll(0);
}

async function manualRefresh() {
  state.prevStatusRaw = null;
  state.prevLogRaw = null;
  schedulePoll(null);
  if (state.pollInFlight) {
    state.refreshRequested = true;
    return;
  }
  await poll();
}
function initPatchCheckboxMutual() {
  elements.patchVendorBootCheckbox.addEventListener("change", () => {
    if (elements.patchVendorBootCheckbox.checked) {
      elements.patchSuperCheckbox.checked = false;
    }
  });
  elements.patchSuperCheckbox.addEventListener("change", () => {
    if (elements.patchSuperCheckbox.checked) {
      elements.patchVendorBootCheckbox.checked = false;
    }
  });
}

async function init() {
  try {
    const info = moduleInfo();
    if(!info) return;
    state.moduleDir = info.moduleDir;
    state.scriptPath = `${state.moduleDir}/bin/bl_flasher.sh`;
    initPatchCheckboxMutual();
    try {
      const pending = JSON.parse(localStorage.getItem("blFlasherPendingTask") || "null");
      state.activeTaskId = pending?.taskId || localStorage.getItem("blFlasherPendingTaskId") || "";
      state.activeTaskScope = pending?.scope || "";
      state.activeTaskSlot = pending?.slot || "";
    } catch {}
    state.taskStarted = Boolean(state.activeTaskId);
    await refreshStatus();
    await refreshLog();
  } catch (e) {
    elements.stateChip.textContent = state.lang === "zh" ? "WebUI 初始化失败" : "WebUI Init Failed";
    elements.stateChip.className = "chip chip-danger";
    elements.taskMessage.textContent = e.message;
    elements.flashButton.disabled = true;
    elements.bdsToolsButton.disabled = true;
    elements.patchPartButton.disabled = true;
    elements.clearLogButton.disabled = true;
    return;
  }

  elements.refreshButton.addEventListener("click", manualRefresh);
  elements.languageButton.addEventListener("click", toggleLanguage);
  elements.flashButton.addEventListener("click", () => openConfirmModal("flash"));
  elements.bdsToolsButton.addEventListener("click", () => openConfirmModal("bds-tools"));
  document.addEventListener("visibilitychange", () => {
    if (document.hidden) schedulePoll(null);
    else manualRefresh();
  });

  elements.patchPartButton.addEventListener("click", () => openConfirmModal("patch-part"));
  elements.clearLogButton.addEventListener("click", clearLog);
  elements.cancelConfirmButton.addEventListener("click", closeConfirmModal);
  elements.nextConfirmButton.addEventListener("click", handleConfirmProgress);
  elements.confirmModal.addEventListener("click", e => e.target === elements.confirmModal && closeConfirmModal());

  startPolling();
}

init();

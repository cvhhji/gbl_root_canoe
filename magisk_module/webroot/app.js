const IMAGE_NAMES = ["abl"];

const state = {
  confirmStep: 0,
  moduleDir: "",
  scriptPath: "",
  status: null,
  pollTimer: null,
  prevStatusRaw: "",
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
  clearLogButton: document.getElementById("clearLogButton"),
  refreshButton: document.getElementById("refreshButton"),
  confirmModal: document.getElementById("confirmModal"),
  confirmText: document.getElementById("confirmText"),
  nextConfirmButton: document.getElementById("nextConfirmButton"),
  cancelConfirmButton: document.getElementById("cancelConfirmButton"),
  updateEfispCheckbox: document.getElementById("updateEfispCheckbox"),
  installSuperfastbootCheckbox: document.getElementById("installSuperfastbootCheckbox"),
  debugModeCheckbox: document.getElementById("debugModeCheckbox"),
};

function getKsuBridge() {
  return globalThis.ksu || window.ksu || null;
}

function shellQuote(value) {
  return `'${String(value).replace(/'/g, `'\\''`)}'`;
}

function toast(message) {
  getKsuBridge()?.toast?.(message);
}

function moduleInfo() {
  const bridge = getKsuBridge();
  if (!bridge?.moduleInfo) throw new Error("当前页面不在 KernelSU WebUI 环境中");
  const raw = bridge.moduleInfo();
  return typeof raw === "string" ? JSON.parse(raw) : raw;
}

function extractStdout(raw) {
  if (raw == null) return "";
  if (typeof raw === "string") {
    try {
      const obj = JSON.parse(raw);
      if (typeof obj?.stdout === "string") return obj.stdout;
      if (typeof obj?.out === "string") return obj.out;
    } catch {}
    return raw;
  }
  if (typeof raw?.stdout === "string") return raw.stdout;
  if (typeof raw?.out === "string") return raw.out;
  return String(raw);
}

function exec(command) {
  const bridge = getKsuBridge();
  if (!bridge?.exec) throw new Error("KernelSU exec API 不可用");
  return extractStdout(bridge.exec(command));
}

function runScript(action, arg) {
  const parts = [`MODDIR=${shellQuote(state.moduleDir)}`, "sh", shellQuote(state.scriptPath), action];
  if (arg) parts.push(shellQuote(arg));
  return exec(parts.join(" ")).replace(/\t/g, "\n");
}

function parseKeyValueOutput(output) {
  const info = {};
  for (const line of output.split(/\r?\n/)) {
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
  if (currentSlot === "-" || targetSlot === "-") {
    elements.imageTableBody.innerHTML =
      '<tr><td colspan="4" class="empty-row">等待槽位检测</td></tr>';
    return;
  }

  elements.imageTableBody.innerHTML = IMAGE_NAMES.map((name) => {
    const srcPath = `/dev/block/by-name/${name}${currentSlot}`;
    const dstPath = `/dev/block/by-name/${name}${targetSlot}`;
    return `
      <tr>
        <td>${escapeHtml(name)}</td>
        <td class="caption">${escapeHtml(srcPath)}</td>
        <td>${escapeHtml(dstPath)}</td>
        <td><span class="status-pill ok">分区拷贝</span></td>
      </tr>
    `;
  }).join("");
}

function renderStatus(status) {
  state.status = status;

  const currentSlot = status.CURRENT_SLOT || "-";
  const targetSlot = status.TARGET_SLOT || "-";
  const running = status.RUNNING === "1";
  const taskState = status.STATE || "idle";
  const taskMessage = status.MESSAGE || "等待操作";

  elements.currentSlot.textContent = currentSlot;
  elements.targetSlot.textContent = targetSlot;
  elements.imageCount.textContent = String(IMAGE_NAMES.length);
  elements.taskMessage.textContent = taskMessage;
  elements.updatedAt.textContent = status.UPDATED_AT || "-";

  elements.stateChip.textContent = running ? "任务运行中" : `状态: ${taskState}`;
  elements.stateChip.className = "chip";
  if (taskState === "success") {
    elements.stateChip.classList.add("chip-success");
  } else if (taskState === "error") {
    elements.stateChip.classList.add("chip-danger");
  } else if (taskState === "warning") {
    elements.stateChip.classList.add("chip-warn");
  } else if (running) {
    elements.stateChip.classList.add("chip-warn");
  }

  elements.slotChip.textContent =
    currentSlot !== "-" && targetSlot !== "-"
      ? `当前 ${currentSlot} → 目标 ${targetSlot}`
      : "槽位未知";

  elements.flashButton.disabled = running || currentSlot === "-" || targetSlot === "-";
  elements.clearLogButton.disabled = running;

  renderTable(currentSlot, targetSlot);
}

function refreshStatus() {
  try {
    const raw = runScript("status");
    if (raw === state.prevStatusRaw) return state.status;
    state.prevStatusRaw = raw;
    const status = parseKeyValueOutput(raw);
    renderStatus(status);
    return status;
  } catch (error) {
    elements.stateChip.textContent = "状态读取失败";
    elements.stateChip.className = "chip chip-danger";
    elements.taskMessage.textContent = error.message;
    return null;
  }
}

function refreshLog() {
  try {
    const log = runScript("tail", "200").trim();
    elements.logOutput.textContent = log || "暂无日志输出";
    elements.logOutput.scrollTop = elements.logOutput.scrollHeight;
  } catch (error) {
    elements.logOutput.textContent = `日志读取失败: ${error.message}`;
  }
}

function closeConfirmModal() {
  state.confirmStep = 0;
  elements.confirmModal.classList.add("hidden");
  elements.confirmModal.setAttribute("aria-hidden", "true");
  elements.nextConfirmButton.textContent = "继续";
}

function openConfirmModal() {
  const targetSlot = state.status?.TARGET_SLOT || "?";
  const withEfisp = Boolean(elements.updateEfispCheckbox?.checked);
  const withSuperfastboot = Boolean(elements.installSuperfastbootCheckbox?.checked);
  const debugMode = Boolean(elements.debugModeCheckbox?.checked);

  if (withSuperfastboot && !withEfisp) {
    toast("安装 superfastboot 需要同时勾选\"更新 efisp\"");
    return;
  }

  state.confirmStep = 1;
  let confirmMsg = debugMode
    ? "调试模式：将执行所有处理流程但不刷写分区，生成的文件保存在 tmp 目录。"
    : `第一次确认: 将把当前槽位的 BL 分区拷贝到槽位 ${targetSlot}`;

  if (!debugMode) {
    if (withEfisp) {
    confirmMsg += withSuperfastboot
        ? "，并更新 efisp（包含 superfastboot loader）。"
        : "，并更新 efisp。";
    } else {
      confirmMsg += "，不更新 efisp。";
    }
    confirmMsg += "请确认槽位无误。";
  }

  elements.confirmText.textContent = confirmMsg;
  elements.nextConfirmButton.textContent = debugMode ? "开始调试" : "继续确认";
  elements.confirmModal.classList.remove("hidden");
  elements.confirmModal.setAttribute("aria-hidden", "false");
}

function handleConfirmProgress() {
  const debugMode = Boolean(elements.debugModeCheckbox?.checked);

  if (state.confirmStep === 1 && !debugMode) {
    state.confirmStep = 2;
    elements.confirmText.textContent =
      "第二次确认: 这是高风险写入操作，错误操作可能导致目标槽位无法启动。确认后将立即开始刷写。";
    elements.nextConfirmButton.textContent = "确认刷写";
    return;
  }

  closeConfirmModal();
  startFlash();
}

function startFlash() {
  const withEfisp = elements.updateEfispCheckbox?.checked;
  const withSuperfastboot = elements.installSuperfastbootCheckbox?.checked;
  const debugMode = elements.debugModeCheckbox?.checked;

  let flashMode = "skip-efisp";
  if (debugMode) {
    flashMode = withSuperfastboot ? "debug-with-superfastboot" : "debug";
  } else if (withEfisp) {
    flashMode = withSuperfastboot ? "update-efisp-with-superfastboot" : "update-efisp";
  }

  try {
    const output = parseKeyValueOutput(runScript("start", flashMode));
    if (output.ALREADY_RUNNING) {
    toast("已有刷写任务在运行");
    } else if (output.STARTED === "1") {
      toast(debugMode ? "调试任务已启动" : "刷写任务已启动");
    } else if (output.FINISHED === "success") {
      toast(debugMode ? "调试完成" : "刷写已完成");
    } else if (output.FINISHED === "warning") {
      toast("BL 刷写完成，但 efisp 未更新");
    } else if (output.FINISHED === "error") {
    toast("任务已结束（失败）");
    } else {
    toast("任务启动失败");
    }
  } catch (error) {
    toast(`启动失败: ${error.message}`);
  }

  manualRefresh();
}

function clearLog() {
  try {
    const output = parseKeyValueOutput(runScript("clear-log"));
    if (output.BUSY === "1") {
      toast("任务运行中，暂时不能清空日志");
      return;
    }
    toast("日志已清空");
  } catch (error) {
    toast(`清空失败: ${error.message}`);
  }

  manualRefresh();
}

function poll() {
  const status = refreshStatus();
  if (status?.RUNNING === "1") refreshLog();
  schedulePoll(status?.RUNNING === "1" ? 3000 : 8000);
}

function schedulePoll(ms) {
  clearTimeout(state.pollTimer);
  state.pollTimer = setTimeout(poll, ms);
}

function manualRefresh() {
  clearTimeout(state.pollTimer);
  state.prevStatusRaw = "";
  refreshStatus();
  refreshLog();
  schedulePoll(state.status?.RUNNING === "1" ? 3000 : 8000);
}

function init() {
  try {
    const info = moduleInfo();
    state.moduleDir = info.moduleDir;
    state.scriptPath = `${state.moduleDir}/bin/bl_flasher.sh`;
  } catch (error) {
    elements.stateChip.textContent = "WebUI 初始化失败";
    elements.stateChip.className = "chip chip-danger";
    elements.taskMessage.textContent = error.message;
    elements.flashButton.disabled = true;
    elements.clearLogButton.disabled = true;
    return;
  }

  elements.refreshButton.addEventListener("click", manualRefresh);
  elements.flashButton.addEventListener("click", openConfirmModal);
  elements.clearLogButton.addEventListener("click", clearLog);
  elements.cancelConfirmButton.addEventListener("click", closeConfirmModal);
  elements.nextConfirmButton.addEventListener("click", handleConfirmProgress);
  elements.confirmModal.addEventListener("click", (event) => {
    if (event.target === elements.confirmModal) {
      closeConfirmModal();
    }
  });

  refreshStatus();
  refreshLog();
  schedulePoll(state.status?.RUNNING === "1" ? 3000 : 8000);
}

init();

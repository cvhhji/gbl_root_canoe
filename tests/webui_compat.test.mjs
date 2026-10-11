import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import vm from "node:vm";

const root = new URL("../targets/magisk_module/module/webroot/", import.meta.url);
const appSource = readFileSync(new URL("app.js", root), "utf8").replace(/^import .*;\r?\n/, "");
const bridgeSource = readFileSync(new URL("kernelsu.js", root), "utf8").replaceAll("export function", "function");
const actions = ["flashButton", "bdsToolsButton", "patchPartButton", "clearLogButton"];

function createHost({ bridge = true, info = { moduleDir: "/data/adb/modules/fake_bl_efisp" },
  status = "CURRENT_SLOT=_a|TARGET_SLOT=_b|RUNNING=0|STATE=idle|USER_LANG=zh", reject = false, errno = 0 } = {}) {
  const nodes = new Map();
  const node = id => {
    if (!nodes.has(id)) nodes.set(id, {
      textContent: "", innerHTML: "", disabled: actions.includes(id), checked: false,
      className: "", dataset: {}, listeners: {},
      classList: { add() {}, contains() { return true; } },
      setAttribute() {}, addEventListener(event, handler) { this.listeners[event] = handler; },
    });
    return nodes.get(id);
  };
  const commands = [];
  const context = vm.createContext({
    document: {
      hidden: false, documentElement: {}, getElementById: node,
      querySelector: selector => node(selector.replace(/^#/, "")), querySelectorAll: () => [],
      addEventListener() {},
    },
    navigator: { language: "zh-CN" }, window: {},
    localStorage: { getItem() { return null; }, setItem() {}, removeItem() {} },
    console: { error() {}, warn() {} }, setTimeout() { return 1; }, clearTimeout() {},
  });
  if (bridge) {
    context.window.ksu = {
      exec(command, options, callback) {
        commands.push(command);
        assert.deepEqual(JSON.parse(options), {});
        if (reject) throw new Error("exec unavailable");
        context.window[callback](errno, command.endsWith("status") ? status : "", errno ? "root denied" : "");
      },
      toast() {},
    };
    if (info !== undefined) context.window.ksu.moduleInfo = () => info;
  }
  vm.runInContext(bridgeSource + "\nglobalThis.ksuExec = exec; globalThis.ksuModuleInfo = moduleInfo;", context);
  return { node, commands, context, async start() {
    vm.runInContext(appSource.replace(/init\(\);\s*$/, "globalThis.started = init();"), context);
    await context.started;
  } };
}

test("APatch command bridge works without KernelSU moduleInfo", () => {
  const context = vm.createContext({ window: {} });
  vm.runInContext(bridgeSource, context);
  assert.equal(vm.runInContext("hasExecBridge()", context), false);
  context.window.ksu = { exec() {} };
  assert.equal(vm.runInContext("hasExecBridge()", context), true);
  assert.equal(vm.runInContext("moduleInfo().moduleDir", context), "/data/adb/modules/fake_bl_efisp");
  context.window.ksu.moduleInfo = () => "{}";
  assert.equal(vm.runInContext("moduleInfo()", context), "{}");
});

test("missing bridge reports failure in the existing status and never dispatches a command", async () => {
  const host = createHost({ bridge: false });
  await host.start();
  assert.match(host.node("stateChip").textContent, /初始化失败/);
  assert.match(host.node("taskMessage").textContent, /命令接口/);
  assert.equal(host.commands.length, 0);
  for (const id of actions) assert.equal(host.node(id).disabled, true);
  await host.node("languageButton").listeners.click();
  for (const id of actions) assert.equal(host.node(id).disabled, true);
});

test("invalid module directories never reach shell execution", async () => {
  for (const info of [null, {}, "broken JSON", { moduleDir: "/sdcard/bad" },
    { moduleDir: "/data/adb/modules/.." }, { moduleDir: "/data/adb/modules/../bad" }]) {
    const host = createHost({ info });
    await host.start();
    assert.equal(host.commands.length, 0);
    for (const id of actions) assert.equal(host.node(id).disabled, true);
  }
});

test("KernelSU object and JSON metadata both initialize existing idle actions", async () => {
  for (const info of [{ moduleDir: "/data/adb/modules/fake_bl_efisp" },
    JSON.stringify({ moduleDir: "/data/adb/modules/fake_bl_efisp" })]) {
    const host = createHost({ info });
    await host.start();
    for (const id of actions) assert.equal(host.node(id).disabled, false);
    assert.equal(host.node("currentSlot").textContent, "_a");
  }
});

test("APatch initializes status and log and dispatches language changes without moduleInfo", async () => {
  const host = createHost();
  delete host.context.window.ksu.moduleInfo;
  await host.start();
  for (const id of actions) assert.equal(host.node(id).disabled, false);
  assert.equal(host.node("currentSlot").textContent, "_a");
  assert.ok(host.commands.some(command => command.endsWith("tail '200'")));
  await host.node("languageButton").listeners.click();
  assert.ok(host.commands.some(command => command.endsWith("set-language 'en'")));
  assert.ok(host.commands.every(command => command.includes("/data/adb/modules/fake_bl_efisp")));
});

test("unusable or malformed status remains disabled after changing language", async () => {
  for (const settings of [
    { reject: true }, { errno: 1 }, { status: "" },
    { status: "CURRENT_SLOT=_a|TARGET_SLOT=_a|RUNNING=0|STATE=idle" },
    { status: "CURRENT_SLOT=_a|TARGET_SLOT=_b|RUNNING=0|STATE=unknown" },
    { status: "CURRENT_SLOT=_a|TARGET_SLOT=_b|RUNNING=unexpected|STATE=idle" },
  ]) {
    const host = createHost(settings);
    await host.start();
    await host.node("languageButton").listeners.click();
    for (const id of actions) assert.equal(host.node(id).disabled, true);
  }
});

test("running tasks keep every action disabled", async () => {
  const host = createHost({ status: "CURRENT_SLOT=_b|TARGET_SLOT=_a|RUNNING=1|STATE=running|USER_LANG=en" });
  await host.start();
  for (const id of actions) assert.equal(host.node(id).disabled, true);
  assert.match(host.node("stateChip").textContent, /running/i);
});

test("the existing page contains no persistent compatibility notice", () => {
  const html = readFileSync(new URL("index.html", root), "utf8");
  const css = readFileSync(new URL("style.css", root), "utf8");
  assert.doesNotMatch(html + css + appSource, /compatibility-notice|compatibilityMessage|compatibilityTitle/);
});

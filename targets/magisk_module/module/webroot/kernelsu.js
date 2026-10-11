let callbackCounter = 0;

export function hasExecBridge() {
  return typeof window.ksu?.exec === "function";
}

export function exec(command, options = {}) {
  return new Promise((resolve, reject) => {
    const name = `exec_callback_${Date.now()}_${callbackCounter++}`;
    const cleanup = () => {
      clearTimeout(timer);
      delete window[name];
    };
    const timer = setTimeout(() => {
      cleanup();
      reject(new Error("KernelSU exec timeout"));
    }, 8000);
    window[name] = (errno, stdout, stderr) => {
      cleanup();
      resolve({ errno, stdout, stderr });
    };
    try {
      window.ksu.exec(command, JSON.stringify(options), name);
    } catch (error) {
      cleanup();
      reject(error);
    }
  });
}

export function toast(message) {
  if (typeof window.ksu?.toast === "function") window.ksu.toast(message);
  else console.warn(message);
}

export function moduleInfo() {
  if (typeof window.ksu?.moduleInfo === "function") return window.ksu.moduleInfo();
  return { moduleDir: "/data/adb/modules/fake_bl_efisp" };
}

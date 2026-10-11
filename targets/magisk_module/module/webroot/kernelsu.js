let callbackCounter = 0;

function callbackName(prefix) {
  return `${prefix}_callback_${Date.now()}_${callbackCounter++}`;
}

export function hasExecBridge() {
  return typeof window.ksu?.exec === "function";
}

export function exec(command, options = {}) {
  return new Promise((resolve, reject) => {
    const name = callbackName("exec");
    window[name] = (errno, stdout, stderr) => {
      delete window[name];
      resolve({ errno, stdout, stderr });
    };
    try {
      window.ksu.exec(command, JSON.stringify(options), name);
    } catch (error) {
      delete window[name];
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
  // APatch and FolkPatch expose ksu.exec without KernelSU's moduleInfo API.
  return { moduleDir: "/data/adb/modules/fake_bl_efisp" };
}
